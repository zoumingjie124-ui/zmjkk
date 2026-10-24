"""协议帧编解码与异常边界自动化测试（纯逻辑，不依赖硬件）。

运行：python host/test_protocol.py
返回码 0 = 全部通过，1 = 有失败。结果同时写入 host/test_protocol_log.txt。
"""

from __future__ import annotations

import io
import random
import struct
import sys

import protocol as P

checks = 0
failed = 0
lines: list[str] = []


def check(cond: bool, msg: str) -> None:
    global checks, failed
    checks += 1
    status = "PASS" if cond else "FAIL"
    if not cond:
        failed += 1
    lines.append(f"  [{status}] {msg}")


def section(name: str) -> None:
    lines.append(name)


# ---------------- T1 CRC 已知向量 ----------------
def t1_crc():
    section("T1 CRC16 known vector:")
    v = P.crc16_ccitt(b"123456789")
    check(v == 0x29B1, f"crc16('123456789')=0x{v:04X} == 0x29B1")
    check(P.crc16_ccitt(b"") == 0xFFFF, "crc16(empty) == 0xFFFF")


# ---------------- T2 帧结构 ----------------
def t2_frame_layout():
    section("T2 frame layout:")
    f = P.build_frame(P.CMD_SET_TARGET, P.f32(45.0))
    check(f[0] == 0xAA and f[1] == 0x55, f"header={f[0]:02X} {f[1]:02X}")
    check(f[2] == 0x02, f"cmd=0x{f[2]:02X}")
    check(f[3] == 4, f"len={f[3]}")
    check(len(f) == 6 + 4, f"total len={len(f)} == 10 (6 + LEN)")
    body = f[2:-2]
    crc_rx = f[-2] | (f[-1] << 8)
    check(P.crc16_ccitt(body) == crc_rx, "crc covers cmd..payload")


# ---------------- T3 往返与分片 ----------------
def t3_roundtrip_and_split():
    section("T3 parser round trip / arbitrary chunking:")
    p = P.Parser()
    frames = []
    payloads = [
        (P.CMD_GET_STATUS, b""),
        (P.CMD_SET_TARGET, P.f32(45.5)),
        (P.CMD_SET_PID, P.f32(8.0) + P.f32(0.1) + P.f32(1.5)),
        (P.CMD_SAVE_PARAMS, b""),
    ]
    stream = b"".join(P.build_frame(c, pl) for c, pl in payloads)
    frames += p.feed(stream)
    check(len(frames) == 4, f"whole-buffer feed -> {len(frames)} frames")

    p2 = P.Parser()
    frames2 = []
    for i in range(len(stream)):
        frames2 += p2.feed(stream[i:i + 1])       # 逐字节投喂
    check(len(frames2) == 4, f"byte-by-byte feed -> {len(frames2)} frames")
    check(frames2[1].payload == struct.pack("<f", 45.5), "split parse keeps payload")

    p3 = P.Parser()
    frames3 = []
    for i in range(0, len(stream), 7):
        frames3 += p3.feed(stream[i:i + 7])       # 7 字节不规则切分
    check(len(frames3) == 4, f"chunk-7 feed -> {len(frames3)} frames")


# ---------------- T4 CRC 错误 ----------------
def t4_crc_error():
    section("T4 corrupt frame detected:")
    bad = bytearray(P.build_frame(P.CMD_SET_TARGET, P.f32(45.0)))
    bad[-1] ^= 0xFF
    p = P.Parser()
    frames = p.feed(bytes(bad))
    check(len(frames) == 0, f"corrupted frame dropped ({len(frames)} emitted)")
    check(p.crc_errors == 1, f"crc_errors={p.crc_errors}")

    # 载荷翻 1 bit 同样应被丢弃
    bad2 = bytearray(P.build_frame(P.CMD_SET_PID, P.f32(1.0) + P.f32(2.0) + P.f32(3.0)))
    bad2[5] ^= 0x01
    p2 = P.Parser()
    check(len(p2.feed(bytes(bad2))) == 0, "payload bitflip dropped")
    check(p2.crc_errors == 1, f"crc_errors={p2.crc_errors}")


# ---------------- T5 长度异常 ----------------
def t5_length_error():
    section("T5 length boundary:")
    # 手工构造 LEN=33（> MAX_PAYLOAD=32）的帧
    raw = bytes([0xAA, 0x55, 0x01, 33]) + bytes(33)
    p = P.Parser()
    frames = p.feed(raw)
    check(len(frames) == 0, "oversized LEN emits nothing")
    check(p.length_errors == 1, f"length_errors={p.length_errors}")

    # 客户端组帧时也应拒绝超长载荷
    try:
        P.build_frame(0x01, bytes(33))
        check(False, "build_frame should raise on 33 bytes")
    except ValueError:
        check(True, "build_frame rejects 33-byte payload")

    # LEN=32 合法上限
    f = P.build_frame(0x08, bytes(32))
    p2 = P.Parser()
    out = p2.feed(f)
    check(len(out) == 1 and len(out[0].payload) == 32, "LEN=32 accepted (upper bound)")


