# -*- coding: utf-8 -*-
"""图形界面（tkinter）。由 wf280_tool.py 在无参数时调用。

界面依赖系统自带的 tkinter，命令行模式不需要它。
"""

from __future__ import annotations

import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

from mb_rtu import RtuMaster
from wf280_proto import (
    COEFF_MAX,
    FullTestResult,
    RailReading,
    Wf280Tester,
    status_text,
)

BAUD_RATES = ["115200", "57600", "38400", "19200", "9600"]
BG = "#f5f6f8"
CARD = "#ffffff"
TEXT = "#1f2328"
MUTED = "#6b7280"
ACCENT = "#1f6feb"
OK_COLOR = "#1a7f37"
NG_COLOR = "#cf222e"


class TestApp:
    def __init__(self, root: "tk.Tk"):
        self.root = root
        self.master: RtuMaster | None = None
        self.tester: Wf280Tester | None = None
        self.busy = False
        self.auto_rail = tk.BooleanVar(value=False)
        self.version_text = "未知"
        self.device_info = ""
        self._queue: queue.Queue = queue.Queue()

        self._build_style()
        self._build_ui()
        self._refresh_ports()
        self._drain_queue()
        self._poll_auto_rail()

    # ---------- 样式 ----------

    def _build_style(self) -> None:
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        base = ("Microsoft YaHei UI", 10)
        style.configure(".", font=base, background=BG, foreground=TEXT)
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=TEXT)
        style.configure("Card.TFrame", background=CARD)
        style.configure("Card.TLabel", background=CARD, foreground=TEXT)
        style.configure("Card.TCheckbutton", background=CARD, foreground=TEXT)
        style.configure("Muted.TLabel", background=CARD, foreground=MUTED,
                        font=("Microsoft YaHei UI", 9))
        style.configure("Head.TLabel", background=CARD, foreground=TEXT,
                        font=("Microsoft YaHei UI", 11, "bold"))
        style.configure("TButton", padding=(12, 6))
        style.configure("Treeview", background=CARD, fieldbackground=CARD,
                        foreground=TEXT, rowheight=26, borderwidth=0)
        style.configure("Treeview.Heading",
                        font=("Microsoft YaHei UI", 9, "bold"),
                        background="#eceef1", foreground=TEXT)
        style.map("Treeview", background=[("selected", "#dbeafe")],
                  foreground=[("selected", TEXT)])

    # ---------- 布局 ----------

    def _build_ui(self) -> None:
        self.root.title("WF280 全功能测试工具")
        self.root.configure(bg=BG)
        self.root.geometry("920x840")
        self.root.minsize(840, 720)

        outer = ttk.Frame(self.root, padding=14)
        outer.pack(fill="both", expand=True)
        self._build_connection_bar(outer)
        self.info_label = ttk.Label(outer, text="未连接设备",
                                    foreground=MUTED)
        self.info_label.pack(fill="x", pady=(0, 10))
        self._build_full_test_card(outer)
        self._build_rail_card(outer)
        self._build_cmd_card(outer)
        self._build_log(outer)

    def _build_connection_bar(self, parent) -> None:
        bar = ttk.Frame(parent)
        bar.pack(fill="x", pady=(0, 12))

        ttk.Label(bar, text="串口").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(bar, textvariable=self.port_var, width=12,
                                     state="readonly")
        self.port_box.pack(side="left", padx=(6, 6))
        ttk.Button(bar, text="刷新", width=6,
                   command=self._refresh_ports).pack(side="left")

        ttk.Label(bar, text="波特率").pack(side="left", padx=(16, 4))
        self.baud_var = tk.StringVar(value="115200")
        ttk.Combobox(bar, textvariable=self.baud_var, values=BAUD_RATES,
                     width=8, state="readonly").pack(side="left")

        ttk.Label(bar, text="从机").pack(side="left", padx=(16, 4))
        self.slave_var = tk.StringVar(value="1")
        ttk.Spinbox(bar, from_=1, to=247, width=5,
                    textvariable=self.slave_var).pack(side="left")

        self.connect_btn = ttk.Button(bar, text="连接", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=(16, 8))
        self.version_btn = ttk.Button(bar, text="读版本", width=8,
                                      command=self._on_read_version, state="disabled")
        self.version_btn.pack(side="left", padx=(0, 8))
        self.conn_label = ttk.Label(bar, text="未连接", foreground=MUTED)
        self.conn_label.pack(side="left")

    def _card(self, parent, title: str, hint: str) -> ttk.Frame:
        frame = ttk.Frame(parent, style="Card.TFrame", padding=14)
        frame.pack(fill="x", pady=(0, 12))
        head = ttk.Frame(frame, style="Card.TFrame")
        head.pack(fill="x")
        ttk.Label(head, text=title, style="Head.TLabel").pack(side="left")
        ttk.Label(head, text=hint, style="Muted.TLabel").pack(side="left", padx=(10, 0))
        return frame

    def _build_full_test_card(self, parent) -> None:
        card = self._card(parent, "① WF280 整机测试",
                          "cmd[0] = 14    异步执行，走 IsTaskDone 队列")
        row = ttk.Frame(card, style="Card.TFrame")
        row.pack(fill="x", pady=(10, 8))
        self.full_btn = ttk.Button(row, text="开始整机测试",
                                   command=self._on_full_test)
        self.full_btn.pack(side="left")
        self.full_status = ttk.Label(row, text="待测", style="Card.TLabel")
        self.full_status.pack(side="left", padx=(14, 0))

        cols = ("item", "value", "verdict")
        self.full_tree = ttk.Treeview(card, columns=cols, show="headings", height=6)
        for col, text, width in (("item", "项目", 190), ("value", "数值", 270),
                                 ("verdict", "判定", 170)):
            self.full_tree.heading(col, text=text)
            self.full_tree.column(col, width=width, anchor="w")
        self.full_tree.pack(fill="x")
        self.full_tree.tag_configure("ok", foreground=OK_COLOR)
        self.full_tree.tag_configure("ng", foreground=NG_COLOR)

    def _build_rail_card(self, parent) -> None:
        card = self._card(parent, "② 轨压回读",
                          "{1, 0x211}    同步，随时可读，不改变供电状态")
        row = ttk.Frame(card, style="Card.TFrame")
        row.pack(fill="x", pady=(12, 4))
        self.rail_btn = ttk.Button(row, text="读取轨压", command=self._on_read_rail)
        self.rail_btn.pack(side="left")
        ttk.Checkbutton(row, text="连续监测（0.5 s）", variable=self.auto_rail,
                        style="Card.TCheckbutton").pack(side="left", padx=(14, 0))

        self.rail_value = tk.Label(card, text="---- mV", bg=CARD, fg=MUTED,
                                   font=("Consolas", 30, "bold"))
        self.rail_value.pack(anchor="w", pady=(8, 0))
        self.rail_verdict = ttk.Label(card, text="等待读取", style="Muted.TLabel")
        self.rail_verdict.pack(anchor="w")

    def _build_cmd_card(self, parent) -> None:
        card = self._card(parent, "③ 单项命令",
                          "0x207 读 M1 / 0x208 3.3 V / 0x209 系数 / 0x210 2.0 V / 11 OTP")

        row = ttk.Frame(card, style="Card.TFrame")
        row.pack(fill="x", pady=(10, 6))
        ttk.Label(row, text="测量参数", style="Card.TLabel").pack(side="left")
        self.meas_var = tk.StringVar(value="0x0000")
        ttk.Entry(row, textvariable=self.meas_var, width=9).pack(side="left", padx=(4, 12))
        ttk.Label(row, text="稳定时间", style="Card.TLabel").pack(side="left")
        self.settle_var = tk.StringVar(value="300")
        ttk.Entry(row, textvariable=self.settle_var, width=6).pack(side="left", padx=(4, 2))
        ttk.Label(row, text="ms", style="Muted.TLabel").pack(side="left", padx=(0, 12))
        ttk.Label(row, text="系数路数", style="Card.TLabel").pack(side="left")
        self.count_var = tk.StringVar(value="4")
        ttk.Entry(row, textvariable=self.count_var, width=4).pack(side="left", padx=(4, 0))

        btns = ttk.Frame(card, style="Card.TFrame")
        btns.pack(fill="x", pady=(0, 8))
        for text, action in (("OTP 校验", "otp"), ("输出 3.3 V", "out33"),
                             ("输出 2.0 V", "out20"), ("读系数", "coeff"),
                             ("读 M1 系数", "m1")):
            ttk.Button(btns, text=text,
                       command=lambda a=action: self._on_cmd(a)).pack(
                side="left", padx=(0, 8))

        setrow = ttk.Frame(card, style="Card.TFrame")
        setrow.pack(fill="x", pady=(0, 8))
        ttk.Label(setrow, text="辅助：设定 DAC 轨压",
                  style="Card.TLabel").pack(side="left")
        self.vdd_var = tk.StringVar(value="3300")
        ttk.Entry(setrow, textvariable=self.vdd_var, width=7).pack(side="left",
                                                                  padx=(6, 4))
        ttk.Label(setrow, text="mV", style="Muted.TLabel").pack(side="left", padx=(0, 8))
        ttk.Button(setrow, text="设定", width=6,
                   command=self._on_set_supply).pack(side="left")

        cols = ("item", "value", "verdict")
        self.cmd_tree = ttk.Treeview(card, columns=cols, show="headings", height=6)
        for col, text, width in (("item", "项目", 190), ("value", "数值", 270),
                                 ("verdict", "判定", 170)):
            self.cmd_tree.heading(col, text=text)
            self.cmd_tree.column(col, width=width, anchor="w")
        self.cmd_tree.pack(fill="x")
        self.cmd_tree.tag_configure("ok", foreground=OK_COLOR)
        self.cmd_tree.tag_configure("ng", foreground=NG_COLOR)

    def _build_log(self, parent) -> None:
        frame = ttk.Frame(parent)
        frame.pack(fill="both", expand=True)
        head = ttk.Frame(frame)
        head.pack(fill="x")
        ttk.Label(head, text="运行日志").pack(side="left")
        ttk.Button(head, text="清空", width=6,
                   command=self._clear_log).pack(side="right")

        wrap = ttk.Frame(frame)
        wrap.pack(fill="both", expand=True, pady=(6, 0))
        self.log = tk.Text(wrap, height=6, bg="#ffffff", fg=TEXT, relief="flat",
                           font=("Consolas", 9), wrap="word",
                           padx=10, pady=8, state="disabled")
        scroll = ttk.Scrollbar(wrap, orient="vertical", command=self.log.yview)
        self.log.configure(yscrollcommand=scroll.set)
        self.log.pack(side="left", fill="both", expand=True)
        scroll.pack(side="right", fill="y")
        self.log.tag_configure("err", foreground=NG_COLOR)
        self.log.tag_configure("ok", foreground=OK_COLOR)
        self.log.tag_configure("muted", foreground=MUTED)

    # ---------- 日志 ----------

    def _log(self, message: str, tag: str = "") -> None:
        stamp = time.strftime("%H:%M:%S")
        self.log.configure(state="normal")
        self.log.insert("end", f"[{stamp}] {message}\n", tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self) -> None:
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    # ---------- 串口 ----------

    def _refresh_ports(self) -> None:
        try:
            from serial.tools import list_ports
            ports = [p.device for p in list_ports.comports()]
        except Exception:
            ports = []
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])
        if not ports:
            self.port_var.set("")
            self._log("没有检测到串口，插好 USB 转串口后点「刷新」", "muted")

    def _toggle_connect(self) -> None:
        if self.master is not None:
            self._disconnect()
            return
        port = self.port_var.get().strip()
        if not port:
            self._log("请先选择串口", "err")
            return
        try:
            baud = int(self.baud_var.get())
            slave = int(self.slave_var.get())
            self.master = RtuMaster(port, baudrate=baud, slave=slave).open()
        except Exception as exc:  # noqa: BLE001
            self._log(f"打开串口失败：{exc}", "err")
            return
        self.tester = Wf280Tester(self.master)
        self.connect_btn.configure(text="断开")
        self.version_btn.configure(state="normal")
        self.conn_label.configure(text=f"已连接 {port} @ {baud}",
                                  foreground=OK_COLOR)
        self.device_info = f"{port} · {baud} 8N1 · 从机 {slave}"
        self.info_label.configure(text=f"{self.device_info} · 正在读取版本…",
                                  foreground=MUTED)
        self._log(f"已连接 {port}，{baud} 8N1，从机 {slave}", "ok")

        def work():
            bases = self.tester.detect_bases()
            return bases, self.tester.read_version(), self.tester.read_rail()

        def done(result, error):
            if error:
                self._log(f"通讯自检失败：{error}", "err")
                self.info_label.configure(
                    text=f"{self.device_info} · 通讯失败", foreground=NG_COLOR)
                return
            bases, version, rail = result
            self.version_text = version.text if version.ok else "未知"
            self._update_info()
            self._log(f"寄存器基址 命令@{bases[0]} / 结果@{bases[1]}", "muted")
            if version.ok:
                self._log(f"设备固件 {version.text}，当前轨压 {rail.mv} mV", "ok")
            else:
                self._log("读取到的版本号是 0.0，设备可能没有正常应答", "err")

        self._run_async(work, done)

    def _update_info(self) -> None:
        self.info_label.configure(
            text=f"设备：固件 {self.version_text} · {self.device_info}",
            foreground=TEXT)

    def _on_read_version(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        self._run_async(self.tester.read_version, self._show_version)

    def _show_version(self, version, error) -> None:
        if error:
            self._log(f"版本读取失败：{error}", "err")
            return
        self.version_text = version.text if version.ok else "未知"
        self._update_info()
        if version.ok:
            self._log(f"设备固件版本 {version.text}", "ok")
        else:
            self._log("读取到的版本号是 0.0，设备可能没有正常应答", "err")

    def _disconnect(self) -> None:
        if self.master is not None:
            self.master.close()
        self.master = None
        self.tester = None
        self.version_text = "未知"
        self.device_info = ""
        self.auto_rail.set(False)
        self.connect_btn.configure(text="连接")
        self.version_btn.configure(state="disabled")
        self.conn_label.configure(text="未连接", foreground=MUTED)
        self.info_label.configure(text="未连接设备", foreground=MUTED)
        self._log("已断开串口")

    # ---------- 异步执行 ----------

    def _run_async(self, work, done) -> None:
        """把阻塞的串口操作丢到线程里，结果回到 UI 线程。"""
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        if self.busy:
            return  # 自动连续监测会频繁撞上，静默跳过
        self.busy = True

        def runner():
            try:
                self._queue.put(("done", done, (work(), None)))
            except Exception as exc:  # noqa: BLE001
                self._queue.put(("done", done, (None, exc)))

        threading.Thread(target=runner, daemon=True).start()

    def _post_tick(self, elapsed: float) -> None:
        self._queue.put(("tick", elapsed, None))

    def _drain_queue(self) -> None:
        try:
            while True:
                kind, payload, args = self._queue.get_nowait()
                if kind == "tick":
                    self.full_status.configure(
                        text=f"测试中… {payload:.1f} s", foreground=ACCENT)
                    continue
                self.busy = False
                try:
                    payload(*args)
                except Exception as exc:  # noqa: BLE001
                    self._log(f"界面回调异常：{exc}", "err")
        except queue.Empty:
            pass
        self.root.after(50, self._drain_queue)

    # ---------- 整机测试 ----------

    def _on_full_test(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        self.auto_rail.set(False)
        self.full_btn.configure(state="disabled")
        self.full_status.configure(text="测试中…", foreground=ACCENT)
        for item in self.full_tree.get_children():
            self.full_tree.delete(item)
        self._log(f"触发整机测试（cmd=14），设备固件 {self.version_text}，"
                  f"等待完成…")

        def work():
            return self.tester.run_full_test(on_tick=self._post_tick)

        def done(result: FullTestResult | None, error):
            self.full_btn.configure(state="normal")
            if error:
                self.full_status.configure(text="失败", foreground=NG_COLOR)
                self._log(f"整机测试失败：{error}", "err")
                return
            self._show_full_result(result)

        self._run_async(work, done)

    def _show_full_result(self, result: FullTestResult) -> None:
        self._show_rows(self.full_tree, result.rows())
        if result.all_ok:
            self.full_status.configure(text="PASS", foreground=OK_COLOR)
            self._log(f"整机测试完成：PASS（固件 {self.version_text}），"
                      f"轨压 {result.rail_mv} mV，"
                      f"工作 {result.work_ua} uA / 休眠 {result.sleep_ua} uA", "ok")
        else:
            self.full_status.configure(
                text=f"NG（{status_text(result.status)}）", foreground=NG_COLOR)
            self._log(f"整机测试完成：NG（固件 {self.version_text}），"
                      f"{status_text(result.status)}，轨压 {result.rail_mv} mV", "err")

    # ---------- 轨压 ----------

    def _on_read_rail(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        self.rail_btn.configure(state="disabled")
        self._run_async(self.tester.read_rail, self._show_rail)

    def _show_rail(self, result: RailReading | None, error) -> None:
        self.rail_btn.configure(state="normal")
        if error:
            self.rail_value.configure(text="---- mV", fg=MUTED)
            self.rail_verdict.configure(text=f"读取失败：{error}")
            self._log(f"轨压读取失败：{error}", "err")
            return
        self.rail_value.configure(text=f"{result.mv} mV",
                                  fg=OK_COLOR if result.ok else NG_COLOR)
        self.rail_verdict.configure(text=result.verdict)

    # ---------- 单项命令 ----------

    def _read_params(self):
        """读取参数输入框，失败返回 None。"""
        try:
            measurement = int(self.meas_var.get().strip(), 0)
            settle = int(self.settle_var.get().strip())
            count = int(self.count_var.get().strip())
        except ValueError:
            self._log("参数填写有误：测量参数可写 0x0000 或 0，其余填十进制", "err")
            return None
        if not 0 <= measurement <= 0xFFFF:
            self._log("测量参数范围 0x0000 ~ 0xFFFF", "err")
            return None
        if not 0 <= settle <= 5000:
            self._log("稳定时间范围 0 ~ 5000 ms", "err")
            return None
        if not 1 <= count <= COEFF_MAX:
            self._log(f"系数路数范围 1 ~ {COEFF_MAX}", "err")
            return None
        return measurement, settle, count

    def _on_cmd(self, action: str) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        params = self._read_params()
        if params is None:
            return
        measurement, settle, count = params

        jobs = {
            "otp": ("OTP 校验", lambda: self.tester.read_otp()),
            "out33": (f"输出测试 3.3 V（参数 0x{measurement:04X}，{settle} ms）",
                      lambda: self.tester.test_output_3v3(measurement, settle)),
            "out20": (f"输出测试 2.0 V（参数 0x{measurement:04X}，{settle} ms）",
                      lambda: self.tester.test_output_2v0(measurement, settle)),
            "coeff": (f"读正常 bank 系数（{count} 路）",
                      lambda: self.tester.read_coeff(count)),
            "m1": (f"读 M1 系数（{count} 路）", lambda: self.tester.read_m1(count)),
        }
        label, work = jobs[action]

        self.auto_rail.set(False)
        for item in self.cmd_tree.get_children():
            self.cmd_tree.delete(item)
        self._log(f"执行：{label}")
        self._run_async(work, lambda result, error: self._show_cmd(label, result, error))

    def _show_cmd(self, label: str, result, error) -> None:
        if error:
            self._log(f"{label} 失败：{error}", "err")
            return
        self._show_rows(self.cmd_tree, result.rows())
        if result.ok:
            self._log(f"{label} 完成：OK", "ok")
        else:
            first = result.rows()[0][2] if result.rows() else "NG"
            self._log(f"{label} 完成：NG（{first}）", "err")

    def _show_rows(self, tree: ttk.Treeview, rows) -> None:
        for item, value, verdict in rows:
            if verdict == "OK":
                tag = "ok"
            elif verdict.startswith("NG") or "未" in verdict or "偏离" in verdict:
                tag = "ng"
            else:
                tag = ""
            tree.insert("", "end", values=(item, value, verdict), tags=(tag,))

    def _on_set_supply(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        try:
            mv = int(self.vdd_var.get().strip())
        except ValueError:
            self._log("轨压请填十进制毫伏值，例如 3300", "err")
            return
        self.auto_rail.set(False)
        self._log(f"设定 DAC 轨压 {mv} mV")
        self._run_async(lambda: self.tester.set_supply(mv),
                        self._show_set_supply)

    def _show_set_supply(self, status: int | None, error) -> None:
        if error:
            self._log(f"设定轨压失败：{error}", "err")
            return
        if status == 0:
            self._log("设定轨压完成", "ok")
        else:
            self._log(f"设定轨压失败：{status_text(status)}", "err")

    def _poll_auto_rail(self) -> None:
        if self.auto_rail.get() and self.tester is not None and not self.busy:
            self._on_read_rail()
        self.root.after(500, self._poll_auto_rail)


def run() -> int:
    root = tk.Tk()
    TestApp(root)
    root.mainloop()
    return 0
