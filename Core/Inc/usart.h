/**
  ******************************************************************************
  * @file    usart.h
  * @brief   USART1 (PA9/PA10, 115200 8N1)：中断接收 + 环形缓冲 + 阻塞发送
  ******************************************************************************
  */
#ifndef __USART_H
#define __USART_H

#include "main.h"

extern UART_HandleTypeDef huart1;

void    MX_USART1_UART_Init(void);
void    Usart_StartRx(void);
uint8_t Usart_ReadByte(uint8_t *b);                 /* 1 = 取到字节 */
void    Usart_Send(const uint8_t *data, uint16_t len);
uint32_t Usart_OverflowCount(void);                 /* 环形缓冲溢出计数（诊断用） */

#endif /* __USART_H */
