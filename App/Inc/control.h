/**
  ******************************************************************************
  * @file    control.h
  * @brief   控制状态机：INIT/STANDBY/RUNNING/FAULT + 安全联锁 + 运行时参数
  ******************************************************************************
  */
#ifndef __CONTROL_H
#define __CONTROL_H

#include "main.h"

/* ---------------- 默认参数（首次上电 / Flash 校验失败时使用） -------------- */
#define CTRL_TARGET_DEFAULT_C   45.0f
#define CTRL_KP_DEFAULT         8.0f
#define CTRL_KI_DEFAULT         0.10f
#define CTRL_KD_DEFAULT         1.5f

/* ---------------- 参数合法范围（超出即拒收，供通信层回报错误） ------------ */
#define CTRL_TARGET_MIN_C       0.0f
#define CTRL_TARGET_MAX_C       70.0f    /* 必须低于安全上限 */
#define CTRL_KP_MIN             0.0f
#define CTRL_KP_MAX             50.0f
#define CTRL_KI_MIN             0.0f
#define CTRL_KI_MAX             5.0f
#define CTRL_KD_MIN             0.0f
#define CTRL_KD_MAX             20.0f

/* ---------------- 联锁与风扇阈值 ---------------- */
#define CTRL_SAFE_LIMIT_C       75.0f    /* 硬安全上限：超过立即 FAULT */
#define CTRL_FAN_ON_DELTA_C     2.0f     /* 运行期：T > target+2.0 开 */
#define CTRL_FAN_OFF_DELTA_C    0.5f     /* 运行期：T < target+0.5 关 */
#define CTRL_PID_TS_S           0.5f     /* 500ms 控制周期 */

/* ---------------- 停机余热管理（STANDBY）---------------------------------
   目标：STOP/确认故障后把加热器余热吹掉，别让余热把温度顶到 75℃ 联锁
        （那样 FAULT 就分不清是故障还是人为停机）。
   两个边界（都是"别让待机吹风变成意外行为"）：
     1) 判据用**绝对温度**，不再用 target+2。目标设得比室温低时
        （夏天设 20℃、室温 28℃），target+2 会让风扇在待机状态永久转。
     2) 只在**刚停机的有限窗口**内做，窗口之外待机一律不吹风。
   窗口长度按余热衰减时间取：180s。窗口内若温度仍高于 ON 阈值继续吹，
   到期即停——此时温度已低于 75℃ 联锁很远，继续闷着不构成危险。 */
#define CTRL_COOLDOWN_ON_C      40.0f    /* 高于此温度才吹（机箱内偏热） */
#define CTRL_COOLDOWN_OFF_C     38.0f    /* 回落 2℃ 迟滞关，避免频繁启停 */
#define CTRL_COOLDOWN_TICKS     360u     /* 180s @ 500ms */

/* ---------------- 热失控 / 传感器脱落判据（仅 RUNNING 时检查） -------------
   现象：加热输出很高，且离目标还远，但温度几乎不涨 —— 典型原因是 NTC 脱落、
   读数停在室温（75℃ 绝对上限永远触发不了）、或加热回路断开。
   判据：占空比 ≥ 80% 且 (目标-温度) > 3℃ 连续 60s，期间温升 < 2℃ -> FAULT。
   这些阈值与被控对象有关（热容越大越要放宽），真机整定时按实测升温曲线调整。 */
#define CTRL_RUNAWAY_DUTY_PERMILLE   800u
#define CTRL_RUNAWAY_MIN_ERR_C       3.0f
#define CTRL_RUNAWAY_WINDOW_TICKS    120u    /* 120 x 0.5s = 60s */
#define CTRL_RUNAWAY_MIN_RISE_C      2.0f
/* 条件不成立的 tick 如何衰减"证据"：漏桶式，每个失败 tick 减 1，不瞬间清零。
   原因：大热容系统升温慢，若占空比偶尔跌破 80%（一个 tick），清零会让窗口从头
   计时，热失控可能一直测不出来。衰减 1 只削弱一次证据，误报风险不增加。 */
#define CTRL_RUNAWAY_DECAY_TICKS     1u

typedef enum
{
  CTRL_INIT = 0,
  CTRL_STANDBY,
  CTRL_RUNNING,
  CTRL_FAULT
} ctrl_state_t;

typedef enum
{
  CTRL_FAULT_NONE = 0,
  CTRL_FAULT_SENSOR,
  CTRL_FAULT_OVERTEMP,
  CTRL_FAULT_RUNAWAY          /* 高功率输出但温度不涨：传感器脱落 / 加热回路异常 */
} ctrl_fault_t;

typedef struct
{
  float target_c;
  float kp;
  float ki;
  float kd;
} ctrl_params_t;

typedef struct
{
  ctrl_state_t state;
  ctrl_fault_t fault;
  float        target_c;
  float        temp_c;
  uint8_t      temp_valid;
  uint8_t      duty;
  uint8_t      fan_on;
  uint8_t      is_simulated;
} ctrl_snapshot_t;

/* 返回值约定：0 = 成功，1 = 参数越界/非法，2 = 当前状态不允许，3 = 传感器无效或仍超温 */
void Control_Init(void);
void Control_Tick(void);                     /* 严格每 500ms 调用 */

uint8_t Control_Start(void);                 /* STANDBY -> RUNNING（会先做传感器新鲜读数校验） */
uint8_t Control_Stop(void);                  /* RUNNING -> STANDBY */
uint8_t Control_CanSaveParams(void);         /* 1 = 允许写 Flash（仅 STANDBY：擦写期间 CPU 停顿，运行中禁止） */
uint8_t Control_AckFault(void);              /* FAULT -> STANDBY；传感器仍无效或仍超温则保持锁定并返回 3 */

uint8_t Control_SetTarget(float target_c);
uint8_t Control_SetPid(float kp, float ki, float kd);
void    Control_ApplyParams(const ctrl_params_t *p);   /* 上电加载时用，内部限幅 */

const ctrl_snapshot_t *Control_Get(void);
const ctrl_params_t   *Control_GetParams(void);
const char            *Control_StateName(void);        /* "INI"/"STB"/"RUN"/"FLT" */

#endif /* __CONTROL_H */
