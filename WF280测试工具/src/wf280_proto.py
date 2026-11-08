# -*- coding: utf-8 -*-
"""WF280 全功能测试协议封装。

覆盖固件里 WF280 相关的全部命令：

    cmd[0] = 14                    异步整机测试（IsTaskDone 队列）  32 寄存器
    {1, 0x211}                     同步回读实际轨压                 状态 + mV
    {11}                           OTP 双 bank 校验                 结果 + 失败路号
    {1, 0x208, meas, settle}       输出测试 3.3 V                   状态 + 6 字节
    {1, 0x210, meas, settle}       输出测试 2.0 V                   状态 + 6 字节
    {1, 0x209, count}              读正常 bank 系数 (0x00..)        状态 + N 路
    {1, 0x207, count}              读 M1 bank 系数 + 电流 (0x80..)  状态 + 电流 + N 路
    {1, 0x201, mv_lo, mv_hi}       设定 DAC 轨压（辅助）            状态

对应固件：Core/Src/CmdHelper.c、Core/Src/wf280.c、Core/Inc/wf280.h
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

from mb_rtu import HOLDING_BASE, INPUT_BASE, RtuMaster

# ---------------- 命令码（与 CmdHelper.h 一致）----------------

CMD_SET_VDD = 0x201           # {1, 0x201, lo, hi}
CMD_GET_VERSION = 0x104       # {1, 0x104}    固件版本号
CMD_WF280_M1 = 0x207          # {1, 0x207, count}
CMD_WF280_OUTDATA = 0x208     # {1, 0x208, measurement, settle_ms}
CMD_WF280_COEFF = 0x209       # {1, 0x209, count}
CMD_WF280_2V_OUTDATA = 0x210  # {1, 0x210, measurement, settle_ms}
CMD_WF280_RAIL = 0x211        # {1, 0x211}
CMD_GET_TASK_STATUS = 0x400
CMD_GET_ALLTEST_DATA = 0x401
CMD_WF280_OTP = 11            # {11}
CMD_WF280_FULL_TEST = 14      # {14}

# ---------------- 固件常量 ----------------

RESULT_LENGTH = 32            # WF280_TEST_RESULT_LENGTH
OUTPUT_RESULT_LENGTH = 7      # WF280_OUTPUT_RESULT_LENGTH
COEFF_MAX = 32                # WF280_COEFF_MAX
RAIL_MIN_MV = 1500
RAIL_TOLERANCE_PCT = 10
TEST_RAIL_MV = 3300
SHORT_CURRENT_UA = 20000
SETTLE_3V3_MS = 20
SETTLE_2V_MS = 200

RAIL_FLAG_MEASURED = 0x01
RAIL_FLAG_DEVIATED = 0x02
RAIL_FLAG_UNDER = 0x04

STATUS_TEXT = {
    0x00: "OK",
    0x01: "I2C 通讯失败",
    0x02: "NOR 忙等超时",
    0x03: "命令响应超时",
    0xE1: "电流短路",
    0xE2: "轨压异常",
    0xF1: "设备忙（测试进行中）",
    0xF2: "命令长度错误",
}


def status_text(code: int) -> str:
    return STATUS_TEXT.get(code, f"未定义状态 0x{code:04X}")


def decode_u32(regs: list[int], offset: int) -> int:
    """四个寄存器拼成 32 位小端原始码（固件是一个字节一个寄存器）。"""
    value = 0
    for i in range(4):
        value |= (regs[offset + i] & 0xFF) << (8 * i)
    return value


class Result:
    """所有测试结果的共同接口：ok 与 rows()，界面和命令行共用。"""

    @property
    def ok(self) -> bool:  # pragma: no cover - 由子类实现
        raise NotImplementedError

    def rows(self) -> list[tuple[str, str, str]]:  # pragma: no cover
        raise NotImplementedError


# ---------------- ⓪ 固件版本 ----------------


@dataclass
class VersionResult(Result):
    """{1, 0x104}：FIRMWARE_VERSION 两字节，高字节是主版本。"""

    major: int
    minor: int

    @property
    def ok(self) -> bool:
        # 全 0 基本可以断定设备没真正应答
        return not (self.major == 0 and self.minor == 0)

    @property
    def text(self) -> str:
        return f"v{self.major}.{self.minor}"

    def rows(self):
        return [("固件版本", self.text, "OK" if self.ok else "NG"),
                ("原始值", f"0x{self.major:02X} 0x{self.minor:02X}", "—")]


# ---------------- ① 整机测试 ----------------


@dataclass
class FullTestResult(Result):
    """AllTestFor280 的 32 个结果寄存器。"""

    regs: list[int] = field(default_factory=list)

    def __post_init__(self):
        if len(self.regs) < RESULT_LENGTH:
            self.regs = list(self.regs) + [0] * (RESULT_LENGTH - len(self.regs))

    status = property(lambda self: self.regs[0])
    rail_mv = property(lambda self: self.regs[1])
    rail_set_mv = property(lambda self: self.regs[2])
    rail_flag = property(lambda self: self.regs[3])
    work_ua = property(lambda self: self.regs[4])
    sleep_ua = property(lambda self: self.regs[5])
    work_code = property(lambda self: decode_u32(self.regs, 6))
    sleep_code = property(lambda self: decode_u32(self.regs, 10))
    bt_data = property(lambda self: self.regs[14:20])
    coeff = property(lambda self: self.regs[20:22])
    m1_coeff = property(lambda self: self.regs[22:24])

    @property
    def ok(self) -> bool:
        return self.status == 0

    @property
    def rail_verdict(self) -> str:
        flags = self.rail_flag
        if flags & RAIL_FLAG_UNDER:
            return f"轨未建立（{self.rail_mv} mV < {RAIL_MIN_MV} mV）"
        if not flags & RAIL_FLAG_MEASURED:
            return "ADC 未读到有效值"
        if flags & RAIL_FLAG_DEVIATED:
            return f"偏离设定值超过 {RAIL_TOLERANCE_PCT}%"
        return "正常"

    @property
    def all_ok(self) -> bool:
        """整机判定：总状态 OK 且轨压既没欠压也没超差。"""
        return self.ok and not (self.rail_flag & (RAIL_FLAG_UNDER | RAIL_FLAG_DEVIATED))

    def rows(self):
        rows: list[tuple[str, str, str]] = [
            ("总体状态", status_text(self.status), "OK" if self.ok else "NG"),
            ("轨压（实测）", f"{self.rail_mv} mV", self.rail_verdict),
            ("轨压（设定）", f"{self.rail_set_mv} mV", "—"),
        ]
        flags = []
        if self.rail_flag & RAIL_FLAG_MEASURED:
            flags.append("已测量")
        if self.rail_flag & RAIL_FLAG_DEVIATED:
            flags.append("超差")
        if self.rail_flag & RAIL_FLAG_UNDER:
            flags.append("欠压")
        rows.append(("轨压标志", " / ".join(flags) if flags else "无",
                     f"0x{self.rail_flag:02X}"))
        rows.append(("工作电流", f"{self.work_ua} uA",
                     "NG 疑似短路" if self.work_ua >= SHORT_CURRENT_UA else "OK"))
        rows.append(("休眠电流", f"{self.sleep_ua} uA",
                     "NG 疑似短路" if self.sleep_ua >= SHORT_CURRENT_UA else "OK"))
        rows.append(("工作电流原始码", f"0x{self.work_code:08X}", "—"))
        rows.append(("休眠电流原始码", f"0x{self.sleep_code:08X}", "—"))
        rows.append(("0xAC 测量数据",
                     " ".join(f"{b:02X}" for b in self.bt_data), "—"))
        rows.append(("正常 bank 系数",
                     " ".join(f"0x{c:04X}" for c in self.coeff), "—"))
        rows.append(("M1 bank 系数",
                     " ".join(f"0x{c:04X}" for c in self.m1_coeff), "—"))
        return rows


# ---------------- ② 轨压回读 ----------------


@dataclass
class RailReading(Result):
    status: int
    mv: int

    @property
    def ok(self) -> bool:
        return self.status == 0 and self.mv > 0

    @property
    def verdict(self) -> str:
        if self.status != 0:
            return status_text(self.status)
        if self.mv < RAIL_MIN_MV:
            return f"轨未建立（{self.mv} mV < {RAIL_MIN_MV} mV）"
        low = TEST_RAIL_MV * (100 - RAIL_TOLERANCE_PCT) // 100
        high = TEST_RAIL_MV * (100 + RAIL_TOLERANCE_PCT) // 100
        if not low <= self.mv <= high:
            return f"偏离 ±{RAIL_TOLERANCE_PCT}%（正常 {low}~{high} mV）"
        return "正常"

    def rows(self):
        return [("轨压（实测）", f"{self.mv} mV", self.verdict),
                ("状态", status_text(self.status), "OK" if self.status == 0 else "NG")]


# ---------------- ③ OTP 校验 ----------------


@dataclass
class OtpResult(Result):
    """{11}：两个 bank 逐路比对，out[0]=1 表示通过，0xFF 表示失败。"""

    status: int
    fail_index: int = 0

    @property
    def ok(self) -> bool:
        return self.status == 1

    @property
    def verdict(self) -> str:
        if self.ok:
            return "通过"
        if self.status == 0xFF:
            return f"第 {self.fail_index} 路不一致" if self.fail_index else "校验失败"
        return status_text(self.status)

    def rows(self):
        return [("OTP 校验", "通过" if self.ok else "不一致", "OK" if self.ok else "NG"),
                ("失败路号", str(self.fail_index), "—")]


# ---------------- ④⑤ 输出测试 3.3 V / 2.0 V ----------------


@dataclass
class OutputResult(Result):
    """{1, 0x208/0x210, measurement, settle}：状态 + 6 个原始字节。"""

    supply_mv: int
    status: int
    data: list[int] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.status == 0

    def rows(self):
        return [
            ("供电", f"{self.supply_mv} mV", "—"),
            ("状态", status_text(self.status), "OK" if self.ok else "NG"),
            ("0xAC 数据", " ".join(f"{b:02X}" for b in self.data), "—"),
            ("数据（十进制）", " ".join(str(b) for b in self.data), "—"),
        ]


# ---------------- ⑥ 读系数 ----------------


@dataclass
class CoeffResult(Result):
    """{1, 0x209, count}：正常 bank（0x00 起）的 N 路系数。"""

    start: int
    status: int
    values: list[int] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.status == 0

    def rows(self):
        rows = [("状态", status_text(self.status), "OK" if self.ok else "NG")]
        for i, value in enumerate(self.values):
            rows.append((f"系数 0x{self.start + i:02X}", f"0x{value:04X}", "—"))
        return rows


# ---------------- ⑦ 读 M1 ----------------


@dataclass
class M1Result(Result):
    """{1, 0x207, count}：2.0 V 下的待机/工作电流原始码 + M1 系数。"""

    status: int
    standby_code: int = 0
    work_code: int = 0
    values: list[int] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.status == 0

    def rows(self):
        rows = [
            ("状态", status_text(self.status), "OK" if self.ok else "NG"),
            ("待机电流码", f"0x{self.standby_code:04X}", "—"),
            ("工作电流码", f"0x{self.work_code:04X}", "—"),
        ]
        for i, value in enumerate(self.values):
            rows.append((f"M1 系数 0x{0x80 + i:02X}", f"0x{value:04X}", "—"))
        return rows


# ---------------- 命令交互 ----------------


class Wf280Tester:
    """把固件的命令-结果约定包装成直观的方法。"""

    def __init__(self, master: RtuMaster, *, cmd_addr: int = HOLDING_BASE,
                 in_addr: int = INPUT_BASE):
        self.master = master
        self.cmd_addr = cmd_addr
        self.in_addr = in_addr

    # 写命令 → 读结果。固件在写保持寄存器的回调里同步执行 Parse()，
    # 结果放在 sendBuf 里，随后从输入寄存器读回来。
    def _command(self, values: list[int], read_count: int) -> list[int]:
        self.master.write_multiple_registers(self.cmd_addr, values)
        if read_count <= 0:
            return []
        return self.master.read_input_registers(self.in_addr, read_count)

    # ---- ⓪ 固件版本 ----
    def read_version(self) -> VersionResult:
        out = self._command([1, CMD_GET_VERSION], 2)
        return VersionResult(major=out[0], minor=out[1])

    # ---- ① 异步整机测试 ----
    def get_task_status(self) -> int:
        """1 = 已入队/正在跑，0 = 空闲/已完成。"""
        return self._command([1, CMD_GET_TASK_STATUS], 1)[0]

    def read_results(self, count: int = RESULT_LENGTH) -> FullTestResult:
        return FullTestResult(self._command([1, CMD_GET_ALLTEST_DATA], count))

    def start_full_test(self) -> int:
        """触发 cmd[0]=14，固件回 out[0]=1 表示已入队。"""
        return self._command([CMD_WF280_FULL_TEST], 1)[0]

    def run_full_test(self, timeout: float = 20.0, interval: float = 0.2,
                      on_tick=None) -> FullTestResult:
        ack = self.start_full_test()
        if ack != 1:
            raise RuntimeError(f"触发测试失败，设备返回 {status_text(ack)}")
        deadline = time.monotonic() + timeout
        start = time.monotonic()
        while True:
            if not self.get_task_status():
                break
            if on_tick:
                on_tick(time.monotonic() - start)
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"整机测试超时（{timeout:.0f} s 未完成），检查 DUT 是否插好")
            time.sleep(interval)
        return self.read_results()

    # ---- ② 轨压 ----
    def read_rail(self) -> RailReading:
        out = self._command([1, CMD_WF280_RAIL], 2)
        return RailReading(status=out[0], mv=out[1])

    # ---- ③ OTP ----
    def read_otp(self) -> OtpResult:
        out = self._command([CMD_WF280_OTP], 2)
        return OtpResult(status=out[0], fail_index=out[1])

    # ---- ④⑤ 输出测试 ----
    def test_output(self, supply_mv: int = TEST_RAIL_MV,
                    measurement: int = 0, settle_ms: int | None = None) -> OutputResult:
        if supply_mv not in (2000, 3300):
            raise ValueError("输出测试只支持 2000 mV 或 3300 mV")
        if settle_ms is None:
            settle_ms = SETTLE_2V_MS if supply_mv == 2000 else SETTLE_3V3_MS
        cmd_head = (CMD_WF280_2V_OUTDATA if supply_mv == 2000
                    else CMD_WF280_OUTDATA)
        out = self._command(
            [1, cmd_head, measurement & 0xFFFF, settle_ms & 0xFFFF],
            OUTPUT_RESULT_LENGTH)
        return OutputResult(supply_mv=supply_mv, status=out[0], data=out[1:7])

    def test_output_3v3(self, measurement: int = 0,
                        settle_ms: int | None = None) -> OutputResult:
        return self.test_output(3300, measurement, settle_ms)

    def test_output_2v0(self, measurement: int = 0,
                        settle_ms: int | None = None) -> OutputResult:
        return self.test_output(2000, measurement, settle_ms)

    # ---- ⑥ 系数 ----
    def read_coeff(self, count: int = 4) -> CoeffResult:
        count = max(1, min(COEFF_MAX, int(count)))
        out = self._command([1, CMD_WF280_COEFF, count], count + 1)
        return CoeffResult(start=0, status=out[0], values=out[1:count + 1])

    # ---- ⑦ M1 ----
    def read_m1(self, count: int = 4) -> M1Result:
        count = max(1, min(COEFF_MAX, int(count)))
        out = self._command([1, CMD_WF280_M1, count], 5 + count)
        standby = (out[1] & 0xFF) | ((out[2] & 0xFF) << 8)
        working = (out[3] & 0xFF) | ((out[4] & 0xFF) << 8)
        return M1Result(status=out[0], standby_code=standby,
                        work_code=working, values=out[5:5 + count])

    # ---- 辅助：设定 DAC 轨压 ----
    def set_supply(self, mv: int) -> int:
        mv = max(0, min(5000, int(mv)))
        out = self._command([1, CMD_SET_VDD, mv & 0xFF, (mv >> 8) & 0xFF], 1)
        return out[0]
