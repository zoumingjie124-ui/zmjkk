/**
  ******************************************************************************
  * @file    control.h
  * @brief   控制状态机：INIT/STANDBY/RUNNING/FAULT + 安全联锁
  ******************************************************************************
  */
#ifndef __CONTROL_H
#define __CONTROL_H

#include "main.h"

/* ---------------- 可调参数（集中定义，改这里不要散落魔法数字） ---------- */
#define CTRL_TARGET_DEFAULT_C   45.0f    /* 测试目标温度（阶段6 按键可改） */
#define CTRL_TARGET_MIN_C       0.0f
#define CTRL_TARGET_MAX_C       70.0f    /* 目标值上限必须低于安全上限 */
#define CTRL_SAFE_LIMIT_C       75.0f    /* 硬安全上限：超过立即 FAULT */
#define CTRL_FAN_ON_DELTA_C     2.0f     /* 高于目标 +2.0℃ 开风扇 */
#define CTRL_FAN_OFF_DELTA_C    0.5f     /* 回落到目标 +0.5℃ 以内关风扇 */

/* PID 参数（初值，阶段 5 真实闭环时按升温曲线整定） */
#define CTRL_PID_KP             8.0f
#define CTRL_PID_KI             0.10f
#define CTRL_PID_KD             1.5f
#define CTRL_PID_TS_S           0.5f     /* 500ms 控制周期 */

typedef enum
{
  CTRL_INIT = 0,     /* 上电自检 */
  CTRL_STANDBY,      /* 待机：加热关 */
  CTRL_RUNNING,      /* 闭环运行 */
  CTRL_FAULT         /* 故障锁定：必须人工确认解除 */
} ctrl_state_t;

typedef enum
{
  CTRL_FAULT_NONE = 0,
  CTRL_FAULT_SENSOR,     /* 传感器失联 / 读数无效 / PID 输入异常 */
  CTRL_FAULT_OVERTEMP    /* 超过安全上限 */
} ctrl_fault_t;

typedef struct
{
  ctrl_state_t state;
  ctrl_fault_t fault;
  float        target_c;       /* 当前目标温度 */
  float        temp_c;         /* 最近一次测量温度 */
  uint8_t      temp_valid;     /* 测量是否有效 */
  uint8_t      duty;           /* 当前加热占空比 */
  uint8_t      fan_on;         /* 风扇状态 */
  uint8_t      is_simulated;   /* 数据源标识（透传 sensor） */
} ctrl_snapshot_t;

void Control_Init(void);
void Control_Tick(void);               /* 严格每 500ms 调用一次 */
const ctrl_snapshot_t *Control_Get(void);
void Control_SetTarget(float target_c);
void Control_AckFault(void);           /* 故障人工确认（阶段6 接按键） */
const char *Control_StateName(void);   /* "INI"/"STB"/"RUN"/"FLT" */

#endif /* __CONTROL_H */
