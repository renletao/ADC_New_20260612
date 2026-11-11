# -*- coding: utf-8 -*-
"""WF280 全功能测试工具 —— 入口。

覆盖固件里 WF280 的全部命令：

    整套测试   cmd[0] = 14     异步，走 IsTaskDone 队列，76 个寄存器
    轨压回读   {1, 0x211}      同步，随时可读
    OTP 校验   {11}            两个 bank 逐路比对
    输出测试   {1, 0x208 / 0x210, 测量参数, 稳定时间}
    读系数     {1, 0x209, 路数}
    读 M1      {1, 0x207, 路数}
    设定轨压   {1, 0x201, lo, hi}

用法：
    python wf280_tool.py                             图形界面
    python wf280_tool.py --port COM5 version         读固件版本
    python wf280_tool.py --port COM5 full            整套测试
    python wf280_tool.py --port COM5 full --coeffs   整套测试并打印 32 路系数
    python wf280_tool.py --port COM5 rail            读轨压
    python wf280_tool.py --port COM5 otp             OTP 校验
    python wf280_tool.py --port COM5 out33          输出测试 3.3 V
    python wf280_tool.py --port COM5 out18          输出测试 1.8 V
    python wf280_tool.py --port COM5 coeff          3.3 V 读 NOR 32 路
    python wf280_tool.py --port COM5 m1             1.8 V 读 M1 32 路
    python wf280_tool.py --port COM5 vdd --mv 3300   设定轨压
    python wf280_tool.py ports                       列出串口

依赖 pyserial；图形界面还需要 tkinter（Windows 官方 Python 自带）。
"""

from __future__ import annotations

import argparse
import sys

from mb_rtu import ModbusError, RtuMaster
from wf280_proto import COEFF_MAX, Wf280Tester, status_text


def _print_result(label: str, result) -> None:
    print(f"== {label} ==")
    for item, value, verdict in result.rows():
        print(f"  {item:<22}{value:<44}{verdict}")
    print("结果:", "OK" if result.ok else "NG")


def _print_coeffs(result) -> None:
    """打印 M1 / NOR 两个 bank 的 32 路原始值，方便逐路对账。"""
    print("\n== M1 / NOR 系数（32 路） ==")
    print(f"  {'路号':<6}{'M1':<10}{'NOR':<10}比较")
    for index, m1, nor, same in result.coeff_pairs():
        if m1 is None:
            print(f"  {index + 1:<6}{'—':<10}{'—':<10}—")
            continue
        print(f"  {index + 1:<6}0x{m1:04X}    0x{nor:04X}    "
              f"{'一致' if same else '不一致'}")


def cli(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        description="WF280 全功能测试工具（命令行模式）")
    parser.add_argument("--port", help="串口，如 COM5")
    parser.add_argument("--baud", type=int, default=115200,
                        help="波特率，默认 115200")
    parser.add_argument("--slave", type=int, default=1, help="从机地址，默认 1")
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="整套测试超时秒数，默认 60（实测整套约 2~3 s）")
    parser.add_argument("--count", type=int, default=COEFF_MAX,
                        help=f"系数路数，默认 {COEFF_MAX}（上限 {COEFF_MAX}）")
    parser.add_argument("--meas", default="0x0000",
                        help="0xAC 测量参数，默认 0x0000")
    parser.add_argument("--settle", type=int, default=None,
                        help="测量稳定时间 ms，默认 3.3 V 用 300、1.8 V 用 400")
    parser.add_argument("--mv", type=int, default=3300,
                        help="设定轨压毫伏值，默认 3300")
    parser.add_argument("--coeffs", action="store_true",
                        help="整套测试后打印 M1/NOR 32 路原始值")
    parser.add_argument("cmd", choices=[
        "version", "full", "rail", "otp", "out33", "out18", "out20", "coeff",
        "m1", "vdd", "ports",
    ], help="要执行的测试项（full = 整套测试 cmd[0]=14）")
    args = parser.parse_args(argv)

    if args.cmd == "ports":
        from serial.tools import list_ports
        ports = list(list_ports.comports())

        def score(port) -> int:
            text = f"{port.device} {port.description}".lower()
            if "jlink" in text or "segger" in text:
                return -10
            if "ch340" in text or "usb-serial" in text or "cp210" in text \
                    or "ft232" in text or "usb serial" in text:
                return 10
            if "蓝牙" in text or "bluetooth" in text:
                return -5
            return 0

        ports.sort(key=score, reverse=True)
        if not ports:
            print("没有检测到串口")
        for i, p in enumerate(ports):
            hint = "\t<= 大概率是板子" if i == 0 and score(p) > 0 else ""
            print(f"{p.device}\t{p.description}{hint}")
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
            cmd_addr, in_addr = tester.detect_bases()
            print(f"寄存器基址: 命令@{cmd_addr} 结果@{in_addr}")

            version = tester.read_version()

            if args.cmd == "version":
                _print_result("固件版本", version)
                return 0 if version.ok else 1

            if not version.ok:
                print("警告: 读到的版本号是 0.0，设备可能没有正常应答", file=sys.stderr)

            if args.cmd == "full":
                print(f"设备固件: {version.text}")
                print("触发整套测试（cmd=14）…", flush=True)
                tty = sys.stdout.isatty()

                def tick(elapsed):
                    if tty:      # 重定向到文件/管道时不要留一堆回车符
                        print(f"  测试中… {elapsed:.1f} s", end="\r", flush=True)

                result = tester.run_full_test(timeout=args.timeout, on_tick=tick)
                if tty:
                    print(" " * 32, end="\r")
                _print_result("WF280 整套测试", result)
                if args.coeffs:
                    _print_coeffs(result)
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

            if args.cmd in ("out33", "out18", "out20"):
                # out20 是旧名字：固件 0x210 实际输出 1.8 V
                supply = 3300 if args.cmd == "out33" else 1800
                result = tester.test_output(supply, measurement, args.settle)
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
