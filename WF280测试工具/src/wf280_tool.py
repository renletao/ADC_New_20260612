# -*- coding: utf-8 -*-
"""WF280 全功能测试工具 —— 入口。

覆盖固件里 WF280 的全部命令：

    整机测试   cmd[0] = 14     异步，走 IsTaskDone 队列
    轨压回读   {1, 0x211}      同步，随时可读
    OTP 校验   {11}            两个 bank 逐路比对
    输出测试   {1, 0x208 / 0x210, 测量参数, 稳定时间}
    读系数     {1, 0x209, 路数}
    读 M1      {1, 0x207, 路数}
    设定轨压   {1, 0x201, lo, hi}

用法：
    python wf280_tool.py                             图形界面
    python wf280_tool.py --port COM5 version         读固件版本
    python wf280_tool.py --port COM5 full            整机测试
    python wf280_tool.py --port COM5 rail            读轨压
    python wf280_tool.py --port COM5 otp             OTP 校验
    python wf280_tool.py --port COM5 out33          输出测试 3.3 V
    python wf280_tool.py --port COM5 out20          输出测试 2.0 V
    python wf280_tool.py --port COM5 coeff --count 4 读系数
    python wf280_tool.py --port COM5 m1 --count 4    读 M1
    python wf280_tool.py --port COM5 vdd --mv 3300   设定轨压
    python wf280_tool.py ports                       列出串口

依赖 pyserial；图形界面还需要 tkinter（Windows 官方 Python 自带）。
"""

from __future__ import annotations

import argparse
import sys

from mb_rtu import ModbusError, RtuMaster
from wf280_proto import Wf280Tester, status_text


def _print_result(label: str, result) -> None:
    print(f"== {label} ==")
    for item, value, verdict in result.rows():
        print(f"  {item:<20}{value:<34}{verdict}")
    print("结果:", "OK" if result.ok else "NG")


def cli(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="WF280 全功能测试工具（命令行模式）")
    parser.add_argument("--port", help="串口，如 COM5")
    parser.add_argument("--baud", type=int, default=115200,
                        help="波特率，默认 115200")
    parser.add_argument("--slave", type=int, default=1, help="从机地址，默认 1")
    parser.add_argument("--timeout", type=float, default=20.0,
                        help="整机测试超时秒数，默认 20")
    parser.add_argument("--count", type=int, default=4,
                        help="系数路数，默认 4")
    parser.add_argument("--meas", default="0x0000",
                        help="0xAC 测量参数，默认 0x0000")
    parser.add_argument("--settle", type=int, default=None,
                        help="测量稳定时间 ms，默认 3.3 V 用 20、2.0 V 用 200")
    parser.add_argument("--mv", type=int, default=3300,
                        help="设定轨压毫伏值，默认 3300")
    parser.add_argument("cmd", choices=[
        "version", "full", "rail", "otp", "out33", "out20", "coeff", "m1",
        "vdd", "ports",
    ], help="要执行的测试项")
    args = parser.parse_args(argv)

    if args.cmd == "ports":
        from serial.tools import list_ports
        ports = list(list_ports.comports())
        if not ports:
            print("没有检测到串口")
        for p in ports:
            print(f"{p.device}\t{p.description}")
        return 0

    if not args.port:
        parser.error("执行测试需要 --port 参数")

    try:
        measurement = int(str(args.meas), 0) & 0xFFFF
    except ValueError:
        parser.error("--meas 支持 0x0000 或十进制写法")

    try:
        with RtuMaster(args.port, baudrate=args.baud, slave=args.slave) as master:
            tester = Wf280Tester(master)

            version = tester.read_version()

            if args.cmd == "version":
                _print_result("固件版本", version)
                return 0 if version.ok else 1

            if not version.ok:
                print("警告: 读到的版本号是 0.0，设备可能没有正常应答", file=sys.stderr)

            if args.cmd == "full":
                print(f"设备固件: {version.text}")
                print("触发整机测试（cmd=14）…", flush=True)
                result = tester.run_full_test(timeout=args.timeout)
                _print_result("WF280 整机测试", result)
                print("\n结论:", "PASS" if result.all_ok else "NG")
                return 0 if result.all_ok else 1

            if args.cmd == "rail":
                result = tester.read_rail()
                _print_result("轨压回读", result)
                return 0 if result.ok else 1

            if args.cmd == "otp":
                result = tester.read_otp()
                _print_result("OTP 校验", result)
                return 0 if result.ok else 1

            if args.cmd in ("out33", "out20"):
                result = tester.test_output(
                    3300 if args.cmd == "out33" else 2000,
                    measurement, args.settle)
                _print_result(f"输出测试 {result.supply_mv} mV", result)
                return 0 if result.ok else 1

            if args.cmd == "coeff":
                result = tester.read_coeff(args.count)
                _print_result(f"读系数（{args.count} 路）", result)
                return 0 if result.ok else 1

            if args.cmd == "m1":
                result = tester.read_m1(args.count)
                _print_result(f"读 M1（{args.count} 路）", result)
                return 0 if result.ok else 1

            # vdd
            status = tester.set_supply(args.mv)
            print(f"设定轨压 {args.mv} mV → {status_text(status)}")
            return 0 if status == 0 else 1

    except (ModbusError, TimeoutError, RuntimeError, ValueError) as exc:
        print(f"错误: {exc}", file=sys.stderr)
        return 2
    except OSError as exc:
        print(f"串口打开失败: {exc}", file=sys.stderr)
        return 2


def main() -> int:
    if len(sys.argv) > 1:
        return cli(sys.argv[1:])
    try:
        import gui
    except ImportError as exc:
        print(f"无法启动图形界面：{exc}\n"
              "当前 Python 没有 tkinter，改用命令行模式，例如：\n"
              "    python wf280_tool.py --port COM5 full", file=sys.stderr)
        return 3
    return gui.run()


if __name__ == "__main__":
    sys.exit(main())
