# -*- coding: utf-8 -*-
"""极简 Modbus RTU 主站，只实现本工程用到的两个功能码。

不依赖 pymodbus，只用 pyserial，方便在产线电脑上直接跑。

固件里的约定（见 Middlewares/Third_Party/FreeModbus/Modbus.c）：
    eMBRegHoldingCB 直接比较 usAddress == 40001
    eMBRegInputCB   直接比较 usAddress == 30001
所以 PDU 里的寄存器地址要写 40001 / 30001 本身，不能用 0 基偏移。
这是这块板子既有的约定，别按标准 Modbus 的偏移去改，否则会返回
MB_ENOREG（异常码 2）。
"""

from __future__ import annotations

import struct
import time

HOLDING_BASE = 40001   # 命令寄存器（主站写）
INPUT_BASE = 30001     # 结果寄存器（主站读）


class ModbusError(Exception):
    """通用 Modbus 通讯错误。"""

    def __init__(self, message: str, code: int | None = None):
        super().__init__(message)
        self.code = code


class ModbusTimeout(ModbusError):
    """从机没有应答。"""


class ModbusExceptionResponse(ModbusError):
    """从机返回了标准 Modbus 异常码。"""

    NAMES = {
        1: "非法功能码",
        2: "非法寄存器地址",
        3: "非法数据值",
        4: "从机设备故障",
        6: "从机忙",
    }

    def __init__(self, code: int):
        name = self.NAMES.get(code, "未知异常")
        super().__init__(f"从机返回异常码 0x{code:02X}（{name}）", code)


def crc16(data: bytes) -> int:
    """Modbus CRC16，多项式 0xA001，初值 0xFFFF。"""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def with_crc(frame: bytes) -> bytes:
    """在帧尾追加 CRC（低字节在前）。"""
    return frame + struct.pack("<H", crc16(frame))


class RtuMaster:
    """Modbus RTU 主站。"""

    def __init__(self, port: str, baudrate: int = 115200, slave: int = 1,
                 timeout: float = 1.0):
        self.port = port
        self.baudrate = baudrate
        self.slave = slave
        self.timeout = timeout
        self._ser = None
        # 统计，方便排查
        self.ok_count = 0
        self.err_count = 0

    # ---------- 连接管理 ----------

    def open(self) -> "RtuMaster":
        import serial  # 延迟导入，--help / 自检时不需要真串口

        self._ser = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=self.timeout,
            write_timeout=self.timeout,
            inter_byte_timeout=0.2,
        )
        # 固件是 8N1，115200，从机地址 1（app_freertos.c）
        time.sleep(0.1)
        self._ser.reset_input_buffer()
        return self

    def close(self) -> None:
        if self._ser is not None:
            try:
                self._ser.close()
            finally:
                self._ser = None

    @property
    def is_open(self) -> bool:
        return self._ser is not None and self._ser.is_open

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    # ---------- 底层收发 ----------

    def _read_exact(self, count: int) -> bytes:
        buf = bytearray()
        deadline = time.monotonic() + self.timeout
        while len(buf) < count:
            chunk = self._ser.read(count - len(buf))
            if chunk:
                buf.extend(chunk)
                continue
            if time.monotonic() >= deadline:
                raise ModbusTimeout(
                    f"等待应答超时（已收到 {len(buf)}/{count} 字节）")
        return bytes(buf)

    def _transact(self, pdu: bytes) -> bytes:
        """发送一个 PDU 并返回应答 PDU（不含从机地址和 CRC）。"""
        if not self.is_open:
            raise ModbusError("串口未打开")

        frame = with_crc(bytes([self.slave]) + pdu)
        self._ser.reset_input_buffer()
        self._ser.write(frame)
        self._ser.flush()

        head = self._read_exact(2)
        if head[0] != self.slave:
            self.err_count += 1
            raise ModbusError(
                f"应答地址不匹配：期望 {self.slave}，收到 {head[0]}")

        if head[1] & 0x80:
            rest = self._read_exact(3)
            self.err_count += 1
            raise ModbusExceptionResponse(rest[0])

        if head[1] != pdu[0]:
            self.err_count += 1
            raise ModbusError(
                f"应答功能码不匹配：期望 0x{pdu[0]:02X}，收到 0x{head[1]:02X}")

        fc = head[1]
        # after_fc = 功能码之后、CRC 之前的所有字节（读类应答里含字节数字段）
        if fc in (0x03, 0x04):
            byte_count = self._read_exact(1)
            after_fc = byte_count + self._read_exact(byte_count[0] + 2)
        elif fc in (0x06, 0x10):
            after_fc = self._read_exact(6)
        else:
            after_fc = self._read_exact(2)

        payload, crc = after_fc[:-2], after_fc[-2:]
        check = struct.pack("<H", crc16(frame[:1] + bytes([fc]) + payload))
        if crc != check:
            self.err_count += 1
            raise ModbusError("CRC 校验失败，帧可能被截断")
        self.ok_count += 1
        return bytes([fc]) + payload

    # ---------- 功能码 ----------

    def read_input_registers(self, address: int, count: int) -> list[int]:
        """0x04 读输入寄存器。"""
        pdu = struct.pack(">BHH", 0x04, address, count)
        resp = self._transact(pdu)
        data = resp[2:]
        return list(struct.unpack(f">{count}H", data))

    def read_holding_registers(self, address: int, count: int) -> list[int]:
        """0x03 读保持寄存器（本工程主要用于回读刚写入的命令）。"""
        pdu = struct.pack(">BHH", 0x03, address, count)
        resp = self._transact(pdu)
        data = resp[2:]
        return list(struct.unpack(f">{count}H", data))

    def write_multiple_registers(self, address: int, values: list[int]) -> None:
        """0x10 写多个保持寄存器。"""
        count = len(values)
        pdu = struct.pack(">BHHB", 0x10, address, count, count * 2)
        pdu += struct.pack(f">{count}H", *values)
        self._transact(pdu)
