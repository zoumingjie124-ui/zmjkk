/**
  ******************************************************************************
  * @file    fan.c
  * @brief   风扇驱动：PB0 高低电平开关（经 MOSFET 低边驱动，禁止直接带载）
  *
  * 后续版本：PB0 可切换为 TIM3_CH3 复用功能做 PWM 调速，引脚不用改。
  ******************************************************************************
  */
#include "fan.h"

void Fan_Init(void)
{
  Fan_Off();                  /* 上电默认关闭 */
}

void Fan_On(void)
{
  HAL_GPIO_WritePin(FAN_GPIO_Port, FAN_Pin, GPIO_PIN_SET);
}

void Fan_Off(void)
{
  HAL_GPIO_WritePin(FAN_GPIO_Port, FAN_Pin, GPIO_PIN_RESET);
}

void Fan_Set(uint8_t on)
{
  if (on)
  {
    Fan_On();
  }
  else
  {
    Fan_Off();
  }
}

uint8_t Fan_IsOn(void)
{
  /* 读输出寄存器，而不是另存标志位 —— 引脚状态即事实 */
  return (HAL_GPIO_ReadPin(FAN_GPIO_Port, FAN_Pin) == GPIO_PIN_SET) ? 1u : 0u;
}
