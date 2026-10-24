/**
  ******************************************************************************
  * @file    control.c
  * @brief   控制状态机：闭环调度 + 安全联锁 + 运行时参数管理
  *
  * 状态流转（完全由按键/通信控制，无自动启动）：
  *   INIT ──自检通过──> STANDBY ──Control_Start()──> RUNNING
  *   RUNNING ──Control_Stop()──> STANDBY
  *   任意状态 ──联锁触发──> FAULT(锁定) ──Control_AckFault()──> STANDBY
  *
  * 安全联锁（每 tick 最先检查，优先级高于 PID）：
  *   sensor.is_valid == 0 / temp > 75℃ / PID input_fault
  *   -> Heater_Off() + Fan_On() + 锁定 FAULT
  ******************************************************************************
  */
#include "control.h"
#include "sensor.h"
#include "heater.h"
#include "fan.h"
#include "pid.h"

static pid_t           s_pid;
static ctrl_snapshot_t s_snap;
static ctrl_params_t   s_params;

void Control_Init(void)
{
  s_snap.state        = CTRL_INIT;
  s_snap.fault        = CTRL_FAULT_NONE;
  s_snap.temp_c       = 0.0f;
  s_snap.temp_valid   = 0u;
  s_snap.duty         = 0u;
  s_snap.fan_on       = 0u;
  s_snap.is_simulated = 1u;

  s_params.target_c = CTRL_TARGET_DEFAULT_C;
  s_params.kp       = CTRL_KP_DEFAULT;
  s_params.ki       = CTRL_KI_DEFAULT;
  s_params.kd       = CTRL_KD_DEFAULT;
  s_snap.target_c   = s_params.target_c;

  pid_init(&s_pid, s_params.kp, s_params.ki, s_params.kd, CTRL_PID_TS_S);

  Heater_Init();      /* 依赖 main 中已调用 MX_TIM1_Init */
  Fan_Init();
}

/* ---------------- 参数写入（带范围校验） ---------------- */

uint8_t Control_SetTarget(float target_c)
{
  if (target_c != target_c)                        /* NaN */
  {
    return 1u;
  }
  if (target_c < CTRL_TARGET_MIN_C || target_c > CTRL_TARGET_MAX_C)
  {
    return 1u;
  }
  s_params.target_c = target_c;
  s_snap.target_c   = target_c;
  return 0u;
}

uint8_t Control_SetPid(float kp, float ki, float kd)
{
  if (kp != kp || ki != ki || kd != kd)
  {
    return 1u;
  }
  if (kp < CTRL_KP_MIN || kp > CTRL_KP_MAX ||
      ki < CTRL_KI_MIN || ki > CTRL_KI_MAX ||
      kd < CTRL_KD_MIN || kd > CTRL_KD_MAX)
  {
    return 1u;
  }
  s_params.kp = kp;
  s_params.ki = ki;
  s_params.kd = kd;
  /* 在线生效：直接更新 PID 参数，积分项保留（无扰切换） */
  s_pid.kp = kp;
  s_pid.ki = ki;
  s_pid.kd = kd;
  return 0u;
}

void Control_ApplyParams(const ctrl_params_t *p)
{
  if (p == 0)
  {
    return;
  }
  /* 逐项校验后写入；越界项保留原值，避免脏 Flash 参数导致失稳 */
  (void)Control_SetTarget(p->target_c);
  (void)Control_SetPid(p->kp, p->ki, p->kd);
}

const ctrl_params_t *Control_GetParams(void)
{
  return &s_params;
}

/* ---------------- 启停与故障确认 ---------------- */

uint8_t Control_Start(void)
{
  Sensor_Data_t d;

  if (s_snap.state != CTRL_STANDBY)
  {
    return 2u;
  }

  /* 安全校验：启动前必须拿到**新鲜**且有效的测量值。
     按键与串口两个入口都走这里，不存在旁路。 */
  d = Sensor_Read();
  if (!d.is_valid || d.temperature_c > CTRL_SAFE_LIMIT_C)
  {
    Heater_Off();                 /* 逻辑清零 + 关软件输出许可 */
    Fan_On();
    s_snap.state        = CTRL_FAULT;
    s_snap.fault        = (!d.is_valid) ? CTRL_FAULT_SENSOR : CTRL_FAULT_OVERTEMP;
    s_snap.temp_c       = d.temperature_c;
    s_snap.temp_valid   = d.is_valid;
    s_snap.is_simulated = d.is_simulated;
    s_snap.duty         = Heater_GetDuty();
    s_snap.fan_on       = Fan_IsOn();
    return 3u;                    /* 传感器无效或超温，拒绝启动并锁定 */
  }

  pid_reset(&s_pid);              /* 每次启动清零积分，避免沿用旧累积 */
  s_snap.state = CTRL_RUNNING;
  return 0u;
}

