/**
  ******************************************************************************
  * @file    pid.h
  * @brief   位置式 PID 控制器（纯 C，无任何 HAL/STM32 依赖）
  *
  * 约束 4：本文件与 pid.c 不允许 include 任何 HAL 库头文件，
  * 保证可在宿主机（MSVC/GCC）上编译并做离线单元测试。
  ******************************************************************************
  */
#ifndef __PID_H
#define __PID_H

#include <stdint.h>

/* 输入合法范围（℃）：超出视为传感器异常，拒绝计算并置故障标志 */
#define PID_INPUT_MIN_C   (-40.0f)
#define PID_INPUT_MAX_C   (150.0f)

typedef struct
{
  /* 参数 */
  float kp;
  float ki;
  float kd;
  float ts;              /* 固定采样周期，单位秒（调用方保证按此周期调用） */

  /* 运行状态 */
  float integral;        /* 积分累积项（已含 Ki*Ts 因子） */
  float prev_meas;       /* 上次测量值（微分用：对测量值微分，设定值阶跃不产生冲击） */
  float d_state;         /* 微分项低通滤波状态（单位 ℃/s） */
  float d_tau;           /* 微分低通时间常数 s（默认 2.0，抑制 NTC 量化噪声） */
  uint8_t has_prev;      /* 0 = 复位后尚无上次测量值：首次计算微分恒为 0，避免启动冲击 */
  uint8_t input_fault;   /* 1 = 上次输入异常（NaN/超界），输出被强制为 0 */

  /* 限幅配置 */
  float out_min;         /* 输出下限（默认 0，对应 PWM 0%） */
  float out_max;         /* 输出上限（默认 100，对应 PWM 100%） */
  float int_min;         /* 积分硬限幅下限（第二重抗饱和） */
  float int_max;         /* 积分硬限幅上限 */
} pid_t;

void  pid_init(pid_t *p, float kp, float ki, float kd, float ts);
float pid_update(pid_t *p, float setpoint, float measurement);
void  pid_reset(pid_t *p);

#endif /* __PID_H */
