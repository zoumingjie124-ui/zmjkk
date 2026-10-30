"""无 GUI 的终端实时监控（tkinter 不可用或需要无头环境时使用）。

用法：
    python host/monitor_cli.py --port COM5        # 实时监视
    python host/monitor_cli.py --demo             # 无硬件演示：合成状态帧跑通解析链路
    python host/monitor_cli.py --port COM5 --cmd target=48.0
                                                  # 连上后下发一条指令再监视
    python host/monitor_cli.py --port COM5 --csv run1.csv
                                                  # 同时把状态帧记录成 CSV（整定数据）

注意：本工具与 app_gui.py 共用 protocol.py，协议行为完全一致。
"""

from __future__ import annotations

import argparse
import csv
import random
import struct
import sys
import time

import protocol as P

try:
    import serial
except ImportError:
    serial = None


def render(st: dict, rx_bytes: int, crc_err: int) -> str:
    temp = f"{st['temp']:6.2f}" if st["temp"] > -900 else "   ERR"
    return (f"[{time.strftime('%H:%M:%S')}] {st['state']} "
            f"T={temp}C SET={st['target']:6.2f}C PWM={st['duty']:3d}% "
            f"FAN={'ON ' if st['fan'] else 'OFF'} "
            f"{'SIM ' if st['sim'] else 'REAL'} FAULT={st['fault']:<8} "
            f"(rx={rx_bytes}B crc_err={crc_err})")


def run_demo(seconds: float = 6.0) -> int:
    """无硬件演示：用一阶惯性模型合成状态帧，走真实 Parser 链路。"""
    print("=== monitor_cli demo: 合成状态帧 -> protocol.Parser -> 渲染 ===")
    parser = P.Parser()
    rnd = random.Random(7)
    temp, target = 25.0, 45.0
    duty, fan = 0, 0
    t0 = time.time()
    n = 0
    crc_err_seen = 0

    while time.time() - t0 < seconds:
        err = target - temp
        duty = max(0, min(100, int(8 * err + 50)))
        fan = 1 if temp > target + 2.0 else 0
        temp += 0.5 * (25 + 40 * duty / 100.0 * (0.6 if fan else 1.0) - temp) / 60.0
        payload = struct.pack("<ffBBBBBfff", temp, target, duty, fan, 2, 1, 0, 8.0, 0.1, 1.5)
        frames = parser.feed(P.build_frame(P.CMD_GET_STATUS | P.RESP_FLAG, payload))
        # 顺带注入一个坏帧，验证 CRC 检测在真实链路上生效
        if n % 10 == 9:
            bad = bytearray(P.build_frame(P.CMD_GET_STATUS | P.RESP_FLAG, payload))
            bad[-1] ^= 0xFF
            parser.feed(bytes(bad))
            crc_err_seen = parser.crc_errors
        for f in frames:
            print(render(P.parse_status(f.payload), len(payload) * (n + 1), crc_err_seen))

        # 每 15 帧演示一次诊断帧解析（0x89：溢出计数 + 累计接收字节）
        if n % 15 == 14:
            dpl = struct.pack("<II", 0, len(payload) * (n + 1))
            for f in parser.feed(P.build_frame(P.CMD_GET_DIAG | P.RESP_FLAG, dpl)):
                d = P.parse_diag(f.payload)
                print(f"DIAG overflow={d['overflow']} rx_bytes={d['rx_bytes']}")

        n += 1
        time.sleep(0.2)

    print(f"=== demo done: {n} frames, crc_errors={parser.crc_errors}, "
          f"junk={parser.junk_bytes} ===")
    return 0


def run_serial(port: str, baud: int, one_cmd: str | None, csv_path: str | None = None) -> int:
    if serial is None:
        print("需要 pyserial：pip install pyserial")
        return 1
    ser = serial.Serial(port, baud, timeout=0.2)
    parser = P.Parser()
    rx = 0
    last_diag = 0.0          # 上次发 GET_DIAG 的时刻（诊断每 5s 拉一次）
    csv_file = None
    writer = None
    t_start = time.time()
    if csv_path:
        # 整定用的原始数据：time_s,temp_c,target_c,duty_pct,state,fault（供 host/tune_helper.py 读取）
        csv_file = open(csv_path, "w", newline="", encoding="utf-8")
        writer = csv.writer(csv_file)
        writer.writerow(["time_s", "temp_c", "target_c", "duty_pct", "state", "fault"])
        print(f"logging status frames to {csv_path}")
    try:
        if one_cmd:
            key, _, val = one_cmd.partition("=")
            if key == "target":
                ser.write(P.build_set_target(float(val)))
                print(f">> SET_TARGET {val}")
            elif key == "start":
                ser.write(P.build_simple(P.CMD_START)); print(">> START")
            elif key == "stop":
                ser.write(P.build_simple(P.CMD_STOP)); print(">> STOP")
            elif key == "ack":
                ser.write(P.build_simple(P.CMD_ACK_FAULT)); print(">> ACK_FAULT")
        while True:
            # 周期性拉诊断：溢出计数只有主动问才拿得到（固件不主动上报）
            if time.time() - last_diag >= 5.0:
                last_diag = time.time()
                ser.write(P.build_get_diag())

            chunk = ser.read(64)
            if not chunk:
                continue
            rx += len(chunk)
            for f in parser.feed(chunk):
                if f.cmd == (P.CMD_GET_STATUS | P.RESP_FLAG):
                    st = P.parse_status(f.payload)
                    print(render(st, rx, parser.crc_errors))
                    if writer is not None:
                        writer.writerow([f"{time.time() - t_start:.2f}", f"{st['temp']:.3f}",
                                         f"{st['target']:.2f}", st["duty"], st["state"], st["fault"]])
                        csv_file.flush()
                elif f.cmd == (P.CMD_GET_PARAMS | P.RESP_FLAG):
                    print("PARAMS", P.parse_params(f.payload))
                elif f.cmd == (P.CMD_GET_DIAG | P.RESP_FLAG):
                    d = P.parse_diag(f.payload)
                    warn = "  <-- 曾有溢出丢字节" if d["overflow"] else ""
                    print(f"DIAG overflow={d['overflow']} rx_bytes={d['rx_bytes']}{warn}")
                elif f.cmd == P.CMD_NACK:
                    print("NACK", P.ACK_TEXT.get(f.payload[0] if f.payload else 0xFF))
                elif f.is_response:
                    print("ACK", hex(f.cmd), P.ACK_TEXT.get(f.payload[0] if f.payload else 0xFF))
    except KeyboardInterrupt:
        print("\nbye")
    finally:
        ser.close()
        if csv_file is not None:
            csv_file.close()
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--cmd")
    ap.add_argument("--demo", action="store_true")
    ap.add_argument("--csv", help="把状态帧记录成 CSV（升温曲线/整定数据）")
    a = ap.parse_args()

    if a.demo:
        return run_demo()
    if not a.port:
        ap.print_help()
        return 1
    return run_serial(a.port, a.baud, a.cmd, a.csv)


if __name__ == "__main__":
    sys.exit(main())
