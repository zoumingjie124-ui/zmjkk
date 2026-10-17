/**
  ******************************************************************************
  * @file    control.c
  * @brief   控制状态机：闭环调度 + 安全联锁（联锁优先级高于一切控制逻辑）
  *
  * 状态流转：
  *   INIT ──传感器自检通过──> STANDBY ──2s 后自动进入(阶段6改按键)──> RUNNING
  *   RUNNING ──联锁触发──> FAULT(锁定) ──人工确认──> STANDBY
  *
  * 安全联锁（最高优先级，每个 tick 最先检查）：
  *   触发条件：sensor.is_valid == 0（断线/短路/贴轨）
  *            或 temp > CTRL_SAFE_LIMIT_C（75℃）
  *            或 PID 报 input_fault（NaN/超界）
  *   触发动作：Heater_Off() + Fan_On()（强制散热）+ 锁定 FAULT
  *   解除方式：仅 Control_AckFault()（阶段 6 由按键调用）
  ******************************************************************************
  */
#include "control.h"
#include "sensor.h"
#include "heater.h"
#include "fan.h"
#include "pid.h"

static pid_t           s_pid;
static ctrl_snapshot_t s_snap;
static uint8_t         s_standby_ticks;   /* STANDBY 停留计数（自动启动用） */

void Control_Init(void)
{
  s_snap.state        = CTRL_INIT;
  s_snap.fault        = CTRL_FAULT_NONE;
  s_snap.target_c     = CTRL_TARGET_DEFAULT_C;
  s_snap.temp_c       = 0.0f;
  s_snap.temp_valid   = 0u;
  s_snap.duty         = 0u;
  s_snap.fan_on       = 0u;
  s_snap.is_simulated = 1u;

  s_standby_ticks = 0u;

  pid_init(&s_pid, CTRL_PID_KP, CTRL_PID_KI, CTRL_PID_KD, CTRL_PID_TS_S);

  Heater_Init();      /* PWM 启动，占空比 0 */
  Fan_Init();         /* 风扇关 */
}

/**
  * @brief 安全联锁：返回 1 表示已触发（调用方应直接返回，本 tick 不做控制）
  */
static uint8_t safety_interlock(const Sensor_Data_t *d)
{
  if (!d->is_valid || d->temperature_c > CTRL_SAFE_LIMIT_C)
  {
    Heater_Off();                       /* PWM 立即归零 */
    Fan_On();                           /* 强制散热 */
    s_snap.state     = CTRL_FAULT;      /* 锁定，等待人工确认 */
    s_snap.fault     = (!d->is_valid) ? CTRL_FAULT_SENSOR : CTRL_FAULT_OVERTEMP;
    s_snap.duty      = Heater_GetDuty();
    s_snap.fan_on    = Fan_IsOn();
    return 1u;
  }
  return 0u;
}

void Control_Tick(void)
{
  Sensor_Data_t d = Sensor_Read();

  s_snap.is_simulated = d.is_simulated;
  s_snap.temp_c       = d.temperature_c;
  s_snap.temp_valid   = d.is_valid;

  switch (s_snap.state)
  {
    case CTRL_INIT:
      /* 自检：传感器出数即通过（模拟模式恒有效）；真实模式无效直接进 FAULT */
      if (!safety_interlock(&d))
      {
        s_snap.state = CTRL_STANDBY;
        s_standby_ticks = 0u;
      }
      break;

    case CTRL_STANDBY:
      if (safety_interlock(&d))
      {
        break;
      }
      Heater_Off();
      Fan_Off();
      s_snap.duty   = Heater_GetDuty();
      s_snap.fan_on = Fan_IsOn();
      /* 自动进入 RUNNING 观察收敛（阶段 6 删除，改按键启停） */
      if (++s_standby_ticks >= 4u)      /* 4 × 500ms = 2s */
      {
        pid_reset(&s_pid);
        s_snap.state = CTRL_RUNNING;
      }
      break;

    case CTRL_RUNNING:
      /* ---- 联锁最先检查（最高优先级） ---- */
      if (safety_interlock(&d))
      {
        break;
      }

      /* ---- PID 闭环（500ms 固定周期） ---- */
      {
        float u = pid_update(&s_pid, s_snap.target_c, d.temperature_c);

        if (s_pid.input_fault)
        {
          /* PID 拦截到异常输入：等同传感器故障处理 */
          Heater_Off();
          Fan_On();
          s_snap.state  = CTRL_FAULT;
          s_snap.fault  = CTRL_FAULT_SENSOR;
          s_snap.duty   = Heater_GetDuty();
          s_snap.fan_on = Fan_IsOn();
          break;
        }

        Heater_SetDuty((uint8_t)u);     /* SetDuty 内部 0~100 限幅 */
        s_snap.duty = Heater_GetDuty();
      }

      /* ---- 风扇迟滞控制（独立于 PID 的开关逻辑） ---- */
      if (d.temperature_c > s_snap.target_c + CTRL_FAN_ON_DELTA_C)
      {
        Fan_On();
      }
      else if (d.temperature_c < s_snap.target_c + CTRL_FAN_OFF_DELTA_C)
      {
        Fan_Off();
      }
      s_snap.fan_on = Fan_IsOn();
      break;

    case CTRL_FAULT:
    default:
      /* 锁定态：维持加热关 + 风扇散热，只等 Control_AckFault() */
      Heater_Off();
      Fan_On();
      s_snap.duty   = Heater_GetDuty();
      s_snap.fan_on = Fan_IsOn();
      break;
  }
}

const ctrl_snapshot_t *Control_Get(void)
{
  return &s_snap;
}

void Control_SetTarget(float target_c)
{
  if (target_c != target_c)             /* NaN 拒收 */
  {
    return;
  }
  if (target_c < CTRL_TARGET_MIN_C)
  {
    target_c = CTRL_TARGET_MIN_C;
  }
  if (target_c > CTRL_TARGET_MAX_C)
  {
    target_c = CTRL_TARGET_MAX_C;       /* 目标上限 70 < 安全上限 75 */
  }
  s_snap.target_c = target_c;
}

void Control_AckFault(void)
{
  if (s_snap.state == CTRL_FAULT)
  {
    s_snap.fault        = CTRL_FAULT_NONE;
    s_snap.state        = CTRL_STANDBY;   /* 回待机，不自动恢复闭环 */
    s_standby_ticks     = 0u;
  }
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
