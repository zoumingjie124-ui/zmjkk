/**
  ******************************************************************************
  * @file    fan.h
  * @brief   风扇开关控制：PB0 推挽输出（第一版只做开关，调速预留 TIM3_CH3）
  ******************************************************************************
  */
#ifndef __FAN_H
#define __FAN_H

#include "main.h"

void    Fan_Init(void);       /* 确保上电为关 */
void    Fan_On(void);
void    Fan_Off(void);
void    Fan_Set(uint8_t on);  /* 0=关，非 0=开 */
uint8_t Fan_IsOn(void);

#endif /* __FAN_H */
