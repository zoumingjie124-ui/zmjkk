"""智能温控终端 —— 上位机实时监控与在线调参 GUI。

运行：
    python host/app_gui.py
依赖：pyserial（已随工程环境提供）、tkinter（Python 自带，无需 pip）。

功能：
    - 串口连接（115200 8N1），后台线程收帧，GUI 线程只做渲染
    - 实时曲线：实际温度 / 目标温度 / PWM 占空比（双 Y 轴）
    - 状态徽标：RUN/STB/FLT、SIM/REAL、故障原因
    - 控制面板：启停、故障确认、远程设温、在线改 PID、保存参数到 Flash
    - 指令应答与错误码在日志区可见（非法参数/校验错都会显示原因）

注意：GUI 只做展示与操作，任何安全联锁都在下位机实现，上位机断开不影响控温安全。
"""

from __future__ import annotations

import queue
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk

import protocol as P

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover - 环境缺 pyserial 时给出明确提示
    serial = None
    list_ports = None

MAX_POINTS = 600
TEMP_MIN, TEMP_MAX = 20.0, 80.0
COLOR_TEMP = "#d85a30"
COLOR_TARGET = "#5f5e5a"
COLOR_DUTY = "#378add"
COLOR_BG = "#ffffff"
COLOR_GRID = "#e5e3dc"


class SerialWorker(threading.Thread):
    """串口读取线程：只负责收字节 -> 解析 -> 塞队列。"""

    def __init__(self, port: str, baud: int, out_q: "queue.Queue") -> None:
        super().__init__(daemon=True)
        self.port = port
        self.baud = baud
        self.out_q = out_q
        self.parser = P.Parser()
        self._stop = threading.Event()
        self.ser = None
        self.error = None

    def run(self) -> None:
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.1)
        except Exception as exc:  # noqa: BLE001
            self.error = str(exc)
            self.out_q.put(("error", f"打开串口失败: {exc}"))
            return

        self.out_q.put(("info", f"已连接 {self.port} @ {self.baud}"))
        while not self._stop.is_set():
            try:
                chunk = self.ser.read(64)
            except Exception as exc:  # noqa: BLE001
                self.out_q.put(("error", f"串口读取异常: {exc}"))
                break
            if not chunk:
                continue

            for frame in self.parser.feed(chunk):
                if frame.cmd == (P.CMD_GET_STATUS | P.RESP_FLAG):
                    try:
                        self.out_q.put(("status", P.parse_status(frame.payload)))
                    except ValueError as exc:
                        self.out_q.put(("error", f"状态帧解析失败: {exc}"))
                elif frame.cmd == (P.CMD_GET_PARAMS | P.RESP_FLAG):
                    try:
                        self.out_q.put(("params", P.parse_params(frame.payload)))
                    except ValueError as exc:
                        self.out_q.put(("error", f"参数帧解析失败: {exc}"))
                elif frame.cmd == P.CMD_NACK:
                    code = frame.payload[0] if frame.payload else 0xFF
                    self.out_q.put(("error", f"NACK: {P.ACK_TEXT.get(code, code)}"))
                elif frame.is_response:
                    code = frame.payload[0] if frame.payload else 0xFF
                    self.out_q.put(("ack", f"0x{frame.cmd:02X} {P.ACK_TEXT.get(code, code)}"))
            if self.parser.crc_errors:
                self.out_q.put(("stat", f"CRC错误累计 {self.parser.crc_errors}"))
                self.parser.crc_errors = 0
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send(self, data: bytes) -> None:
        if self.ser and self.ser.is_open:
            self.ser.write(data)

    def stop(self) -> None:
        self._stop.set()


