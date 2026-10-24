/**
  ******************************************************************************
  * @file    heater.h
  * @brief   加热器功率控制：TIM1_CH1 (PA8) 1kHz PWM，占空比 0~100%
  *
  * 安全设计（第二轮审查新增）：软件输出许可（闸门）。
  *   - 上电默认 **关闸**（s_hw_en = 0）：任何 SetDuty 只记录逻辑值，PA8 恒低
  *   - 只有 Control 在「真实传感器 + RUNNING」时才调用 Heater_EnableOutput(1)
  *   - Heater_Off() 同时清零逻辑占空比并关闸（故障路径必用）
  *   这样即使上层逻辑写错，模拟数据也绝无可能驱动真实加热器。
  *
  * 边界说明（重要）：
  *   这里的"许可"是**软件变量**，不是物理断电开关。它能防逻辑错误与模拟数据误驱动，
  *   但无法防：定时器配置被改错、MCU 引脚损坏、MOSFET 击穿、驱动电路故障。
  *   真正的硬件保护在硬件侧，与本文件无关：
  *     - MOSFET 栅极 10kΩ 下拉电阻（复位/掉电/浮空时默认关断）
  *     - 独立常闭温度开关串联在加热器供电回路（器件级断流）
  *     - 经过验证的断电路径（拔掉负载电源）
  *   SIM 模式下 PA8 是否真为低，需按 docs/PREBOARD_TEST_PLAN.md P1-10 用示波器实测。
  ******************************************************************************
  */
#ifndef __HEATER_H
#define __HEATER_H

#include "main.h"

void    Heater_Init(void);              /* 启动 PWM，占空比 0 且**输出闸门关闭** */
void    Heater_SetDuty(uint8_t duty);   /* 逻辑占空比 0~100（是否上引脚取决于闸门） */
uint8_t Heater_GetDuty(void);           /* 逻辑占空比（模拟模型据此演化温度） */

void    Heater_EnableOutput(uint8_t enable);  /* 软件输出许可（闸门）：0=强制 PA8 低 */
uint8_t Heater_OutputEnabled(void);

void    Heater_Off(void);               /* 逻辑归零 + 关闸（安全入口） */

/**
  * @brief 致命异常专用的"最后关断"（裸寄存器实现，不调用 HAL、不依赖外设状态）
  *
  * 为什么必须单独一个函数：PA8 处于 AF 模式时，向 GPIOA->BRR 写 1 **不会**
  * 改变引脚电平——引脚由 TIM1_CH1 驱动，GPIO 输出寄存器被旁路。
  * 且 MOE=0 之后引脚状态还受 BDTR.OSSI / CR2.OIS1 影响（本工程未配置 OSSI，
  * 不能假设一定为低）。
  *
  * 因此这里显式做三件事：
  *   1. TIM1：MOE=0（关主输出）+ CEN=0（停计数）
  *   2. PA8：由"复用推挽"切回"通用推挽输出"（GPIOA->CRH 单次读改写）
  *   3. 驱动 PA8 为低，并顺带把 PB0 置高开风扇
  *
  * 调用约束：可在任意上下文调用（含 HardFault），内部自行开启 GPIOA/B 时钟。
  * 若在 GPIO 初始化之前调用，引脚本就不是 AF 模式，外部 10k 下拉同样保证关断。
  */
void    Heater_PanicOff(void);

#endif /* __HEATER_H */
