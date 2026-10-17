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

/* ------------------------------- USART1 ------------------------------ */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (huart->Instance == USART1)
  {
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* PA9 = TX 复用推挽；PA10 = RX 浮空输入（由 USB-TTL 的 TX 驱动） */
    GPIO_InitStruct.Pin   = GPIO_PIN_9;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin  = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 优先级 3（最低）：通讯绝不抢占 1ms 时基与安全逻辑 */
    HAL_NVIC_SetPriority(USART1_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  }
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
