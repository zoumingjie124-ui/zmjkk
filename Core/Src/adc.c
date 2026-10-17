/**
  ******************************************************************************
  * @file    adc.c
  * @brief   ADC1 配置：单次转换、软件触发、12 位、通道 0 (PA0)
  *
  * 用途：NTC 热敏电阻分压采样（真实传感器底层，sensor.c 调用）。
  * 引脚 PA0 配置在 stm32f1xx_hal_msp.c 的 HAL_ADC_MspInit 中（模拟输入）。
  ******************************************************************************
  */
#include "adc.h"

ADC_HandleTypeDef hadc1;

void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance                   = ADC1;
  hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;    /* 单通道 */
  hadc1.Init.ContinuousConvMode    = DISABLE;             /* 单次转换，按需触发 */
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;  /* 软件触发 */
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion       = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /* 通道 0，采样时间 55.5 周期：分压电路阻抗不高，这个值兼顾速度与精度 */
  sConfig.Channel      = ADC_CHANNEL_0;
  sConfig.Rank         = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
}
