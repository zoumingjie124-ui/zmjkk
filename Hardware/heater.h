/**
  ******************************************************************************
  * @file    heater.h
  * @brief   加热器功率控制：TIM1_CH1 (PA8) 1kHz PWM，占空比 0~100%
  *
  * 安全设计（第二轮审查新增）：硬件输出总闸。
  *   - 上电默认 **关闸**（s_hw_en = 0）：任何 SetDuty 只记录逻辑值，PA8 恒低
  *   - 只有 Control 在「真实传感器 + RUNNING」时才调用 Heater_EnableOutput(1)
  *   - Heater_Off() 同时清零逻辑占空比并关闸（故障路径必用）
  *   这样即使上层逻辑写错，模拟数据也绝无可能驱动真实加热器。
  ******************************************************************************
  */
#ifndef __HEATER_H
#define __HEATER_H

#include "main.h"

void    Heater_Init(void);              /* 启动 PWM，占空比 0 且**输出闸门关闭** */
void    Heater_SetDuty(uint8_t duty);   /* 逻辑占空比 0~100（是否上引脚取决于闸门） */
uint8_t Heater_GetDuty(void);           /* 逻辑占空比（模拟模型据此演化温度） */

void    Heater_EnableOutput(uint8_t enable);  /* 硬件输出总闸：0=强制 PA8 低 */
uint8_t Heater_OutputEnabled(void);

void    Heater_Off(void);               /* 逻辑归零 + 关闸（安全入口） */

#endif /* __HEATER_H */
