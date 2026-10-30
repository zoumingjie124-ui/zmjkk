/**
  ******************************************************************************
  * @file    pid.c
  * @brief   位置式 PID 实现（纯 C，无 HAL 依赖，可宿主机离线测试）
  *
  * ============================ 离散化公式 ==================================
  * 连续形式：u(t) = Kp*e(t) + Ki*∫e(τ)dτ + Kd*de/dt
  *
  * 离散方法（采样周期 Ts，由调用方保证固定且与 pid_init 传入值一致）：
  *   积分（后向矩形）：∫e ≈ Σ e[j]·Ts
  *   微分（对测量值 + 一阶低通）：
  *       raw[k] = -(y[k] - y[k-1]) / Ts            （e = r - y，r 不变时 de/dt = -dy/dt）
  *       d[k]   = d[k-1] + α·(raw[k] - d[k-1])，    α = Ts / (Tf + Ts)
  *     好处：1) 改设定值时没有微分冲击；2) 复位后首次不会用 prev=0 算出巨大微分；
  *           3) NTC 量化噪声被低通抑制。
  *
  * 位置式输出：
  *   u[k] = Kp·e[k] + Ki·Ts·Σe[j] + Kd·d[k]
  *
  * 本工程约定：u 即加热器 PWM 占空比 0~100；Ts = 0.5s（500ms 控制周期）。
  *
  * ============================ 安全机制 ====================================
  * 1. 输出限幅：u ∈ [out_min, out_max] = [0, 100]，任何参数组合都不会越界
  * 2. 积分抗饱和（双重）：
  *    a. 条件积分：输出已饱和且误差仍在向饱和方向推进时，暂停积分累加
  *    b. 积分硬限幅：integral ∈ [int_min, int_max]，防长时间大偏差积分暴涨
  * 3. 异常输入拦截：setpoint/measurement 为 NaN 或超出
  *    [PID_INPUT_MIN_C, PID_INPUT_MAX_C] 时，返回 out_min（0）并置
  *    input_fault=1，且不更新任何内部状态（积分不被脏数据污染）
  ******************************************************************************
  */
#include "pid.h"
#include <math.h>

void pid_init(pid_t *p, float kp, float ki, float kd, float ts)
{
  p->kp = kp;
  p->ki = ki;
  p->kd = kd;
  p->ts = ts;
  p->d_tau = 2.0f;
  pid_reset(p);
  p->out_min  = 0.0f;
  p->out_max  = 100.0f;
  p->int_min  = -100.0f;
  p->int_max  = 100.0f;
}

void pid_reset(pid_t *p)
{
  p->integral    = 0.0f;
  p->prev_meas   = 0.0f;
  p->d_state     = 0.0f;
  p->has_prev    = 0u;
  p->input_fault = 0u;
}

/**
  * @brief  PID 单步计算
  * @param  setpoint    目标值（℃）
  * @param  measurement 测量值（℃）
  * @retval 输出 0~100（PWM 占空比）；输入异常时返回 0
  */
float pid_update(pid_t *p, float setpoint, float measurement)
{
  float err, d_raw, d_filt, alpha, d_term, u, integ_cand;

  /* ---- 1. 异常输入拦截（NaN 用自比较判定，不能写 NaN == NaN） ---- */
  if (setpoint != setpoint || measurement != measurement)
  {
    goto input_fault;
  }
  if (setpoint < PID_INPUT_MIN_C || setpoint > PID_INPUT_MAX_C ||
      measurement < PID_INPUT_MIN_C || measurement > PID_INPUT_MAX_C)
  {
    goto input_fault;
  }

  err = setpoint - measurement;

  /* ---- 2. 微分项（对测量值微分 + 一阶低通） ---- */
  if (p->has_prev)
  {
    d_raw = -(measurement - p->prev_meas) / p->ts;
  }
  else
  {
    d_raw = 0.0f;                       /* 复位后首次：没有历史，不产生微分冲击 */
  }
  alpha  = p->ts / (p->d_tau + p->ts);
  d_filt = p->d_state + alpha * (d_raw - p->d_state);
  d_term = p->kd * d_filt;

  /* ---- 3. 积分项 + 条件积分抗饱和 ---- */
  integ_cand = p->integral + p->ki * err * p->ts;   /* 候选积分值 */

  u = p->kp * err + integ_cand + d_term;

  if (u > p->out_max)
  {
    /* 输出饱和在上限：误差还在正向推（>0）时冻结积分，防止继续累积 */
    if (err > 0.0f)
    {
      integ_cand = p->integral;
    }
    u = p->out_max;
  }
  else if (u < p->out_min)
  {
    /* 输出饱和在下限：误差还在负向推（<0）时冻结积分 */
    if (err < 0.0f)
    {
      integ_cand = p->integral;
    }
    u = p->out_min;
  }
  p->integral = integ_cand;

  /* ---- 4. 积分硬限幅（第二重保险） ---- */
  if (p->integral > p->int_max)
  {
    p->integral = p->int_max;
  }
  else if (p->integral < p->int_min)
  {
    p->integral = p->int_min;
  }

  p->prev_meas   = measurement;
  p->d_state     = d_filt;
  p->has_prev    = 1u;
  p->input_fault = 0u;
  return u;

input_fault:
  /* 脏输入：不更新积分/上次误差，直接返回安全输出 0 */
  p->input_fault = 1u;
  return p->out_min;
}
