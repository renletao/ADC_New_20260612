# -*- coding: utf-8 -*-
"""离线自检：不需要真板子，用假串口模拟固件行为。

逐条验证 WF280 的全部命令：
    Modbus 帧的 CRC / 功能码 / 寄存器地址（40001 写、30001 读）
    cmd 14 的异步队列流程（触发 → 轮询 IsTaskDone → 取结果）
    0x211 轨压、11 OTP、0x208/0x210 输出测试、0x209 系数、0x207 M1、0x201 设压

运行：python selftest.py
"""

from __future__ import annotations

import struct
import sys

import mb_rtu
from mb_rtu import RtuMaster
from wf280_proto import (
    COEFF_MAX,
    RAIL_FLAG_MEASURED,
    RAIL_FLAG_UNDER,
    FullTestResult,
    Wf280Tester,
    raw_to_ua,
    signed24,
    status_text,
)

SLAVE = 1


class FakeFirmwareSerial:
    """假串口：收到主站帧就按固件逻辑生成应答。"""

    def __init__(self, *, rail_mv=3298, busy_polls=3, stuck=False,
                 otp_fail=0, version=(1, 0),
                 holding_base=40000, input_base=30000):
        self.is_open = True
        self.rx = bytearray()
        self.tx_buffer = bytearray()
        self.rail_mv = rail_mv
        self.busy_polls = busy_polls
        self.stuck = stuck
        self.otp_fail = otp_fail          # 0=通过，非 0=第几路失败
        self.version = version
        self.holding_base = holding_base  # 真板子实测是 40000/30000（0 基）
        self.input_base = input_base
        self.busy = False
        self.send_buf = [0] * 120
        self.commands_seen: list[list[int]] = []

    # ---------- pyserial 接口 ----------
    def reset_input_buffer(self):
        self.rx.clear()

    def write(self, data):
        self.rx.extend(data)

    def flush(self):
        pass

    def close(self):
        self.is_open = False

    def read(self, size):
        if not self.tx_buffer:
            self._handle_request()
        chunk = bytes(self.tx_buffer[:size])
        del self.tx_buffer[:size]
        return chunk

    # ---------- 假固件行为 ----------
    def _results(self) -> list[int]:
        regs = [0] * 32
        regs[1] = self.rail_mv
        regs[2] = 3300
        regs[3] = RAIL_FLAG_MEASURED
        regs[4] = 850
        regs[5] = 12
        for i, byte in enumerate(b"\x11\x22\x33\x44"):
            regs[6 + i] = byte
        for i, byte in enumerate(b"\x05\x06\x07\x08"):
            regs[10 + i] = byte
        for i, byte in enumerate(b"\xA1\xA2\xA3\xA4\xA5\xA6"):
            regs[14 + i] = byte
        regs[20], regs[21] = 0x1234, 0x5678
        regs[22], regs[23] = 0x9ABC, 0xDEF0
        return regs

    def _handle_request(self):
        if len(self.rx) < 8:
            return
        frame = bytes(self.rx)
        self.rx.clear()

        body, crc = frame[:-2], struct.unpack("<H", frame[-2:])[0]
        if mb_rtu.crc16(body) != crc:
            return                       # 固件直接丢掉 CRC 错的帧
        slave, fc = body[0], body[1]
        if slave != SLAVE:
            return

        if fc == 0x10:
            address, count, _ = struct.unpack(">HHB", body[2:7])
            if address != self.holding_base:
                # 地址不对就回异常码 02，跟真板子行为一致
                payload = struct.pack(">BB", 0x10 | 0x80, 0x02)
            else:
                values = list(struct.unpack(f">{count}H", body[7:]))
                self.commands_seen.append(values)
                self._run_command(values)
                payload = struct.pack(">BHH", 0x10, address, count)
        elif fc == 0x04:
            address, count = struct.unpack(">HH", body[2:6])
            if address != self.input_base:
                payload = struct.pack(">BB", 0x04 | 0x80, 0x02)
            else:
                data = b"".join(struct.pack(">H", v) for v in self.send_buf[:count])
                payload = struct.pack(">BB", 0x04, len(data)) + data
        else:
            payload = struct.pack(">BB", fc | 0x80, 0x01)

        self.tx_buffer.extend(mb_rtu.with_crc(bytes([SLAVE]) + payload))

    def _run_command(self, cmd: list[int]):
        # 对应 CmdHelper.c 的 Parse()
        if self.busy and not (cmd[0] == 1 and len(cmd) > 1
                              and cmd[1] in (0x400, 0x401)):
            self.send_buf = [0] * 120
            self.send_buf[0] = 0xF1
            return

        if cmd[0] == 14:                      # 异步整机测试
            self.busy = True
            self.busy_polls = 3
            self.send_buf = [0] * 120
            self.send_buf[0] = 1
            return

        if cmd[0] == 11:                      # OTP 校验
            self.send_buf = [0] * 120
            if self.otp_fail:
                self.send_buf[0] = 0xFF
                self.send_buf[1] = self.otp_fail
            else:
                self.send_buf[0] = 1
                self.send_buf[1] = 0
            return

        self.send_buf = [0] * 120
        if cmd[0] != 1 or len(cmd) < 2:
            return
        head = cmd[1]

        if head == 0x400:                     # 查询 IsTaskDone
            if self.stuck:
                self.send_buf[0] = 1
                return
            self.send_buf[0] = 1 if self.busy else 0
            if self.busy:
                self.busy_polls -= 1
                if self.busy_polls <= 0:
                    self.busy = False
            return

        if head == 0x401:                     # 取整机结果
            if self.busy:
                self.send_buf[0] = 0xF1
            else:
                self.send_buf[:32] = self._results()
            return

        if head == 0x211:                     # 同步读轨压
            if self.rail_mv:
                self.send_buf[0] = 0
                self.send_buf[1] = self.rail_mv
            else:
                self.send_buf[0] = 0xE2
            return

        if head == 0x104:                     # 固件版本
            self.send_buf[0] = self.version[0]
            self.send_buf[1] = self.version[1]
            return

        if head == 0x201:                     # 设定 DAC 轨压
            self.supply_mv = cmd[2] | (cmd[3] << 8)
            self.send_buf[0] = 0
            return

        if head in (0x208, 0x210):            # 输出测试
            self.send_buf[0] = 0
            for i, byte in enumerate(b"\xB1\xB2\xB3\xB4\xB5\xB6"):
                self.send_buf[1 + i] = byte
            return

        if head == 0x209:                     # 正常 bank 系数
            count = cmd[2]
            self.send_buf[0] = 0
            for i in range(count):
                self.send_buf[1 + i] = 0x0A00 + i
            return

        if head == 0x207:                     # M1
            count = cmd[2]
            self.send_buf[0] = 0
            self.send_buf[1] = 0x34           # 待机码低字节
            self.send_buf[2] = 0x12           # 待机码高字节
            self.send_buf[3] = 0x78           # 工作码低字节
            self.send_buf[4] = 0x56           # 工作码高字节
            for i in range(count):
                self.send_buf[5 + i] = 0x8000 + i
            return


