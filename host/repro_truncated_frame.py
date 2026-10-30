#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""A 项的验收脚本：截断帧之后的那条命令，还会不会被吞掉。

背景（修复前）
--------------
设备端 comm.c 的解析器在 PS_PAYLOAD 状态只按 s_idx >= s_len 推进，
没有任何"字节间隔超时"。所以：
  1. 发一个声明 LEN=32 但只给 1 个载荷字节的帧，然后停手；
  2. 解析器停在 PS_PAYLOAD，无限等待剩下 31 字节；
  3. 随后发一条完整的 GET_STATUS，它的 6 个字节被当成"上一帧的剩余载荷"吞掉；
  4. 凑满后算 CRC 必然不符 -> 回 NACK(1)，那条命令没有执行也没有应答。

修复（2026-10-30 第五轮）
------------------------
comm.c 加了 50ms 帧内字节间隔超时：半帧卡住超过 50ms 就被静默丢弃、解析器复位。
因此第 3 步那条 GET_STATUS 会被正常执行。

判读（判据已反转）
------------------
设备每 200ms 主动上报状态帧（0x81），所以"有没有收到 0x81"不能单独作判据。
唯一有效的判据是 **NACK CRC 错误帧 AA 55 7F 01** —— 周期上报永远不会产生它。

    看到 NACK(1)  -> 缺陷仍在（旧固件，或超时没生效）
    没有 NACK(1)  -> 修复生效

另外用基线段的状态帧数量判断"链路是否通"，避免把"根本没连上"误判成"修复成功"。

用法
----
    python repro_truncated_frame.py --port COM5
    python repro_truncated_frame.py --port COM5 --dry-run   # 只看字节，不发

接线：USB-TTL 的 RXD -> PA9(TX)，TXD -> PA10(RX)，GND -> GND，115200 8N1
"""

import argparse
import sys
import time

try:
    from protocol import crc16_ccitt as _ref_crc  # 同目录已有实现，用来交叉验证
except Exception:
    _ref_crc = None


def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE：poly 0x1021，init 0xFFFF，不反射，无 xorout。"""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    body = bytes([cmd, len(payload)]) + payload
    c = crc16(body)
    return b"\xAA\x55" + body + bytes([c & 0xFF, (c >> 8) & 0xFF])


def hexs(b: bytes) -> str:
    return " ".join(f"{x:02X}" for x in b)


def selfcheck() -> None:
    """确认本脚本的 CRC 与 host/protocol.py 一致。"""
    probe = b"\x01\x00"
    if _ref_crc is not None:
        assert crc16(probe) == _ref_crc(probe), "CRC 与 protocol.py 不一致"
        print(f"[selfcheck] CRC 与 protocol.py 一致，CRC(CMD+LEN)=0x{crc16(probe):04X}")
    else:
        print(f"[selfcheck] 未导入 protocol.py，本地 CRC(CMD+LEN)=0x{crc16(probe):04X}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="串口号，如 COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--dry-run", action="store_true", help="只打印字节，不打开串口")
    args = ap.parse_args()

    selfcheck()

    # 第 1 段：截断帧。CMD=0x02(SET_TARGET)，LEN 谎报 32，只给 1 字节载荷，不给 CRC。
    partial = b"\xAA\x55\x02\x20\x00"
    # 第 2 段：一条完全合法的 GET_STATUS。
    full = build_frame(0x01)

    print(f"\n[1] 截断帧      ({len(partial)}B): {hexs(partial)}")
    print(f"    含义: 帧头 AA55 / CMD=02 SET_TARGET / LEN=0x20=32 / 载荷只给 0x00 就停手")
    print(f"[2] 合法 GET_STATUS ({len(full)}B): {hexs(full)}")

    if args.dry_run:
        print("\n[dry-run] 未发送。确认上面的字节后去掉 --dry-run 再跑。")
        return 0

    try:
        import serial
    except ImportError:
        print("\n缺少 pyserial：pip install pyserial")
        return 2

    try:
        sp = serial.Serial(args.port, args.baud, timeout=0.1)
    except Exception as e:
        print(f"\n打开 {args.port} 失败：{e}")
        return 2

    def drain(seconds: float) -> bytes:
        buf = b""
        end = time.time() + seconds
        while time.time() < end:
            buf += sp.read(256)
        return buf

    with sp:
        time.sleep(0.3)
        sp.reset_input_buffer()

        base = drain(0.7)
        n_base_status = base.count(b"\xAA\x55\x81")
        print(f"\n[基线] 0.7s 内收到 {len(base)}B，状态帧 x{n_base_status}")

        sp.write(partial)
        time.sleep(0.10)                 # 模拟主机"发一半卡住"
        sp.write(full)
        print(f"[动作] 已发送截断帧 + 合法 GET_STATUS")

        resp = drain(0.7)

    n_nack_crc = resp.count(b"\xAA\x55\x7F\x01")
    n_status = resp.count(b"\xAA\x55\x81")

    print(f"\n[结果] 0.7s 内收到 {len(resp)}B")
    print(f"  状态帧 0x81 : x{n_status}  (基线 x{n_base_status}，属 200ms 周期上报，不算应答)")
    print(f"  NACK CRC(1) : x{n_nack_crc}  <- 只有截断才会产生")
    if resp:
        print(f"  原始字节: {hexs(resp[:64])}{' ...' if len(resp) > 64 else ''}")

    print()
    if n_base_status == 0:
        print("判定：无效。基线段就没收到状态帧 —— 链路不通（固件没跑 / 波特率不对 / 接线反了）。")
        print("      先单独确认能收到 0x81，再跑本脚本。")
        return 1
    if n_nack_crc > 0:
        print("判定：FAIL。仍然看到 NACK CRC(1) —— 截断帧把后续命令吞掉了，A 项修复未生效。")
        print("      检查固件是否为本版本（comm.c 的 FRAME_GAP_TIMEOUT_MS）、以及是否真的烧录成功。")
        return 1
    print("判定：PASS。截断帧之后没有 NACK(1)，说明半帧已被超时丢弃、后续命令正常执行。")
    print("      （如需更强证据，可配合逻辑分析仪确认那条 GET_STATUS 收到了 0x81 应答）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
