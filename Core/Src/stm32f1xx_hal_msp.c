/**
  ******************************************************************************
  * @file    stm32f1xx_hal_msp.c
  * @brief   MSP：外设时钟、引脚复用、NVIC 使能集中在这里。
  *          "外设配置"(tim.c/usart.c) 与"板级资源分配"(本文件) 分离，
  *          换板子只改 MSP。第一阶段无外设 MSP，只有全局初始化。
  ******************************************************************************
  */
#include "main.h"

void HAL_MspInit(void)
{
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();

  /* 关闭 JTAG、保留 SWD：释放 PB3/PB4/PA15，同时不影响 ST-Link 下载调试 */
  __HAL_AFIO_REMAP_SWJ_NOJTAG();
}

/* ------------------------------- ADC1 -------------------------------- */
void HAL_ADC_MspInit(ADC_HandleTypeDef *hadc)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (hadc->Instance == ADC1)
  {
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA0 = ADC1_IN0：NTC 分压采样，必须为模拟输入（无上下拉） */
    GPIO_InitStruct.Pin  = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
}

/* ------------------------------- TIM1 -------------------------------- */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (htim->Instance == TIM1)
  {
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA8 = TIM1_CH1：复用推挽，接管 gpio.c 中的普通输出配置。
       CCR1=0 时输出低电平，接管瞬间加热器仍保持关闭 */
    GPIO_InitStruct.Pin   = GPIO_PIN_8;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
  }
}
