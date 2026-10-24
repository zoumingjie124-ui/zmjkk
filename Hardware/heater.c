/**
  ******************************************************************************
  * @file    heater.c
  * @brief   加热器驱动：PA8 = TIM1_CH1，1kHz PWM 调功 + 硬件输出总闸
  *
  * 安全约束：
  *   - 上电默认：逻辑占空比 0、**输出闸门关闭**（CCR1 = 0）
  *   - 闸门关闭时，Caller 传任何占空比都只更新逻辑值，PA8 保持低电平
  *   - 所有占空比写入都经过 0~100 限幅
  *   - Heater_Off() 是故障保护的统一入口：清零逻辑值 + 关闸
  ******************************************************************************
  */
#include "heater.h"
#include "tim.h"

static uint8_t s_duty  = 0;   /* 逻辑占空比（PID 想要的功率） */
static uint8_t s_hw_en = 0;   /* 硬件输出闸门：0=强制关断（上电默认） */

/* 把"逻辑占空比 + 闸门状态"落到定时器：闸门关 -> CCR 恒 0 */
static void hw_apply(void)
{
  uint32_t ccr = 0u;

  if (s_hw_en)
  {
    ccr = (uint32_t)s_duty * 10u;   /* ARR=999：1% = 10 个计数 */
  }
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr);
}

void Heater_Init(void)
{
  s_duty  = 0u;
  s_hw_en = 0u;                     /* 上电绝不输出 */
  hw_apply();
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
}

void Heater_SetDuty(uint8_t duty)
{
  if (duty > 100u)
  {
    duty = 100u;                    /* 限幅：调用方传错也不能超过 100% */
  }
  s_duty = duty;
  hw_apply();
}

uint8_t Heater_GetDuty(void)
{
  return s_duty;
}

void Heater_EnableOutput(uint8_t enable)
{
  s_hw_en = (enable != 0u) ? 1u : 0u;
  hw_apply();
}

uint8_t Heater_OutputEnabled(void)
{
  return s_hw_en;
}

void Heater_Off(void)
{
  s_duty  = 0u;
  s_hw_en = 0u;
  hw_apply();
}
