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
  *   sensor.is_valid == 0 / temp > 75℃ / PID input_fault / 热失控判据
  *   -> Heater_Off() + Fan_On() + 锁定 FAULT
  *
  * 热失控判据（RUNNING）：占空比 >= 80% 且离目标 > 3℃ 连续 60s，期间温升 < 2℃，
  *   说明"推得很用力但温度不动"——NTC 脱落读室温时 75℃ 上限永远触发不了，靠它兜底。
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

/* 热失控窗口状态 */
static uint16_t s_rw_ticks = 0u;
static float    s_rw_t0    = 0.0f;

/* 统一的进入故障动作：关加热（逻辑+闸门）、开风扇、锁定、刷新快照 */
static void enter_fault(ctrl_fault_t f)
{
  Heater_Off();
  Fan_On();
  s_snap.state  = CTRL_FAULT;
  s_snap.fault  = f;
  s_snap.duty   = Heater_GetDuty();
  s_snap.fan_on = Fan_IsOn();
}

/* 返回 1 = 判定热失控。duty_permille 是本周期 PID 想要的功率。 */
static uint8_t runaway_check(float temp_c, uint16_t duty_permille)
{
  if (duty_permille >= CTRL_RUNAWAY_DUTY_PERMILLE &&
      (s_params.target_c - temp_c) > CTRL_RUNAWAY_MIN_ERR_C)
  {
    if (s_rw_ticks == 0u)
    {
      s_rw_t0 = temp_c;                 /* 窗口起点 */
    }
    s_rw_ticks++;
    if (s_rw_ticks >= CTRL_RUNAWAY_WINDOW_TICKS)
    {
      if ((temp_c - s_rw_t0) < CTRL_RUNAWAY_MIN_RISE_C)
      {
        return 1u;                      /* 60s 内几乎没涨 */
      }
      s_rw_ticks = 0u;                  /* 升温正常，开新窗口 */
    }
  }
  else
  {
    /* 漏桶衰减：单个 tick 条件不成立只扣 1 个 tick 的证据，不整窗清零。
       大热容对象在 PID 调节中占空比偶尔掉到 79.9%，若清零则窗口一直从头计时，
       热失控可能永远触发不了。t0 不随之更新 —— 意味着判定时间跨度变长，
       对真实失控只会"更晚发现"，不会增加误报。 */
    if (s_rw_ticks > CTRL_RUNAWAY_DECAY_TICKS)
    {
      s_rw_ticks = (uint16_t)(s_rw_ticks - CTRL_RUNAWAY_DECAY_TICKS);
    }
    else
    {
      s_rw_ticks = 0u;                  /* 归零后，下一个合格 tick 会重新取 t0 */
    }
  }
  return 0u;
}

uint8_t Control_CanSaveParams(void)
{
  return (s_snap.state == CTRL_STANDBY) ? 1u : 0u;
}

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
  /* 只在目标**数值真的变了**时才清窗：上位机/脚本周期性重发同一个目标是正常用法，
     若每次调用都清，热失控判据会被一条重复指令永久压住（C12 复现）。
     判定前提变了才需要清：旧目标下攒了 55s 的窗口若在新目标上继续计时，
     可能在 5s 后用跨目标的时间窗误报（第四轮审查 #1）。 */
  if (target_c != s_params.target_c)
  {
    s_rw_ticks = 0u;
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

  pid_reset(&s_pid);              /* 每次启动清零积分/微分状态，避免沿用旧累积 */
  s_rw_ticks   = 0u;
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
  /* 风扇**不在这里关**（F 项策略）：停机后余热仍会让温度上升，
     立刻停风等于把热量闷在腔体里。保持当前状态，由 STANDBY 的
     fan_hysteresis() 在下一个 tick 按实际温度决定何时停。 */
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
    enter_fault((!d->is_valid) ? CTRL_FAULT_SENSOR : CTRL_FAULT_OVERTEMP);
    return 1u;
  }
  return 0u;
}

/* ---------------- 周期调度 ---------------- */

/* 风扇迟滞：高于 目标+2.0℃ 开，回落到 目标+0.5℃ 内关。
   RUNNING 与 STANDBY **共用同一套判据**（健壮性审查 F 项的策略）：
   停机只关加热，不立刻停风——余热仍会把温度顶上去，立即停风等于把热量
   闷在腔体里，极端情况能冲到 75℃ 联锁阈值。现在 STANDBY 继续按同一迟滞
   吹风，等温度回落到目标附近才停，OLED/串口都能看到 FAN 仍为 ON。 */
static void fan_hysteresis(float temp_c)
{
  if (temp_c > s_params.target_c + CTRL_FAN_ON_DELTA_C)
  {
    Fan_On();
  }
  else if (temp_c < s_params.target_c + CTRL_FAN_OFF_DELTA_C)
  {
    Fan_Off();
  }
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
      /* 待机：加热必关；风扇按与运行期相同的迟滞做**余热管理**（F 项策略）。
         传感器无效时上面的 interlock 已转 FAULT，走不到这里。 */
      Heater_Off();
      fan_hysteresis(d.temperature_c);
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
          enter_fault(CTRL_FAULT_SENSOR);
          break;
        }

        /* PID 输出 0~100.0% -> ‰（四舍五入，0.1% 分辨率；SetDutyPermille 内部再限幅）
           强转前必须显式钳位：若将来有人把 pid.out_min 改成负值（测试里就这么干过），
           负 float 转无符号是未定义行为，可能回绕成 65535 -> 限幅成 1000‰ ->
           本该"不加热"却变成 100% 满功率。NaN 也用 !(u>0) 一并拦掉。 */
        if (!(u > 0.0f))
        {
          u = 0.0f;                        /* 含 NaN 与负值 */
        }
        else if (u > 100.0f)
        {
          u = 100.0f;
        }
        {
          uint16_t permille = (uint16_t)(u * 10.0f + 0.5f);

          if (runaway_check(d.temperature_c, permille))
          {
            enter_fault(CTRL_FAULT_RUNAWAY);
            break;
          }
          Heater_SetDutyPermille(permille);
        }
        s_snap.duty = Heater_GetDuty(); /* 四舍五入后的百分比占空比，仅供 OLED 与状态帧上报；
                                           模拟模型的温度演化读 Heater_GetDutyPermille() */
      }

      fan_hysteresis(d.temperature_c);
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