def make_tester(**kwargs):
    fake = FakeFirmwareSerial(**kwargs)
    master = RtuMaster("FAKE")
    master._ser = fake                        # 跳过真实串口
    return fake, Wf280Tester(master)


CASES = []


def case(name):
    def deco(fn):
        CASES.append((name, fn))
        return fn
    return deco


@case("电流换算：负原始码按 24 位有符号解释")
def _():
    # 真机读到的原始码，固件因 bit31 判断把它当无效返回 0
    assert signed24(0xFFFD29AE) == -185938
    assert signed24(0x00000017) == 23
    ua = raw_to_ua(0xFFFD29AE, 3296)
    assert 605 < ua < 612, ua          # 约 608.8 uA
    assert raw_to_ua(0xFFFD29AE, 0) == 0    # 没轨压不换算


@case("电流显示：按原始码算出实际值，不标“推算”")
def _():
    result = FullTestResult([0] * 32)
    result.regs[1] = 3296          # 实测轨压
    result.regs[4] = 0             # 固件给的工作电流 0（小于 1 uA 会被截断）
    for i, byte in enumerate(b"\xAE\x29\xFD\xFF"):
        result.regs[6 + i] = byte
    texts = {row[0]: row for row in result.rows()}
    assert "工作电流" in texts, list(texts)
    value = float(texts["工作电流"][1].split()[0])
    assert 605 <= value <= 612, value
    assert not any("推算" in name for name in texts), list(texts)


@case("CRC16 标准向量")
def _():
    # “01 03 00 00 00 01” 的 Modbus CRC = 0x0A84，线上低字节在前 84 0A
    assert mb_rtu.crc16(bytes([1, 3, 0, 0, 0, 1])) == 0x0A84
    assert mb_rtu.with_crc(bytes([1, 3, 0, 0, 0, 1]))[-2:] == b"\x84\x0a"


@case("寄存器基址：识别 0 基（40000/30000，真板子就是这种）")
def _():
    _, tester = make_tester()
    assert tester.detect_bases() == (40000, 30000)


@case("寄存器基址：兼容 1 基固件（40001/30001）")
def _():
    fake, tester = make_tester(holding_base=40001, input_base=30001)
    assert tester.detect_bases() == (40001, 30001)
    result = tester.read_version()
    assert result.ok and fake.commands_seen[-1] == [1, 0x104]


@case("0x104 固件版本：读得到才算连对设备")
def _():
    fake, tester = make_tester(version=(1, 0))
    result = tester.read_version()
    assert fake.commands_seen[-1] == [1, 0x104]
    assert result.text == "v1.0" and result.ok

    # 全 0 视为设备没正常应答
    _, blank = make_tester(version=(0, 0))
    assert not blank.read_version().ok


@case("0x211 轨压：状态 + mV")
def _():
    fake, tester = make_tester(rail_mv=3298)
    reading = tester.read_rail()
    assert (reading.status, reading.mv) == (0, 3298)
    assert reading.ok and "3.3 V" in reading.verdict, reading.verdict
    assert fake.commands_seen[-1] == [1, 0x211]


