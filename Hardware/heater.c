/**
  ******************************************************************************
  * @file    heater.c
  * @brief   加热器驱动：PA8 = TIM1_CH1，1kHz PWM 调功
  *
  * 安全约束：
  *   - 上电默认占空比 0（tim.c 中 CCR1=0，本文件 Init 不改变它）
  *   - 所有占空比写入都经过 0~100 限幅
  *   - Heater_Off() 是故障保护的唯一入口，任何模块发现异常都应直接调它
  *
  * 警告：真实加热器接入前，必须先用 LED 假负载验证 PA8 波形（阶段 3 验收）。
  ******************************************************************************
  */
#include "heater.h"
#include "tim.h"

static uint8_t s_duty = 0;   /* 当前占空比百分比，唯一事实来源 */

void Heater_Init(void)
{
  s_duty = 0;
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
}

void Heater_SetDuty(uint8_t duty)
{
  if (duty > 100u)
  {
    duty = 100u;              /* 限幅：调用方传错也不能超过 100% */
  }
  s_duty = duty;
  /* ARR=999，duty% -> CCR1 = duty * 10 */
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, (uint32_t)duty * 10u);
}

uint8_t Heater_GetDuty(void)
{
  return s_duty;
}

void Heater_Off(void)
{
  Heater_SetDuty(0);
}
