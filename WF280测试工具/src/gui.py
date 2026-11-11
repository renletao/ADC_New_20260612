# -*- coding: utf-8 -*-
"""图形界面（tkinter）。由 wf280_tool.py 在无参数时调用。

三块功能严格分开，互不串联：
    ① 整机测试   固件 cmd[0] = 0x14，即 AllTestFor280 那条综合测试
    ② 轨压回读   {1, 0x211}
    ③ 单项命令   OTP / 输出测试 / 读系数 / 读 M1 / 设定电压

“整机测试”就是 AllTestFor280 一条命令的异步执行，不会顺带跑其它命令。
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
    SHORT_CURRENT_UA,
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

APP_VERSION = "v1.2 · 2026-11-08"


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
        self._poll_auto_rail()

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
        style.configure("Card.TCheckbutton", background=CARD, foreground=TEXT)
        style.configure("Muted.TLabel", background=CARD, foreground=MUTED,
                        font=("Microsoft YaHei UI", 9))
        style.configure("Head.TLabel", background=CARD, foreground=TEXT,
                        font=("Microsoft YaHei UI", 11, "bold"))
        style.configure("TButton", padding=(10, 5))
        style.configure("Treeview", background=CARD, fieldbackground=CARD,
                        foreground=TEXT, rowheight=25, borderwidth=0)
        style.configure("Treeview.Heading",
                        font=("Microsoft YaHei UI", 9, "bold"),
                        background="#eceef1", foreground=TEXT)
        style.map("Treeview", background=[("selected", "#dbeafe")],
                  foreground=[("selected", TEXT)])

    # ---------- 布局 ----------

    def _build_ui(self) -> None:
        self.root.title(f"WF280 全功能测试工具 {APP_VERSION}")
        self.root.configure(bg=BG)
        self.root.geometry("940x900")
        self.root.minsize(880, 760)

        outer = ttk.Frame(self.root, padding=12)
        outer.pack(fill="both", expand=True)
        self._build_connection_bar(outer)
        self.info_label = ttk.Label(outer, text="未连接设备", foreground=MUTED)
        self.info_label.pack(fill="x", pady=(0, 8))
        self._build_full_card(outer)
        self._build_rail_card(outer)
        self._build_item_card(outer)
        self._build_log(outer)
        self._log(f"工具版本 {APP_VERSION}（窗口标题里也有，别开成旧的了）", "muted")

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
        return tree

    def _build_full_card(self, parent) -> None:
        card = self._card(parent, "① 整机测试",
                          "AllTestFor280    cmd[0] = 0x14    取数：轮询 / 固定 1 s")

        row = ttk.Frame(card, style="Card.TFrame")
        row.pack(fill="x", pady=(8, 6))
        ttk.Label(row, text="设定电压", style="Card.TLabel").pack(side="left")
        self.voltage_var = tk.StringVar(value="3300")
        ttk.Entry(row, textvariable=self.voltage_var, width=7).pack(
            side="left", padx=(4, 2))
        ttk.Label(row, text="mV", style="Muted.TLabel").pack(side="left",
                                                                padx=(0, 8))
        ttk.Button(row, text="设定电压", width=10,
                   command=self._on_set_supply).pack(side="left", padx=(0, 14))
        self.full_btn = ttk.Button(row, text="开始整机测试",
                                   command=self._on_full_test)
        self.full_btn.pack(side="left")
        self.full_delay_btn = ttk.Button(row, text="整机测试(1s)", width=14,
                                         command=self._on_full_test_delay)
        self.full_delay_btn.pack(side="left", padx=(6, 0))
        self.full_status = ttk.Label(row, text="待测", style="Card.TLabel")
        self.full_status.pack(side="left", padx=(12, 0))
        self.full_verdict = ttk.Label(row, text="", style="Card.TLabel",
                                      font=("Microsoft YaHei UI", 11, "bold"))
        self.full_verdict.pack(side="left", padx=(12, 0))

        tree_wrap = ttk.Frame(card, style="Card.TFrame")
        tree_wrap.pack(fill="x")
        self.full_tree = self._tree(tree_wrap, 12)
        tree_sb = ttk.Scrollbar(tree_wrap, orient="vertical",
                                command=self.full_tree.yview)
        self.full_tree.configure(yscrollcommand=tree_sb.set)
        self.full_tree.pack(side="left", fill="x", expand=True)
        tree_sb.pack(side="right", fill="y")

    def _build_rail_card(self, parent) -> None:
        card = self._card(parent, "② 轨压回读",
                          "{1, 0x211}    同步，随时可读，不改变供电状态")

        row = ttk.Frame(card, style="Card.TFrame")
        row.pack(fill="x", pady=(8, 4))
        self.rail_btn = ttk.Button(row, text="读取轨压", command=self._on_read_rail)
        self.rail_btn.pack(side="left")
        self.rail_set_btn = ttk.Button(row, text="设压并回读",
                                       command=self._on_set_and_read_rail)
        self.rail_set_btn.pack(side="left", padx=(8, 0))
        ttk.Checkbutton(row, text="连续监测（0.5 s）", variable=self._auto_rail_var(),
                        style="Card.TCheckbutton").pack(side="left", padx=(12, 0))

        self.rail_value = tk.Label(card, text="----", bg=CARD, fg=MUTED,
                                   font=("Consolas", 20, "bold"))
        self.rail_value.pack(side="left", padx=(12, 0))
        self.rail_verdict = ttk.Label(card, text="尚未读取", style="Muted.TLabel")
        self.rail_verdict.pack(side="left", padx=(8, 0))

        ttk.Label(card,
                  text="提示：输出测试正常结束后会保持供电，可直接点「读取轨压」；"
                       "整机测试 / 读系数 / 读 M1 结束时仍会断电，"
                       "这时单点「读取轨压」看到接近 0 mV 是正常的，"
                       "要看带电轨压请用「设压并回读」。",
                  style="Muted.TLabel", wraplength=860).pack(anchor="w", pady=(2, 0))

    def _auto_rail_var(self) -> tk.BooleanVar:
        if not hasattr(self, "auto_rail"):
            self.auto_rail = tk.BooleanVar(value=False)
        return self.auto_rail

    def _build_item_card(self, parent) -> None:
        card = self._card(parent, "③ 单项命令", "单独测某一项时用，跑完不影响其它项")

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
        for text, action in (("OTP 校验", "otp"),
                             ("输出 3.3 V", "out33"), ("输出 2.0 V", "out20"),
                             ("读系数", "coeff"), ("读 M1", "m1")):
            ttk.Button(btns, text=text,
                       command=lambda a=action: self._on_cmd(a)).pack(
                side="left", padx=(0, 6))

        setrow = ttk.Frame(card, style="Card.TFrame")
        setrow.pack(fill="x", pady=(0, 6))
        ttk.Label(setrow, text="提示：整机测试用的电压在 ① 里设定，"
                  "这里各命令按各自的固定电压执行", style="Muted.TLabel").pack(side="left")

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
            infos = list(list_ports.comports())
        except Exception:
            infos = []

        def score(port) -> int:
            """接板子的 USB 转串口优先，调试器/蓝牙靠后。"""
            text = f"{port.device} {port.description}".lower()
            if "jlink" in text or "segger" in text:
                return -10
            if "ch340" in text or "usb-serial" in text or "cp210" in text \
                    or "ft232" in text or "usb serial" in text:
                return 10
            if "蓝牙" in text or "bluetooth" in text:
                return -5
            return 0

        infos.sort(key=score, reverse=True)
        ports = [p.device for p in infos]
        self.port_box["values"] = ports
        if ports and (not self.port_var.get() or self.port_var.get() not in ports):
            self.port_var.set(ports[0])
            desc = next((p.description for p in infos if p.device == ports[0]), "")
            self._log(f"默认选中 {ports[0]}（{desc}），不对的话手动改", "muted")
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
                    self.full_status.configure(text=payload, foreground=ACCENT)
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

    def _show_rows(self, tree: ttk.Treeview, rows, skip=None) -> None:
        if skip:
            rows = [r for r in rows if r[0] not in skip]
        for item, value, verdict in rows:
            if verdict == "OK":
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

    # ---------- ① 整机测试（AllTestFor280）----------

    def _on_full_test(self) -> None:
        self._start_full(
            "流程：写 [0x14] → 轮询 [1,0x400] → 写 [1,0x401] 读 10 字",
            lambda: self.tester.run_full_test(
                on_tick=lambda e: self._post_status(f"测试中… {e:.1f} s")))

    def _on_full_test_delay(self) -> None:
        self._start_full(
            "流程：写 [0x14] → 等 1 s → 写 [1,0x401] → 读 10 个寄存器",
            self.tester.run_full_test_fixed)

    def _start_full(self, note, worker) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        self._set_full_busy(True)
        self.full_status.configure(text="测试中…", foreground=ACCENT)
        self.full_verdict.configure(text="")
        for item in self.full_tree.get_children():
            self.full_tree.delete(item)
        self._log(f"触发整机测试 cmd[0]=0x14（AllTestFor280）；{note}；"
                  "轨压沿用当前设定，本工具不自动设压")

        def work():
            # 整机测试自己不开电：轨压必须事先由使用者用「设定电压」设好，
            # 固件只回读实际轨压并按它换算电流。
            return worker()

        def done(result, error):
            self._set_full_busy(False)
            self.full_status.configure(text="完成", foreground=MUTED)
            if error:
                self.full_verdict.configure(text="中断", foreground=NG_COLOR)
                self._log(f"整机测试失败：{error}", "err")
                return
            self._show_full_result(result)

        self._run_async(work, done)

    def _set_full_busy(self, busy: bool) -> None:
        state = "disabled" if busy else "normal"
        for btn in (self.full_btn, self.full_delay_btn):
            btn.configure(state=state)

    def _show_full_result(self, result: FullTestResult) -> None:
        self._show_rows(self.full_tree, result.rows())
        if result.all_ok:
            self.full_verdict.configure(text="PASS", foreground=OK_COLOR)
            self._log(f"AllTestFor280 完成：PASS，实测 {result.rail_mv} mV，"
                      f"工作 {result.work_ua} uA / 休眠 {result.sleep_ua} uA", "ok")
            return
        # 精简帧没有总状态字，逐项归纳失败原因
        problems = []
        if not result.rail_mv:
            problems.append("轨压未建立")
        if result.sleep_ua >= SHORT_CURRENT_UA \
                or result.work_ua >= SHORT_CURRENT_UA:
            problems.append("疑似短路")
        if result.meas_err:
            problems.append(f"测量失败（{status_text(result.meas_err)}）")
        reason = "；".join(problems) if problems else "数据异常"
        self.full_verdict.configure(text="NG", foreground=NG_COLOR)
        self._log(f"AllTestFor280 完成：NG，{reason}，实测 {result.rail_mv} mV",
                  "err")

    # ---------- ② 轨压 ----------

    def _on_read_rail(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        self._run_async(self.tester.read_rail, self._show_rail)

    def _on_set_and_read_rail(self) -> None:
        """先按 ① 里的电压设压，等轨稳定再回读 —— 想看带电轨压就用这个。"""
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        mv = self._voltage()
        if mv is None:
            return
        self.rail_value.configure(text="读取中…", fg=MUTED)
        self._log(f"设压 {mv} mV 后回读轨压")

        def work():
            self.tester.set_supply(mv)
            time.sleep(0.35)
            return self.tester.read_rail()

        self._run_async(work, self._show_rail)

    def _show_rail(self, result: RailReading | None, error=None) -> None:
        if error:
            self.rail_value.configure(text="----", fg=MUTED)
            self.rail_verdict.configure(text=f"读取失败：{error}")
            self._log(f"轨压读取失败：{error}", "err")
            return
        self.rail_value.configure(text=f"{result.mv} mV",
                                  fg=OK_COLOR if result.ok else NG_COLOR)
        self.rail_verdict.configure(text=result.verdict)

    def _poll_auto_rail(self) -> None:
        if self.auto_rail.get() and self.tester is not None and not self.busy:
            self._on_read_rail()
        self.root.after(500, self._poll_auto_rail)

    # ---------- ③ 单项命令 ----------

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
            "out33": (f"输出 3.3 V（{measurement:#06x} / {settle} ms）",
                      lambda: self.tester.test_output_3v3(measurement, settle)),
            "out20": (f"输出 2.0 V（{measurement:#06x} / {settle} ms）",
                      lambda: self.tester.test_output_2v0(measurement, settle)),
            "coeff": (f"读系数（{count} 路）", lambda: self.tester.read_coeff(count)),
            "m1": (f"读 M1（{count} 路）", lambda: self.tester.read_m1(count)),
        }
        label, work = jobs[action]

        for item in self.item_tree.get_children():
            self.item_tree.delete(item)
        self._log(f"执行：{label}")
        self._run_async(work, lambda r, e: self._show_cmd(label, r, e))

    def _show_cmd(self, label: str, result, error) -> None:
        if error:
            self._log(f"{label} 失败：{error}", "err")
            return
        self._show_rows(self.item_tree, result.rows())
        if getattr(result, "ok", True):
            self._log(f"{label} 完成：OK", "ok")
        else:
            self._log(f"{label} 完成：NG", "err")

    def _on_set_supply(self) -> None:
        if self.tester is None:
            self._log("请先连接串口", "err")
            return
        mv = self._voltage()
        if mv is None:
            return
        self._log(f"设定轨压 {mv} mV（供设压回读/单项命令使用；整机测试固定 3.3 V）")
        self._run_async(lambda: _Simple(self.tester.set_supply(mv)),
                        lambda r, e: self._show_set_supply(mv, r, e))

    def _show_set_supply(self, mv: int, result, error) -> None:
        if error:
            self._log(f"设定电压失败：{error}", "err")
            return
        if result.ok:
            self.full_status.configure(text=f"已设压 {mv} mV", foreground=MUTED)
            self._log(f"轨压已设为 {mv} mV", "ok")
        else:
            self._log(f"设定电压失败：{status_text(result.status)}", "err")

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