# ---------------- T6 噪声与重同步 ----------------
def t6_junk_and_resync():
    section("T6 junk bytes / resync:")
    stream = b"\x00\x11\xAA\x00\x55\xAA\xA5\x13\xAA" + P.build_frame(P.CMD_START)
    p = P.Parser()
    out = p.feed(stream)
    check(len(out) == 1 and out[0].cmd == P.CMD_START, f"resync after junk -> {len(out)} frame")
    check(p.junk_bytes >= 2, f"junk_bytes={p.junk_bytes}")


# ---------------- T7 状态帧解析 ----------------
def t7_status_decode():
    section("T7 status payload decode:")
    payload = struct.pack("<ffBBBBBfff", 44.7, 45.0, 78, 1, 2, 1, 0, 8.0, 0.1, 1.5)
    check(len(payload) == 25, f"status payload len={len(payload)} == 25")
    st = P.parse_status(payload)
    check(abs(st["temp"] - 44.7) < 0.01, f"temp={st['temp']:.2f}")
    check(abs(st["target"] - 45.0) < 0.01, f"target={st['target']:.2f}")
    check(st["duty"] == 78, f"duty={st['duty']}")
    check(st["fan"] is True, "fan=ON")
    check(st["state"] == "RUN", f"state={st['state']}")
    check(st["sim"] is True, "sim=True (SIM 标识透传)")
    check(st["fault"] == "-", f"fault={st['fault']}")

    # 无效温度哨兵 -999：上位机应能识别
    p2 = struct.pack("<ffBBBBBfff", -999.0, 45.0, 0, 0, 3, 0, 1, 8.0, 0.1, 1.5)
    st2 = P.parse_status(p2)
    check(st2["temp"] == -999.0 and st2["state"] == "FLT" and st2["fault"] == "SENSOR",
          "invalid-temp sentinel decoded as FLT/SENSOR")

    try:
        P.parse_status(b"\x00")
        check(False, "parse_status should reject short payload")
    except ValueError:
        check(True, "parse_status rejects short payload")


# ---------------- T8 参数越界校验 ----------------
def t8_range_validation():
    section("T8 client-side range validation:")
    check(P.validate_target(45.0), "target 45.0 valid")
    check(not P.validate_target(-1.0), "target -1.0 invalid")
    check(not P.validate_target(70.5), "target 70.5 invalid (limit 70)")
    check(P.validate_pid(8.0, 0.1, 1.5), "pid defaults valid")
    check(not P.validate_pid(60.0, 0.1, 1.5), "kp 60 invalid (limit 50)")
    check(not P.validate_pid(8.0, 6.0, 1.5), "ki 6.0 invalid (limit 5)")
    check(not P.validate_pid(8.0, 0.1, 25.0), "kd 25 invalid (limit 20)")

    # NaN 必须被判为非法
    nan = float("nan")
    check(not P.validate_target(nan), "NaN target invalid")
    check(not P.validate_pid(nan, 0.1, 1.5), "NaN kp invalid")


# ---------------- T9 随机模糊测试 ----------------
def t9_fuzz():
    section("T9 fuzz (2000 random streams):")
    rnd = random.Random(20261018)
    exceptions = 0
    decoded = 0
    for _ in range(2000):
        n = rnd.randint(0, 60)
        blob = bytes(rnd.randint(0, 255) for _ in range(n))
        p = P.Parser()
        try:
            decoded += len(p.feed(blob))
        except Exception:                      # noqa: BLE001 - 模糊测试就是要抓异常
            exceptions += 1
    check(exceptions == 0, f"no exceptions in 2000 random streams (decoded={decoded})")


# ---------------- T10 上位机指令向量 ----------------
def t10_command_vectors():
    section("T10 command encoding vectors:")
    check(P.build_simple(P.CMD_START)[2] == 0x03, "START cmd byte 0x03")
    f = P.build_set_target(45.0)
    check(len(f) == 10 and f[3] == 4, "SET_TARGET frame len 10 (payload 4)")
    f = P.build_set_pid(8.0, 0.1, 1.5)
    check(len(f) == 18 and f[3] == 12, "SET_PID frame len 18, payload 12")
    p = P.Parser()
    out = p.feed(f)
    check(len(out) == 1, "SET_PID frame parses back")
    vals = struct.unpack("<fff", out[0].payload)
    check(abs(vals[0] - 8.0) < 1e-6 and abs(vals[2] - 1.5) < 1e-6,
          f"SET_PID payload={vals[0]:.2f}/{vals[1]:.2f}/{vals[2]:.2f}")


def main() -> int:
    lines.append("=== TempCtrl protocol codec tests ===")
    t1_crc()
    t2_frame_layout()
    t3_roundtrip_and_split()
    t4_crc_error()
    t5_length_error()
    t6_junk_and_resync()
    t7_status_decode()
    t8_range_validation()
    t9_fuzz()
    t10_command_vectors()
    lines.append(f"=== {'ALL PASS' if failed == 0 else 'FAILED'}: "
                 f"{checks - failed}/{checks} checks passed ===")
    text = "\n".join(lines)
    print(text)
    with io.open("test_protocol_log.txt", "w", encoding="utf-8") as fh:
        fh.write(text + "\n")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
