/**
  ******************************************************************************
  * @file    heater.h
  * @brief   加热器功率控制：TIM1_CH1 (PA8) 1kHz PWM，占空比 0~100%
  ******************************************************************************
  */
#ifndef __HEATER_H
#define __HEATER_H

#include "main.h"

void    Heater_Init(void);              /* 启动 PWM，初始占空比 0（关闭） */
void    Heater_SetDuty(uint8_t duty);   /* 设置占空比 0~100，超范围自动限幅 */
uint8_t Heater_GetDuty(void);
void    Heater_Off(void);               /* 立即关闭（安全入口，故障时调用） */

#endif /* __HEATER_H */
