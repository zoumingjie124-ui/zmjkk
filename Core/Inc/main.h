/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : 智能温控与故障监测终端 —— 公共头文件
  *                   集中定义引脚映射，全工程统一从这里取引脚号。
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

void Error_Handler(void);

/* ---------------- 引脚映射（与 docs 中硬件方案一致，改动前先更新文档） -------- */

/* OLED：软件模拟 I2C（第一阶段方案，PB6/PB7 开漏输出） */
#define OLED_SCL_Pin        GPIO_PIN_6
#define OLED_SCL_GPIO_Port  GPIOB
#define OLED_SDA_Pin        GPIO_PIN_7
#define OLED_SDA_GPIO_Port  GPIOB

/* 加热器：第一阶段先配成普通推挽输出并保持低电平（安全默认关闭），
   第三阶段切换到 TIM1_CH1 复用功能输出 PWM */
#define HEATER_Pin          GPIO_PIN_8
#define HEATER_GPIO_Port    GPIOA

/* 风扇：PB0 普通推挽输出开关（后续版本可切 TIM3_CH3 调速，引脚不变） */
#define FAN_Pin             GPIO_PIN_0
#define FAN_GPIO_Port       GPIOB

/* 按键 ×3：按下接地，内部上拉（接法待用户确认，若不是对地需改 gpio.c） */
#define KEY1_Pin            GPIO_PIN_12
#define KEY1_GPIO_Port      GPIOB
#define KEY2_Pin            GPIO_PIN_13
#define KEY2_GPIO_Port      GPIOB
#define KEY3_Pin            GPIO_PIN_14
#define KEY3_GPIO_Port      GPIOB

/* 保留：PA13/PA14 = SWD（ST-Link），禁止挪用；
   预留：PA9/PA10 = USART1（第八阶段上位机通信） */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
