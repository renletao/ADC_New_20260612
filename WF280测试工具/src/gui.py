# -*- coding: utf-8 -*-
"""图形界面（tkinter）。由 wf280_tool.py 在无参数时调用。

界面按产线实际流程组织：
    手动设压 → 回读确认 → 跑各项 → 复测实际轨压
一键「全套测试」把这几步串起来，单项命令留在下面备用。
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
    RailReading,
    Wf280Tester,
    run_suite,
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

SUITE_STEPS = 8


class _Simple:
    """把只有状态码的命令包装成能进表格的结果。"""

    def __init__(self, status: int):
        self.status = status

    @property
    def ok(self) -> bool:
        return self.status == 0

    def rows(self):
        return [("状态", status_text(self.status), "OK" if self.ok else "NG")]


class TestApp:
    def __init__(self, root: "tk.Tk"):
        self.root = root
        self.master: RtuMaster | None = None
        self.tester: Wf280Tester | None = None
        self.busy = False
        self.version_text = "未知"
        self.device_info = ""
        self._queue: queue.Queue = queue.Queue()

        self._build_style()
        self._build_ui()
        self._refresh_ports()
        self._drain_queue()

    # ---------- 样式 ----------

    def _build_style(self) -> None:
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure(".", font=("Microsoft YaHei UI", 10),
                        background=BG, foreground=TEXT)
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=TEXT)
        style.configure("Card.TFrame", background=CARD)
        style.configure("Card.TLabel", background=CARD, foreground=TEXT)
        style.configure("Muted.TLabel", background=CARD, foreground=MUTED,
                        font=("Microsoft YaHei UI", 9))
        style.configure("Head.TLabel", background=CARD, foreground=TEXT,
                        font=("Microsoft YaHei UI", 11, "bold"))
        style.configure("TButton", padding=(12, 6))
        style.configure("Big.TButton", padding=(16, 8),
                        font=("Microsoft YaHei UI", 10, "bold"))
        style.configure("Treeview", background=CARD, fieldbackground=CARD,
                        foreground=TEXT, rowheight=25, borderwidth=0)
        style.configure("Treeview.Heading",
                        font=("Microsoft YaHei UI", 9, "bold"),
                        background="#eceef1", foreground=TEXT)
        style.map("Treeview", background=[("selected", "#dbeafe")],
                  foreground=[("selected", TEXT)])

    # ---------- 布局 ----------

    def _build_ui(self) -> None:
        self.root.title("WF280 全功能测试工具")
        self.root.configure(bg=BG)
        self.root.geometry("940x880")
        self.root.minsize(880, 780)

        outer = ttk.Frame(self.root, padding=12)
        outer.pack(fill="both", expand=True)
        self._build_connection_bar(outer)
        self.info_label = ttk.Label(outer, text="未连接设备", foreground=MUTED)
        self.info_label.pack(fill="x", pady=(0, 8))
        self._build_suite_card(outer)
        self._build_item_card(outer)
        self._build_log(outer)

    def _build_connection_bar(self, parent) -> None:
        bar = ttk.Frame(parent)
        bar.pack(fill="x", pady=(0, 6))

        ttk.Label(bar, text="串口").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(bar, textvariable=self.port_var, width=11,
                                     state="readonly")
        self.port_box.pack(side="left", padx=(4, 4))
        ttk.Button(bar, text="刷新", width=5,
                   command=self._refresh_ports).pack(side="left")

        ttk.Label(bar, text="波特率").pack(side="left", padx=(12, 4))
        self.baud_var = tk.StringVar(value="115200")
        ttk.Combobox(bar, textvariable=self.baud_var, values=BAUD_RATES,
                     width=7, state="readonly").pack(side="left")

        ttk.Label(bar, text="从机").pack(side="left", padx=(12, 4))
        self.slave_var = tk.StringVar(value="1")
        ttk.Spinbox(bar, from_=1, to=247, width=4,
                    textvariable=self.slave_var).pack(side="left")

        self.connect_btn = ttk.Button(bar, text="连接", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=(12, 6))
        self.version_btn = ttk.Button(bar, text="读版本", width=7,
                                      command=self._on_read_version, state="disabled")
        self.version_btn.pack(side="left")
        self.conn_label = ttk.Label(bar, text="未连接", foreground=MUTED)
        self.conn_label.pack(side="left", padx=(10, 0))

    def _card(self, parent, title: str, hint: str = "") -> ttk.Frame:
        frame = ttk.Frame(parent, style="Card.TFrame", padding=12)
        frame.pack(fill="x", pady=(0, 8))
        head = ttk.Frame(frame, style="Card.TFrame")
        head.pack(fill="x")
        ttk.Label(head, text=title, style="Head.TLabel").pack(side="left")
        if hint:
            ttk.Label(head, text=hint, style="Muted.TLabel").pack(side="left",
                                                                  padx=(10, 0))
        return frame

    def _tree(self, parent, height: int) -> ttk.Treeview:
        cols = ("item", "value", "verdict")
        tree = ttk.Treeview(parent, columns=cols, show="headings", height=height)
        for col, text, width in (("item", "项目", 210), ("value", "数值", 300),
                                 ("verdict", "判定", 180)):
            tree.heading(col, text=text)
            tree.column(col, width=width, anchor="w")
        tree.tag_configure("ok", foreground=OK_COLOR)
        tree.tag_configure("ng", foreground=NG_COLOR)
        tree.tag_configure("step", foreground=TEXT,
                           font=("Microsoft YaHei UI", 9, "bold"))
        return tree

    def _build_suite_card(self, parent) -> None:
        card = self._card(parent, "全套测试",
                          "设压 → 回读 → 整机 → 系数 → M1 → OTP → 输出 → 复测")

        row = ttk.Frame(card, style="Card.TFrame")
        row.pack(fill="x", pady=(8, 6))
        ttk.Label(row, text="设定电压", style="Card.TLabel").pack(side="left")
        self.voltage_var = tk.StringVar(value="3300")
        ttk.Entry(row, textvariable=self.voltage_var, width=7).pack(
            side="left", padx=(4, 2))
        ttk.Label(row, text="mV", style="Muted.TLabel").pack(side="left",
                                                             padx=(0, 12))

        self.suite_btn = ttk.Button(row, text="▶  开始全套测试", style="Big.TButton",
                                    command=self._on_suite)
        self.suite_btn.pack(side="left")

        ttk.Button(row, text="读取实际轨压",
                   command=self._on_read_rail).pack(side="left", padx=(8, 12))

        self.suite_status = ttk.Label(row, text="就绪", style="Card.TLabel")
        self.suite_status.pack(side="left")

        rail_row = ttk.Frame(card, style="Card.TFrame")
        rail_row.pack(fill="x", pady=(2, 6))
        ttk.Label(rail_row, text="实际轨压", style="Card.TLabel").pack(side="left")
        self.rail_value = tk.Label(rail_row, text="----", bg=CARD, fg=MUTED,
                                   font=("Consolas", 22, "bold"))
        self.rail_value.pack(side="left", padx=(10, 10))
        self.rail_verdict = ttk.Label(rail_row, text="尚未读取", style="Muted.TLabel")
        self.rail_verdict.pack(side="left")

        self.suite_tree = self._tree(card, 7)
        self.suite_tree.pack(fill="x")

        foot = ttk.Frame(card, style="Card.TFrame")
        foot.pack(fill="x", pady=(6, 0))
        self.verdict_label = ttk.Label(foot, text="总判定：—", style="Card.TLabel",
                                       font=("Microsoft YaHei UI", 11, "bold"))
        self.verdict_label.pack(side="left")

    def _build_item_card(self, parent) -> None:
        card = self._card(parent, "单项命令", "需要单独测某一项时用")

        row = ttk.Frame(card, style="Card.TFrame")
        row.pack(fill="x", pady=(8, 6))
        ttk.Label(row, text="测量参数", style="Card.TLabel").pack(side="left")
        self.meas_var = tk.StringVar(value="0x0000")
        ttk.Entry(row, textvariable=self.meas_var, width=8).pack(side="left",
                                                                padx=(4, 10))
        ttk.Label(row, text="稳定时间", style="Card.TLabel").pack(side="left")
        self.settle_var = tk.StringVar(value="300")
        ttk.Entry(row, textvariable=self.settle_var, width=5).pack(side="left",
                                                                   padx=(4, 2))
        ttk.Label(row, text="ms", style="Muted.TLabel").pack(side="left",
                                                             padx=(0, 10))
        ttk.Label(row, text="系数路数", style="Card.TLabel").pack(side="left")
        self.count_var = tk.StringVar(value="4")
        ttk.Entry(row, textvariable=self.count_var, width=4).pack(side="left",
                                                                  padx=(4, 0))

        btns = ttk.Frame(card, style="Card.TFrame")
        btns.pack(fill="x", pady=(0, 6))
        for text, action in (("设定电压", "vdd"), ("OTP 校验", "otp"),
                             ("输出 3.3 V", "out33"), ("输出 2.0 V", "out20"),
                             ("读系数", "coeff"), ("读 M1", "m1"),
                             ("整机测试", "full")):
            ttk.Button(btns, text=text,
                       command=lambda a=action: self._on_cmd(a)).pack(
                side="left", padx=(0, 6))

        self.item_tree = self._tree(card, 4)
        self.item_tree.pack(fill="x")

    def _build_log(self, parent) -> None:
        frame = ttk.Frame(parent)
        frame.pack(fill="both", expand=True)
        head = ttk.Frame(frame)
        head.pack(fill="x")
        ttk.Label(head, text="运行日志").pack(side="left")
        ttk.Button(head, text="清空", width=6,
                   command=self._clear_log).pack(side="right")

        wrap = ttk.Frame(frame)
        wrap.pack(fill="both", expand=True, pady=(4, 0))
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
            self._show_rail(rail)

        self._run_async(work, done)

    def _update_info(self) -> None:
        self.info_label.configure(
            text=f"设备：固件 {self.version_text} · {self.device_info}",
            foreground=TEXT)

    def _disconnect(self) -> None:
        if self.master is not None:
            self.master.close()
        self.master = None
        self.tester = None
        self.version_text = "未知"
        self.device_info = ""
        self.connect_btn.configure(text="连接")
        self.version_btn.configure(state="disabled")
        self.conn_label.configure(text="未连接", foreground=MUTED)
        self.info_label.configure(text="未连接设备", foreground=MUTED)
        self._log("已断开串口")

    # ---------- 异步执行 ----------

    def _run_async(self, work, done) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        if self.busy:
            return
        self.busy = True

        def runner():
            try:
                self._queue.put(("done", done, (work(), None)))
            except Exception as exc:  # noqa: BLE001
                self._queue.put(("done", done, (None, exc)))

        threading.Thread(target=runner, daemon=True).start()

    def _post_status(self, text: str) -> None:
        self._queue.put(("status", text, None))

    def _post_rail(self, reading) -> None:
        self._queue.put(("rail", reading, None))

    def _drain_queue(self) -> None:
        try:
            while True:
                kind, payload, args = self._queue.get_nowait()
                if kind == "status":
                    self.suite_status.configure(text=payload, foreground=ACCENT)
                    continue
                if kind == "rail":
                    self._show_rail(payload)
                    continue
                self.busy = False
                try:
                    payload(*args)
                except Exception as exc:  # noqa: BLE001
                    self._log(f"界面回调异常：{exc}", "err")
        except queue.Empty:
            pass
        self.root.after(50, self._drain_queue)

    # ---------- 表格 ----------

    def _show_rows(self, tree: ttk.Treeview, rows) -> None:
        for item, value, verdict in rows:
            if verdict.startswith("【"):
                tag = "step"
            elif verdict == "OK":
                tag = "ok"
            elif verdict.startswith("NG") or "未" in verdict or "偏离" in verdict:
                tag = "ng"
            else:
                tag = ""
            tree.insert("", "end", values=(item, value, verdict), tags=(tag,))

    # ---------- 取输入 ----------

    def _read_params(self):
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

    def _voltage(self):
        try:
            mv = int(self.voltage_var.get().strip())
        except ValueError:
            self._log("设定电压请填十进制毫伏值，例如 3300", "err")
            return None
        if not 500 <= mv <= 5000:
            self._log("设定电压范围 500 ~ 5000 mV", "err")
            return None
        return mv

    # ---------- 全套测试 ----------

    def _on_suite(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        mv = self._voltage()
        if mv is None:
            return

        self.suite_btn.configure(state="disabled")
        for item in self.suite_tree.get_children():
            self.suite_tree.delete(item)
        self.verdict_label.configure(text="总判定：测试中…", foreground=ACCENT)
        self._log(f"开始全套测试，设定电压 {mv} mV")

        tester = self.tester

        def step_cb(n: int, name: str) -> None:
            self._post_status(f"第 {n}/{SUITE_STEPS} 步：{name}")

        def tick_cb(elapsed: float) -> None:
            self._post_status(f"第 3/{SUITE_STEPS} 步：整机测试运行中 {elapsed:.0f} s")

        def work():
            return run_suite(tester, mv, on_step=step_cb, on_rail=self._post_rail,
                             on_tick=tick_cb)

        def done(result, error):
            self.suite_btn.configure(state="normal")
            self.suite_status.configure(text="就绪", foreground=MUTED)
            if error:
                self.verdict_label.configure(text="总判定：测试中断",
                                             foreground=NG_COLOR)
                self._log(f"全套测试中断：{error}", "err")
                return
            rows, ok = result
            self._show_rows(self.suite_tree, rows)
            if ok:
                self.verdict_label.configure(text="总判定：PASS",
                                             foreground=OK_COLOR)
                self._log("全套测试完成：PASS", "ok")
            else:
                self.verdict_label.configure(text="总判定：NG",
                                             foreground=NG_COLOR)
                self._log("全套测试完成：NG，看表格里标红的项", "err")

        self._run_async(work, done)

    # ---------- 轨压 ----------

    def _on_read_rail(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        self._run_async(self.tester.read_rail, self._show_rail)

    def _show_rail(self, result: RailReading | None, error=None) -> None:
        if error:
            self.rail_value.configure(text="----", fg=MUTED)
            self.rail_verdict.configure(text=f"读取失败：{error}")
            self._log(f"轨压读取失败：{error}", "err")
            return
        self.rail_value.configure(text=f"{result.mv} mV",
                                  fg=OK_COLOR if result.ok else NG_COLOR)
        self.rail_verdict.configure(text=result.verdict)

    # ---------- 单项命令 ----------

    def _on_cmd(self, action: str) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        params = self._read_params()
        if params is None:
            return
        measurement, settle, count = params

        if action == "full":
            label = "整机测试"

            def work():
                return self.tester.run_full_test(
                    on_tick=lambda e: self._post_status(f"整机测试运行中 {e:.0f} s"))
        elif action == "vdd":
            mv = self._voltage()
            if mv is None:
                return
            label = f"设定电压 {mv} mV"

            def work():
                return _Simple(self.tester.set_supply(mv))
        else:
            jobs = {
                "otp": ("OTP 校验", lambda: self.tester.read_otp()),
                "out33": (f"输出 3.3 V（{measurement:#06x} / {settle} ms）",
                          lambda: self.tester.test_output_3v3(measurement, settle)),
                "out20": (f"输出 2.0 V（{measurement:#06x} / {settle} ms）",
                          lambda: self.tester.test_output_2v0(measurement, settle)),
                "coeff": (f"读系数（{count} 路）",
                          lambda: self.tester.read_coeff(count)),
                "m1": (f"读 M1（{count} 路）", lambda: self.tester.read_m1(count)),
            }
            label, work = jobs[action]

        for item in self.item_tree.get_children():
            self.item_tree.delete(item)
        self._log(f"执行：{label}")
        self._run_async(work, lambda r, e: self._show_cmd(label, r, e))

    def _show_cmd(self, label: str, result, error) -> None:
        self.suite_status.configure(text="就绪", foreground=MUTED)
        if error:
            self._log(f"{label} 失败：{error}", "err")
            return
        self._show_rows(self.item_tree, result.rows())
        if getattr(result, "ok", True):
            self._log(f"{label} 完成：OK", "ok")
        else:
            self._log(f"{label} 完成：NG", "err")

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
        self._log(f"设备固件版本 {version.text}", "ok" if version.ok else "err")


def run() -> int:
    root = tk.Tk()
    TestApp(root)
    root.mainloop()
    return 0