@case("0x211 轨压：2.0 V 档同样判为正常")
def _():
    _, tester = make_tester(rail_mv=1992)
    reading = tester.read_rail()
    assert reading.ok and "2.0 V" in reading.verdict, reading.verdict


@case("0x211 轨压：轨没起来")
def _():
    _, tester = make_tester(rail_mv=0)
    reading = tester.read_rail()
    assert reading.status == 0xE2 and not reading.ok
    assert status_text(0xE2) == "轨压异常"


@case("14 整机测试：异步流程与结果解码")
def _():
    fake, tester = make_tester(rail_mv=3298)
    ticks: list[float] = []
    result = tester.run_full_test(interval=0.0, on_tick=ticks.append)

    assert result.ok and result.all_ok
    assert result.rail_mv == 3298 and result.rail_set_mv == 3300
    assert (result.work_ua, result.sleep_ua) == (850, 12)
    assert result.work_code == 0x44332211
    assert result.sleep_code == 0x08070605
    assert result.bt_data == [0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6]
    assert result.coeff == [0x1234, 0x5678]
    assert result.m1_coeff == [0x9ABC, 0xDEF0]
    assert ticks, "应当至少有一次进度回调"
    assert fake.commands_seen[0] == [14]
    assert [1, 0x400] in fake.commands_seen
    assert fake.commands_seen[-1] == [1, 0x401]


@case("14 整机测试：轨欠压 → 0xE2")
def _():
    fake, tester = make_tester(rail_mv=1200)
    original = fake._results

    def under_voltage():
        regs = original()
        regs[1] = 1200
        regs[3] = RAIL_FLAG_MEASURED | RAIL_FLAG_UNDER
        regs[0] = 0xE2
        return regs

    fake._results = under_voltage
    result = tester.run_full_test(interval=0.0)
    assert result.status == 0xE2 and not result.all_ok
    assert "轨未建立" in result.rail_verdict


@case("14 整机测试：超时保护")
def _():
    _, tester = make_tester(stuck=True)
    try:
        tester.run_full_test(timeout=0.3, interval=0.05)
    except TimeoutError as exc:
        assert "超时" in str(exc)
    else:
        raise AssertionError("应当在超时后抛异常")


@case("11 OTP 校验：通过 / 不一致")
def _():
    fake, tester = make_tester()
    ok_result = tester.read_otp()
    assert ok_result.ok and ok_result.verdict == "通过"
    assert fake.commands_seen[-1] == [11]

    fake.otp_fail = 7
    bad = tester.read_otp()
    assert not bad.ok and bad.fail_index == 7
    assert "7" in bad.verdict


@case("0x208 输出测试 3.3 V：命令与数据")
def _():
    fake, tester = make_tester()
    result = tester.test_output_3v3(measurement=0x0003, settle_ms=120)
    assert fake.commands_seen[-1] == [1, 0x208, 0x0003, 120]
    assert result.ok and result.supply_mv == 3300
    assert result.data == [0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6]


@case("0x210 输出测试 2.0 V：用另一条命令头")
def _():
    fake, tester = make_tester()
    result = tester.test_output_2v0()
    assert fake.commands_seen[-1] == [1, 0x210, 0x0000, 400]
    assert result.ok and result.supply_mv == 2000


@case("输出测试：非法供电电压被拦下")
def _():
    _, tester = make_tester()
    try:
        tester.test_output(2500)
    except ValueError as exc:
        assert "2000" in str(exc) or "3300" in str(exc)
    else:
        raise AssertionError("2500 mV 应当被拒绝")


@case("0x209 读系数：路数上限与解码")
def _():
    fake, tester = make_tester()
    result = tester.read_coeff(4)
    assert fake.commands_seen[-1] == [1, 0x209, 4]
    assert result.ok and result.values == [0x0A00, 0x0A01, 0x0A02, 0x0A03]

    tester.read_coeff(999)                 # 超上限应被夹到 COEFF_MAX
    assert fake.commands_seen[-1] == [1, 0x209, COEFF_MAX]


@case("0x207 读 M1：电流码与系数")
def _():
    fake, tester = make_tester()
    result = tester.read_m1(3)
    assert fake.commands_seen[-1] == [1, 0x207, 3]
    assert result.ok
    assert result.standby_code == 0x1234
    assert result.work_code == 0x5678
    assert result.values == [0x8000, 0x8001, 0x8002]


@case("0x201 设定轨压：低字节在前")
def _():
    fake, tester = make_tester()
    assert tester.set_supply(3300) == 0
    assert fake.commands_seen[-1] == [1, 0x201, 3300 & 0xFF, 3300 >> 8]
    assert fake.supply_mv == 3300


def main() -> int:
    failed = 0
    for name, fn in CASES:
        try:
            fn()
        except Exception as exc:  # noqa: BLE001
            failed += 1
            print(f"[FAIL] {name}: {type(exc).__name__}: {exc}")
        else:
            print(f"[ OK ] {name}")
    print(f"\n{len(CASES) - failed}/{len(CASES)} 通过")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
