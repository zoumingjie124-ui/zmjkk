/**
  ******************************************************************************
  * @file    sensor.h
  * @brief   温度采集统一接口：真实硬件（NTC/ADC1_IN0）与软件模拟双底层
  *
  * 上层（控制/PID/显示）只允许调用 Sensor_Read()，不允许直接碰 ADC。
  * 数据结构里的 is_simulated 标志是全工程的"诚信开关"：
  * 模拟数据必须带着这个标志走完显示/通信全链路，绝不伪装成真实测量。
  ******************************************************************************
  */
#ifndef __SENSOR_H
#define __SENSOR_H

#include "main.h"

typedef struct
{
  float   temperature_c;   /* 温度，单位 ℃ */
  float   humidity_rh;     /* 湿度 %RH；NTC 无湿度功能，固定为 -1.0f（显示层应显示 "--"） */
  uint8_t is_simulated;    /* 1 = 软件模拟数据源；0 = 真实 ADC 采样 */
  uint8_t is_valid;        /* 0 = 本次读数无效（如 NTC 开路/短路），上层必须按故障处理 */
} Sensor_Data_t;

void          Sensor_Init(void);
Sensor_Data_t Sensor_Read(void);

/* 强制进入/退出模拟模式（调试入口；正常流程由 Sensor_Init 自动检测决定） */
void          Sensor_ForceSimulation(uint8_t enable);
uint8_t       Sensor_IsSimulated(void);

#endif /* __SENSOR_H */
