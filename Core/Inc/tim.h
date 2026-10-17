/**
  ******************************************************************************
  * @file    tim.h
  * @brief   TIM1 初始化：CH1 (PA8) PWM 输出，1kHz，加热器功率控制用
  ******************************************************************************
  */
#ifndef __TIM_H
#define __TIM_H

#include "main.h"

extern TIM_HandleTypeDef htim1;

void MX_TIM1_Init(void);

#endif /* __TIM_H */
