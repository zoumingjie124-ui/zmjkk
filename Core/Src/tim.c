/**
  ******************************************************************************
  * @file    tim.c
  * @brief   TIM1 CH1 PWM：1kHz、占空比 0~100%（加热器功率控制）
  *
  * 定时器数学：
  *   TIM1 挂在 APB2(72MHz)，且 APB2 不分频时定时器时钟 = 72MHz
  *   PSC = 71  -> 计数频率 72MHz/72 = 1MHz（1us 一格）
  *   ARR = 999 -> PWM 周期 1000us = 1kHz
  *   CCR1 = 0~1000 对应占空比 0~100%（heater.c 中 duty*10 换算）
  *
  * 安全：初始化时 CCR1 = 0，PWM1 模式下输出恒为低电平 —— 上电默认关闭加热。
  * 注意：TIM1 是高级定时器，必须置 MOE 才有输出（HAL_TIM_PWM_Start 内部已做）。
  ******************************************************************************
  */
#include "tim.h"

TIM_HandleTypeDef htim1;

void MX_TIM1_Init(void)
{
  TIM_OC_InitTypeDef sConfigOC = {0};

  htim1.Instance               = TIM1;
  htim1.Init.Prescaler         = 71;
  htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim1.Init.Period            = 999;
  htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }

  sConfigOC.OCMode       = TIM_OCMODE_PWM1;
  sConfigOC.Pulse        = 0;                       /* 上电占空比 0：加热关闭 */
  sConfigOC.OCPolarity   = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode   = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState  = TIM_OCIDLESTATE_RESET;   /* 空闲态也是低电平 */
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
}
