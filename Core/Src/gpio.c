/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   GPIO 初始化
  *
  * 安全原则：加热器(PA8) 和风扇(PB0) 必须在"配置为输出之前"先写入低电平，
  *           保证从上电到初始化完成的整个过程中负载始终处于关闭状态。
  *           真实负载接入前，这两个引脚只接 LED 假负载或悬空。
  ******************************************************************************
  */
#include "gpio.h"

void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* 第一步：先写低电平，再配置为输出 —— 防止配置瞬间输出不确定电平 */
  HAL_GPIO_WritePin(HEATER_GPIO_Port, HEATER_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(FAN_GPIO_Port,    FAN_Pin,    GPIO_PIN_RESET);

  /* 第二步：OLED 空闲时 I2C 两线都为高（开漏 + 外部/内部上拉） */
  HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_SET);

  /* 加热器 PA8：推挽输出低电平（第三阶段改为 TIM1_CH1 复用推挽） */
  GPIO_InitStruct.Pin   = HEATER_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HEATER_GPIO_Port, &GPIO_InitStruct);

  /* 风扇 PB0：推挽输出低电平 */
  GPIO_InitStruct.Pin   = FAN_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(FAN_GPIO_Port, &GPIO_InitStruct);

  /* OLED 软件 I2C：开漏输出（等同课程例程的 GPIO_Mode_Out_OD） */
  GPIO_InitStruct.Pin   = OLED_SCL_Pin | OLED_SDA_Pin;
  GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull  = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* 按键 PB12/13/14：内部上拉输入，按下接地（接法待确认） */
  GPIO_InitStruct.Pin  = KEY1_Pin | KEY2_Pin | KEY3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