class App(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("智能温控与故障监测终端 — 上位机")
        self.geometry("1080x680")
        self.configure(bg=COLOR_BG)

        self.q: "queue.Queue" = queue.Queue()
        self.worker: SerialWorker | None = None
        self.t_hist: list[float] = []
        self.d_hist: list[float] = []
        self.s_hist: list[float] = []
        self.last_status: dict | None = None

        self._build_ui()
        self.after(50, self._poll_queue)

    # ---------------- UI ----------------
    def _build_ui(self) -> None:
        top = tk.Frame(self, bg=COLOR_BG)
        top.pack(fill="x", padx=12, pady=(10, 4))

        tk.Label(top, text="串口", bg=COLOR_BG).pack(side="left")
        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=22, values=self._ports())
        self.port_box.pack(side="left", padx=6)
        tk.Button(top, text="刷新", command=self._refresh_ports).pack(side="left")
        self.btn_conn = tk.Button(top, text="连接", width=8, command=self._toggle_conn)
        self.btn_conn.pack(side="left", padx=8)
        tk.Label(top, text="115200 8N1", bg=COLOR_BG, fg="#888780").pack(side="left")

        badges = tk.Frame(self, bg=COLOR_BG)
        badges.pack(fill="x", padx=12, pady=4)
        self.b_state = tk.Label(badges, text="--", width=6, bg="#d3d1c7", fg="white",
                                font=("Consolas", 14, "bold"))
        self.b_state.pack(side="left")
        self.b_mode = tk.Label(badges, text="--", width=8, bg="#b4b2a9", fg="white",
                               font=("Consolas", 12, "bold"))
        self.b_mode.pack(side="left", padx=8)
        self.b_fault = tk.Label(badges, text="FAULT: -", bg=COLOR_BG, fg="#a32d2d",
                                font=("Consolas", 11))
        self.b_fault.pack(side="left", padx=8)
        self.b_read = tk.Label(badges, text="T = --.- C   SET = --.- C   PWM = --- %   FAN = -",
                               bg=COLOR_BG, font=("Consolas", 12))
        self.b_read.pack(side="left", padx=16)

        body = tk.Frame(self, bg=COLOR_BG)
        body.pack(fill="both", expand=True, padx=12, pady=6)

        self.canvas = tk.Canvas(body, bg=COLOR_BG, highlightthickness=1,
                                highlightbackground="#d3d1c7")
        self.canvas.pack(side="left", fill="both", expand=True)

        panel = tk.Frame(body, bg=COLOR_BG, width=270)
        panel.pack(side="right", fill="y", padx=(10, 0))
        panel.pack_propagate(False)

        row = 0
        tk.Label(panel, text="目标温度 (0~70 ℃)", bg=COLOR_BG).grid(row=row, column=0, sticky="w")
        row += 1
        self.e_target = tk.Entry(panel, width=10)
        self.e_target.insert(0, "45.0")
        self.e_target.grid(row=row, column=0, sticky="w", pady=2)
        tk.Button(panel, text="下发", width=8,
                  command=lambda: self._send(P.build_set_target(self._f(self.e_target)))).grid(
            row=row, column=1, padx=4)
        row += 1

        tk.Label(panel, text="Kp / Ki / Kd", bg=COLOR_BG).grid(row=row, column=0, sticky="w", pady=(8, 0))
        row += 1
        self.e_kp = tk.Entry(panel, width=6); self.e_kp.insert(0, "8.0")
        self.e_ki = tk.Entry(panel, width=6); self.e_ki.insert(0, "0.10")
        self.e_kd = tk.Entry(panel, width=6); self.e_kd.insert(0, "1.5")
        self.e_kp.grid(row=row, column=0, sticky="w")
        self.e_ki.grid(row=row, column=1, sticky="w")
        self.e_kd.grid(row=row, column=2, sticky="w")
        row += 1
        tk.Button(panel, text="应用 PID", width=10, command=self._apply_pid).grid(
            row=row, column=0, sticky="w", pady=2)
        row += 1

        tk.Button(panel, text="启动 (START)", width=20,
                  command=lambda: self._send(P.build_simple(P.CMD_START))).grid(
            row=row, column=0, columnspan=3, sticky="we", pady=(10, 2))
        row += 1
        tk.Button(panel, text="停止 (STOP)", width=20,
                  command=lambda: self._send(P.build_simple(P.CMD_STOP))).grid(
            row=row, column=0, columnspan=3, sticky="we", pady=2)
        row += 1
        tk.Button(panel, text="故障确认 (ACK)", width=20,
                  command=lambda: self._send(P.build_simple(P.CMD_ACK_FAULT))).grid(
            row=row, column=0, columnspan=3, sticky="we", pady=2)
        row += 1
        tk.Button(panel, text="读取参数", width=20,
                  command=lambda: self._send(P.build_simple(P.CMD_GET_PARAMS))).grid(
            row=row, column=0, columnspan=3, sticky="we", pady=(10, 2))
        row += 1
        tk.Button(panel, text="保存到 Flash", width=20,
                  command=lambda: self._send(P.build_simple(P.CMD_SAVE_PARAMS))).grid(
            row=row, column=0, columnspan=3, sticky="we", pady=2)
        row += 1

        tk.Label(panel, text="日志", bg=COLOR_BG).grid(row=row, column=0, sticky="w", pady=(10, 0))
        row += 1
        self.log = tk.Text(panel, height=10, width=36, font=("Consolas", 9), wrap="none")
        self.log.grid(row=row, column=0, columnspan=3, sticky="nsew")
        panel.grid_rowconfigure(row, weight=1)
        panel.grid_columnconfigure(2, weight=1)

    def _ports(self) -> list[str]:
        if list_ports is None:
            return []
        return [p.device for p in list_ports.comports()]

    def _refresh_ports(self) -> None:
        self.port_box["values"] = self._ports()

    @staticmethod
    def _f(entry: tk.Entry) -> float:
        try:
            return float(entry.get().strip())
        except ValueError:
            return float("nan")

    # ---------------- 连接 ----------------
    def _toggle_conn(self) -> None:
        if self.worker is not None:
            self.worker.stop()
            self.worker = None
            self.btn_conn.config(text="连接")
            self._log("已断开串口")
            return

        if serial is None:
            messagebox.showerror("缺少依赖", "未安装 pyserial，请先 pip install pyserial")
            return
        port = self.port_var.get().strip()
        if not port:
            messagebox.showwarning("提示", "请选择串口")
            return
        self.worker = SerialWorker(port, 115200, self.q)
        self.worker.start()
        self.btn_conn.config(text="断开")

    def _send(self, data: bytes) -> None:
        if self.worker is None:
            self._log("未连接串口")
            return
        try:
            self.worker.send(data)
        except Exception as exc:  # noqa: BLE001
            self._log(f"发送失败: {exc}")

    def _apply_pid(self) -> None:
        kp, ki, kd = self._f(self.e_kp), self._f(self.e_ki), self._f(self.e_kd)
        if not P.validate_pid(kp, ki, kd):
            self._log("PID 参数超出允许范围（Kp<=50, Ki<=5, Kd<=20），已阻止发送")
            return
        self._send(P.build_set_pid(kp, ki, kd))

    # ---------------- 消息泵 ----------------
    def _poll_queue(self) -> None:
        try:
            while True:
                kind, data = self.q.get_nowait()
                if kind == "status":
                    self._on_status(data)
                elif kind == "params":
                    self.e_target.delete(0, "end"); self.e_target.insert(0, f"{data['target']:.1f}")
                    self.e_kp.delete(0, "end"); self.e_kp.insert(0, f"{data['kp']:.2f}")
                    self.e_ki.delete(0, "end"); self.e_ki.insert(0, f"{data['ki']:.2f}")
                    self.e_kd.delete(0, "end"); self.e_kd.insert(0, f"{data['kd']:.2f}")
                    self._log("已读取下位机参数")
                elif kind == "ack":
                    self._log(f"ACK {data}")
                elif kind == "error":
                    self._log(f"ERROR {data}")
                elif kind == "stat":
                    self._log(data)
                else:
                    self._log(str(data))
        except queue.Empty:
            pass
        self.after(50, self._poll_queue)

    def _on_status(self, st: dict) -> None:
        self.last_status = st
        state = st["state"]
        colors = {"RUN": "#3b6d11", "STB": "#888780", "FLT": "#a32d2d", "INI": "#854f0b"}
        self.b_state.config(text=state, bg=colors.get(state, "#888780"))
        if st["sim"]:
            self.b_mode.config(text="SIM", bg="#ba7517")
        else:
            self.b_mode.config(text="REAL", bg="#185fa5")
        self.b_fault.config(text=f"FAULT: {st['fault']}")

        temp_txt = f"{st['temp']:.1f}" if st["temp"] > -900 else "ERR"
        self.b_read.config(
            text=f"T = {temp_txt} C   SET = {st['target']:.1f} C   "
                 f"PWM = {st['duty']:3d} %   FAN = {'ON ' if st['fan'] else 'OFF'}")

        if st["temp"] > -900:
            self.t_hist.append(st["temp"])
        else:
            self.t_hist.append(self.t_hist[-1] if self.t_hist else TEMP_MIN)
        self.s_hist.append(st["target"])
        self.d_hist.append(float(st["duty"]))
        for buf in (self.t_hist, self.s_hist, self.d_hist):
            if len(buf) > MAX_POINTS:
                del buf[0]
        self._draw()

    # ---------------- 绘图 ----------------
    def _draw(self) -> None:
        c = self.canvas
        c.delete("all")
        w = max(c.winfo_width(), 200)
        h = max(c.winfo_height(), 150)
        pad_l, pad_r, pad_t, pad_b = 44, 44, 12, 24
        pw, ph = w - pad_l - pad_r, h - pad_t - pad_b

        def ty(v: float) -> float:                     # 温度轴
            v = min(max(v, TEMP_MIN), TEMP_MAX)
            return pad_t + ph * (1.0 - (v - TEMP_MIN) / (TEMP_MAX - TEMP_MIN))

        def dy(v: float) -> float:                     # 占空比轴
            return pad_t + ph * (1.0 - v / 100.0)

        for i in range(5):                             # 网格 + 左轴刻度
            y = pad_t + ph * i / 4
            c.create_line(pad_l, y, pad_l + pw, y, fill=COLOR_GRID)
            temp_val = TEMP_MAX - (TEMP_MAX - TEMP_MIN) * i / 4
            c.create_text(pad_l - 6, y, text=f"{temp_val:.0f}", anchor="e",
                          fill=COLOR_TARGET, font=("Consolas", 9))
            c.create_text(pad_l + pw + 6, y, text=f"{100 - 25 * i}", anchor="w",
                          fill=COLOR_DUTY, font=("Consolas", 9))

        c.create_text(8, pad_t, text="℃", anchor="nw", fill=COLOR_TEMP, font=("Consolas", 9))
        c.create_text(w - 8, pad_t, text="%", anchor="ne", fill=COLOR_DUTY, font=("Consolas", 9))

        n = min(len(self.t_hist), MAX_POINTS)
        if n < 2:
            c.create_text(w / 2, h / 2, text="等待数据…（连接串口后自动开始绘图）",
                          fill="#888780", font=("Microsoft YaHei", 11))
            return

        step = pw / (MAX_POINTS - 1)
        x0 = pad_l + pw - (n - 1) * step

        def poly(vals: list[float], mapper, color: str, dash=None) -> None:
            pts = []
            for i, v in enumerate(vals[-n:]):
                pts.extend((x0 + i * step, mapper(v)))
            if len(pts) >= 4:
                c.create_line(*pts, fill=color, width=2, dash=dash)

        poly(self.s_hist, ty, COLOR_TARGET, dash=(4, 3))
        poly(self.t_hist, ty, COLOR_TEMP)
        poly(self.d_hist, dy, COLOR_DUTY)

        c.create_text(pad_l + 6, pad_t + 6, anchor="nw", text="— 实际温度", fill=COLOR_TEMP,
                      font=("Microsoft YaHei", 9))
        c.create_text(pad_l + 96, pad_t + 6, anchor="nw", text="-- 目标温度", fill=COLOR_TARGET,
                      font=("Microsoft YaHei", 9))
        c.create_text(pad_l + 196, pad_t + 6, anchor="nw", text="— PWM 占空比(右轴)",
                      fill=COLOR_DUTY, font=("Microsoft YaHei", 9))

    def _log(self, text: str) -> None:
        ts = time.strftime("%H:%M:%S")
        self.log.insert("end", f"[{ts}] {text}\n")
        self.log.see("end")

    def on_close(self) -> None:
        if self.worker is not None:
            self.worker.stop()
        self.destroy()


if __name__ == "__main__":
    app = App()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
