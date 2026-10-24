/**
  ******************************************************************************
  * @file    sensor.h
  * @brief   温度采集统一接口：真实硬件（NTC/ADC1_IN0）与软件模拟双底层
  *
  * ============================ 安全约束（第二轮审查） ========================
  * 模拟模式 **不是** 运行时自动回退，而是编译期显式选择：
  *
  *   SENSOR_SIM_ENABLE = 0（默认，发布/上板配置）
  *       - 只有真实 NTC 通道；读数无效一律 is_valid=0
  *       - 控制层见到 invalid 直接 FAULT 锁定，绝不切换数据源继续运行
  *
  *   SENSOR_SIM_ENABLE = 1（仅台架算法验证）
  *       - 用一阶惯性模型产生温度；is_simulated 恒为 1
  *       - 控制层在模拟模式下**强制关闭加热器硬件输出闸门**（PA8 恒低）
  *
  * 上层（控制/PID/显示/通信）只允许调用 Sensor_Read()，不得直接碰 ADC；
  * is_simulated 是"诚信开关"，必须原样透传到 OLED 与上位机。
  ******************************************************************************
  */
#ifndef __SENSOR_H
#define __SENSOR_H

#include "main.h"

/* 编译期模式开关：0 = 只用真实 NTC（默认）；1 = 台架模拟数据源 */
#ifndef SENSOR_SIM_ENABLE
#define SENSOR_SIM_ENABLE   0
#endif

typedef struct
{
  float   temperature_c;   /* 温度 ℃ */
  float   humidity_rh;     /* 湿度 %RH；NTC 无湿度功能，固定 -1.0f */
  uint8_t is_simulated;    /* 1 = 软件模拟数据源；0 = 真实 ADC */
  uint8_t is_valid;        /* 0 = 本次读数无效（开路/短路/超范围/ADC 错） */
} Sensor_Data_t;

void          Sensor_Init(void);
Sensor_Data_t Sensor_Read(void);
uint8_t       Sensor_IsSimulated(void);

#endif /* __SENSOR_H */
