"""帧协议编解码（与固件 App/Src/comm.c 一一对应）。

帧格式（多字节小端）:
    0xAA 0x55 | CMD | LEN | PAYLOAD[LEN] | CRC16_L | CRC16_H
    CRC16 = CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, 不反射)
    校验范围 = CMD..PAYLOAD 末字节

本模块只做纯逻辑，不依赖 pyserial，可被自动化测试直接导入。
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

H1 = 0xAA
H2 = 0x55
MAX_PAYLOAD = 32

# 命令字
CMD_GET_STATUS = 0x01
CMD_SET_TARGET = 0x02
CMD_START = 0x03
CMD_STOP = 0x04
CMD_ACK_FAULT = 0x05
CMD_SET_PID = 0x06
CMD_SAVE_PARAMS = 0x07
CMD_GET_PARAMS = 0x08
CMD_NACK = 0x7F
RESP_FLAG = 0x80

# 状态码
ACK_OK = 0
ACK_CRC_ERR = 1
ACK_LEN_ERR = 2
ACK_RANGE_ERR = 3
ACK_STATE_ERR = 4
ACK_FLASH_ERR = 5      # Flash 擦除/写入/回读失败（第二轮审查新增）
ACK_SENSOR_ERR = 6     # 传感器无效，拒绝启动 / 无法确认故障（新增）

ACK_TEXT = {
    ACK_OK: "OK",
    ACK_CRC_ERR: "CRC error",
    ACK_LEN_ERR: "length/protocol error",
    ACK_RANGE_ERR: "value out of range",
    ACK_STATE_ERR: "state not allowed",
    ACK_FLASH_ERR: "flash save failed",
    ACK_SENSOR_ERR: "sensor invalid",
}

STATE_NAMES = {0: "INI", 1: "STB", 2: "RUN", 3: "FLT"}
FAULT_NAMES = {0: "-", 1: "SENSOR", 2: "OVERTEMP", 3: "RUNAWAY"}

# 参数合法范围（与固件 control.h 保持一致）
TARGET_MIN, TARGET_MAX = 0.0, 70.0
KP_MIN, KP_MAX = 0.0, 50.0
KI_MIN, KI_MAX = 0.0, 5.0
KD_MIN, KD_MAX = 0.0, 20.0


def crc16_ccitt(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE，与固件 comm.c 的 crc16_ccitt 等价。"""
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(cmd: int, payload: bytes = b"") -> bytes:
    """组帧。长度非法时抛 ValueError（避免发出固件无法解析的帧）。"""
    if not 0 <= cmd <= 0xFF:
        raise ValueError("cmd out of range")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    body = bytes([cmd, len(payload)]) + payload
    crc = crc16_ccitt(body)
    return bytes([H1, H2]) + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def f32(value: float) -> bytes:
    return struct.pack("<f", value)


def build_set_target(target_c: float) -> bytes:
    return build_frame(CMD_SET_TARGET, f32(target_c))


def build_set_pid(kp: float, ki: float, kd: float) -> bytes:
    return build_frame(CMD_SET_PID, f32(kp) + f32(ki) + f32(kd))


def build_simple(cmd: int) -> bytes:
    return build_frame(cmd, b"")


def validate_target(target_c: float) -> bool:
    return TARGET_MIN <= target_c <= TARGET_MAX


def validate_pid(kp: float, ki: float, kd: float) -> bool:
    return (KP_MIN <= kp <= KP_MAX) and (KI_MIN <= ki <= KI_MAX) and (KD_MIN <= kd <= KD_MAX)


@dataclass
class Frame:
    cmd: int
    payload: bytes
    raw: bytes = b""

    @property
    def is_response(self) -> bool:
        return bool(self.cmd & RESP_FLAG)


STATUS_STRUCT = struct.Struct("<ffBBBBBfff")  # 25 字节
PARAMS_STRUCT = struct.Struct("<ffff")        # 16 字节


def parse_status(payload: bytes) -> dict:
    """解析 0x81 状态帧载荷。"""
    if len(payload) != STATUS_STRUCT.size:
        raise ValueError(f"status payload must be {STATUS_STRUCT.size} bytes, got {len(payload)}")
    temp, target, duty, fan, state, sim, fault, kp, ki, kd = STATUS_STRUCT.unpack(payload)
    return {
        "temp": temp,
        "target": target,
        "duty": duty,
        "fan": bool(fan),
        "state": STATE_NAMES.get(state, "?"),
        "state_code": state,
        "sim": bool(sim),
        "fault": FAULT_NAMES.get(fault, "?"),
        "fault_code": fault,
        "kp": kp,
        "ki": ki,
        "kd": kd,
    }


def parse_params(payload: bytes) -> dict:
    if len(payload) != PARAMS_STRUCT.size:
        raise ValueError(f"params payload must be {PARAMS_STRUCT.size} bytes")
    target, kp, ki, kd = PARAMS_STRUCT.unpack(payload)
    return {"target": target, "kp": kp, "ki": ki, "kd": kd}


class Parser:
    """非阻塞字节流解析器（与固件解析状态机同构）。

    feed() 可接受任意切分的字节块；返回本次解析出的完整帧列表。
    错误计数：crc_errors / length_errors / junk_bytes。
    """

    def __init__(self) -> None:
        self._state = "H1"
        self._cmd = 0
        self._len = 0
        self._buf = bytearray()
        self.crc_errors = 0
        self.length_errors = 0
        self.junk_bytes = 0
        self.frames_ok = 0

    def reset(self) -> None:
        self._state = "H1"
        self._buf.clear()

    def feed(self, data: bytes) -> list[Frame]:
        out: list[Frame] = []
        for b in data:
            state = self._state
            if state == "H1":
                if b == H1:
                    self._state = "H2"
                else:
                    self.junk_bytes += 1
            elif state == "H2":
                if b == H2:
                    self._state = "CMD"
                elif b == H1:
                    self._state = "H2"
                    self.junk_bytes += 1
                else:
                    self._state = "H1"
                    self.junk_bytes += 1
            elif state == "CMD":
                self._cmd = b
                self._state = "LEN"
            elif state == "LEN":
                self._len = b
                self._buf.clear()
                if self._len > MAX_PAYLOAD:
                    self.length_errors += 1
                    self._state = "H1"
                else:
                    self._state = "PAYLOAD" if self._len else "CRC_L"
            elif state == "PAYLOAD":
                self._buf.append(b)
                if len(self._buf) >= self._len:
                    self._state = "CRC_L"
            elif state == "CRC_L":
                self._crc = b
                self._state = "CRC_H"
            elif state == "CRC_H":
                crc_rx = self._crc | (b << 8)
                body = bytes([self._cmd, self._len]) + bytes(self._buf)
                if crc16_ccitt(body) != crc_rx:
                    self.crc_errors += 1
                else:
                    self.frames_ok += 1
                    out.append(Frame(self._cmd, bytes(self._buf), body))
                self._state = "H1"
            else:
                self._state = "H1"
        return out
