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