uint8_t Control_Stop(void)
{
  if (s_snap.state != CTRL_RUNNING)
  {
    return 2u;
  }
  Heater_Off();                   /* 逻辑 + 软件输出许可一起关 */
  Fan_Off();
  s_snap.duty   = Heater_GetDuty();
  s_snap.fan_on = Fan_IsOn();
  s_snap.state  = CTRL_STANDBY;
  return 0u;
}

uint8_t Control_AckFault(void)
{
  Sensor_Data_t d;

  if (s_snap.state != CTRL_FAULT)
  {
    return 2u;
  }

  /* 故障确认前复核传感器与超温状态：仍无效或仍超温就保持锁定，
     返回 3u（由 comm.c 映射为 ACK_SENSOR_ERR=6），避免误关停散热风扇 */
  d = Sensor_Read();
  if (!d.is_valid || d.temperature_c > CTRL_SAFE_LIMIT_C)
  {
    Heater_Off();
    Fan_On();
    s_snap.fault      = (!d.is_valid) ? CTRL_FAULT_SENSOR : CTRL_FAULT_OVERTEMP;
    s_snap.temp_c     = d.temperature_c;
    s_snap.temp_valid = d.is_valid;
    s_snap.duty       = Heater_GetDuty();
    s_snap.fan_on     = Fan_IsOn();
    return 3u;
  }

  s_snap.fault  = CTRL_FAULT_NONE;
  s_snap.state  = CTRL_STANDBY;   /* 只回待机，不自动恢复闭环 */
  Heater_Off();
  Fan_Off();
  s_snap.duty   = Heater_GetDuty();
  s_snap.fan_on = Fan_IsOn();
  return 0u;
}

/* ---------------- 安全联锁 ---------------- */

static uint8_t safety_interlock(const Sensor_Data_t *d)
{
  if (!d->is_valid || d->temperature_c > CTRL_SAFE_LIMIT_C)
  {
    Heater_Off();
    Fan_On();
    s_snap.state  = CTRL_FAULT;
    s_snap.fault  = (!d->is_valid) ? CTRL_FAULT_SENSOR : CTRL_FAULT_OVERTEMP;
    s_snap.duty   = Heater_GetDuty();
    s_snap.fan_on = Fan_IsOn();
    return 1u;
  }
  return 0u;
}

/* ---------------- 周期调度 ---------------- */

void Control_Tick(void)
{
  Sensor_Data_t d = Sensor_Read();

  s_snap.is_simulated = d.is_simulated;
  s_snap.temp_c       = d.temperature_c;
  s_snap.temp_valid   = d.is_valid;

  switch (s_snap.state)
  {
    case CTRL_INIT:
      if (!safety_interlock(&d))
      {
        s_snap.state = CTRL_STANDBY;   /* 自检通过，等按键启动 */
      }
      break;

    case CTRL_STANDBY:
      if (safety_interlock(&d))
      {
        break;
      }
      /* 待机：加热关（风扇状态由 STOP/故障确认时已置于关闭） */
      Heater_Off();
      s_snap.duty   = Heater_GetDuty();
      s_snap.fan_on = Fan_IsOn();
      break;

    case CTRL_RUNNING:
      if (safety_interlock(&d))
      {
        break;
      }

      /* 软件输出许可（闸门）（第二轮审查）：模拟数据绝不允许驱动真实 PA8。
         REAL 模式才会开闸；SIM 模式下 PID 仍计算，但占空比只落在逻辑变量里。 */
      Heater_EnableOutput(d.is_simulated ? 0u : 1u);

      {
        float u = pid_update(&s_pid, s_params.target_c, d.temperature_c);

        if (s_pid.input_fault)
        {
          Heater_Off();
          Fan_On();
          s_snap.state  = CTRL_FAULT;
          s_snap.fault  = CTRL_FAULT_SENSOR;
          s_snap.duty   = Heater_GetDuty();
          s_snap.fan_on = Fan_IsOn();
          break;
        }
        Heater_SetDuty((uint8_t)u);     /* SetDuty 内部 0~100 限幅 */
        s_snap.duty = Heater_GetDuty(); /* 逻辑占空比（模拟模型据此演化） */
      }

      /* 风扇迟滞：高于目标+2.0℃ 开，回落到目标+0.5℃ 内关 */
      if (d.temperature_c > s_params.target_c + CTRL_FAN_ON_DELTA_C)
      {
        Fan_On();
      }
      else if (d.temperature_c < s_params.target_c + CTRL_FAN_OFF_DELTA_C)
      {
        Fan_Off();
      }
      s_snap.fan_on = Fan_IsOn();
      break;

    case CTRL_FAULT:
    default:
      Heater_Off();
      Fan_On();                     /* 故障态强制散热 */
      s_snap.duty   = Heater_GetDuty();
      s_snap.fan_on = Fan_IsOn();
      break;
  }
}

const ctrl_snapshot_t *Control_Get(void)
{
  return &s_snap;
}

const char *Control_StateName(void)
{
  switch (s_snap.state)
  {
    case CTRL_INIT:    return "INI";
    case CTRL_STANDBY: return "STB";
    case CTRL_RUNNING: return "RUN";
    default:           return "FLT";
  }
}
