/**
  ******************************************************************************
  * @file    adc.h
  * @brief   ADC1 初始化：通道 0 (PA0)，用于 NTC 热敏电阻分压采样
  ******************************************************************************
  */
#ifndef __ADC_H
#define __ADC_H

#include "main.h"

extern ADC_HandleTypeDef hadc1;

void MX_ADC1_Init(void);

#endif /* __ADC_H */
