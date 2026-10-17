/**
  ******************************************************************************
  * @file    comm.h
  * @brief   上位机通信协议（USART1）：非阻塞帧解析 + 定时状态上报
  *
  * 帧格式（多字节均小端）：
  *   0xAA 0x55 | CMD | LEN | PAYLOAD[LEN] | CRC16_L | CRC16_H
  *   CRC16 = CRC-16/CCITT-FALSE（poly 0x1021, init 0xFFFF, 不反射）
  *   校验范围 = CMD..PAYLOAD 末字节
  *
  * 命令字：
  *   0x01 GET_STATUS  -> 0x81 STATUS(25B)
  *   0x02 SET_TARGET  payload: f32        -> 0x82 ACK(1B status)
  *   0x03 START                            -> 0x83 ACK
  *   0x04 STOP                             -> 0x84 ACK
  *   0x05 ACK_FAULT                        -> 0x85 ACK
  *   0x06 SET_PID     payload: f32 x3     -> 0x86 ACK
  *   0x07 SAVE_PARAMS                      -> 0x87 ACK
  *   0x08 GET_PARAMS                       -> 0x88 PARAMS(16B: target,kp,ki,kd)
  *   0x7F NACK  payload: 1 字节错误码（1=CRC错 2=长度错 3=参数越界 4=状态不允许）
  *
  * STATUS 载荷（25B）：
  *   f32 temp | f32 target | u8 duty | u8 fan | u8 state | u8 sim | u8 fault
  *   | f32 kp | f32 ki | f32 kd
  ******************************************************************************
  */
#ifndef __COMM_H
#define __COMM_H

#include "main.h"

void Comm_Init(void);
void Comm_Tick(void);          /* 建议 10ms 调用一次（解析 + 定时上报） */

#endif /* __COMM_H */
