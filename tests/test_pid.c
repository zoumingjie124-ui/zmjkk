/**
  ******************************************************************************
  * @file    test_pid.c
  * @brief   PID 离线单元测试（宿主机运行，不碰任何 STM32/HAL 代码）
  *
  * 方法：直接 #include "../App/Src/pid.c"（单编译单元测试法），
  *       逻辑零改编——测的就是固件里那份 pid.c。
  *
  * 被测对象与工程参数一致：Kp=8.0 Ki=0.10 Kd=1.5 Ts=0.5s，输出 0~100
  * 被控对象模型：与 sensor.c 软件模拟一致的一阶惯性
  *       T[k+1] = T[k] + dt * (T_amb + K*u/100 - T[k]) / tau
  *       T_amb=25℃, K=40℃(100%占空比稳态温升), tau=60s(自然散热)
  *
  * 用例：
  *   T1 脉冲(阶跃)响应：25℃ -> 目标 45℃，收敛性与超调
  *   T2 输出限幅：大误差下输出精确钳在 0/100
  *   T3 积分抗饱和：条件积分冻结 + 积分硬限幅 + 反向误差立即退饱和
  *   T4 异常输入拦截：NaN/超界返回 0 并置故障，内部状态不被污染
  *   T5 微分作用于测量值：设定值阶跃无微分冲击；复位后首次无启动冲击
  *   T6 微分低通：测量值单步跳变时微分项被平滑，且稳态下为 0
  ******************************************************************************
  */
#include <stdio.h>
#include <math.h>

/* 被测源文件：逻辑与固件完全一致 */
#include "../App/Src/pid.c"

static int g_failed = 0;

#define CHECK(cond, ...)                                            \
  do {                                                              \
    if (cond) {                                                     \
      printf("  [PASS] " __VA_ARGS__);                              \
      printf("\n");                                                 \
    } else {                                                        \
      printf("  [FAIL] " __VA_ARGS__);                              \
      printf("\n");                                                 \
      g_failed++;                                                   \
    }                                                               \
  } while (0)

/* 一阶惯性被控对象（与 sensor.c 模拟底层同构） */
static float plant_update(float t, float u_pct, float dt, float tau)
{
  const float T_AMB = 25.0f, GAIN = 40.0f;
  return t + dt * (T_AMB + GAIN * u_pct / 100.0f - t) / tau;
}

/* 运行期产生 NaN（volatile 防止编译器常量折叠报除零错） */
static float make_nan(void)
{
  volatile float zero = 0.0f;
  return zero / zero;
}

/* 工程同款 PID 参数 */
static void pid_default(pid_t *p)
{
  pid_init(p, 8.0f, 0.10f, 1.5f, 0.5f);
}

/* ============================ T1 阶跃响应 ============================ */
static void t1_step_response(void)
{
  pid_t pid;
  float temp = 25.0f, u;
  float max_temp = 25.0f;
  int k, range_ok = 1;

  printf("T1 step response (25C -> 45C, 600s):\n");
  pid_default(&pid);

  for (k = 0; k < 1200; k++)            /* 1200 x 0.5s = 600s */
  {
    u = pid_update(&pid, 45.0f, temp);
    if (u < 0.0f || u > 100.0f) range_ok = 0;
    temp = plant_update(temp, u, 0.5f, 60.0f);
    if (temp > max_temp) max_temp = temp;
    if (k == 1199)
    {
      printf("  final T=%.2fC u=%.1f%% err=%.2fC maxT=%.2fC\n",
             temp, u, 45.0f - temp, max_temp);
    }
  }

  CHECK(fabsf(45.0f - temp) < 0.5f, "converged: |err|=%.3fC", fabsf(45.0f - temp));
  CHECK((max_temp - 45.0f) < 3.0f, "overshoot=%.3fC < 3.0C", max_temp - 45.0f);
  CHECK(range_ok, "output always in [0,100]");
  CHECK(pid.input_fault == 0u, "no input fault");
}

/* ============================ T2 输出限幅 ============================ */
static void t2_output_clamp(void)
{
  pid_t pid;
  float u;

  printf("T2 output clamp:\n");
  pid_default(&pid);

  u = pid_update(&pid, 140.0f, 25.0f);  /* err=115，正饱和 */
  CHECK(u == 100.0f, "positive clamp u=%.1f == 100", u);

  u = pid_update(&pid, 10.0f, 60.0f);   /* err=-50，负饱和 */
  CHECK(u == 0.0f, "negative clamp u=%.1f == 0", u);

  CHECK(pid.integral <= pid.int_max && pid.integral >= pid.int_min,
        "integral=%.3f within hard limits", pid.integral);
}

/* ============================ T3 积分抗饱和 ============================ */
static void t3_anti_windup(void)
{
  pid_t pid;
  float u;
  int k;

  printf("T3 anti-windup:\n");

  /* 3a. 输出饱和期间误差同向 -> 条件积分冻结，积分不累积 */
  pid_default(&pid);
  for (k = 0; k < 2000; k++)            /* 长时间 err=+30，u 恒 100 */
  {
    u = pid_update(&pid, 55.0f, 25.0f);
  }
  CHECK(u == 100.0f, "saturated output u=%.1f == 100", u);
  CHECK(pid.integral == 0.0f, "frozen integral=%.4f == 0", pid.integral);

  /* 3b. 误差反向 -> 一个周期内立即退饱和（无 windup 锁死） */
  u = pid_update(&pid, 25.0f, 55.0f);   /* err=-30 */
  CHECK(u == 0.0f, "desaturated in 1 step u=%.1f == 0", u);

  /* 3c. 积分硬限幅：缩小 int_max，未饱和输出下积分也被钳住 */
  pid_default(&pid);
  pid.int_max = 5.0f;                   /* 测试注入：故意压低上限 */
  for (k = 0; k < 200; k++)             /* err=+2，u≈16+I 未饱和 */
  {
    u = pid_update(&pid, 27.0f, 25.0f);
  }
  CHECK(pid.integral == 5.0f, "hard integral clamp=%.4f == 5", pid.integral);
  CHECK(u > 0.0f && u < 100.0f, "unsaturated output u=%.1f", u);
}

/* ============================ T4 异常输入拦截 ============================ */
static void t4_invalid_input(void)
{
  pid_t pid;
  float u, i0;

  printf("T4 invalid input interception:\n");
  pid_default(&pid);

  /* 先正常运行，建立内部状态 */
  for (int k = 0; k < 10; k++)
  {
    (void)pid_update(&pid, 45.0f, 40.0f);
  }
  i0 = pid.integral;
  printf("  state before fault: integral=%.4f\n", i0);

  /* NaN 测量值 */
  u = pid_update(&pid, 45.0f, make_nan());
  CHECK(u == 0.0f && pid.input_fault == 1u, "NaN measurement -> u=%.1f fault=%u", u, pid.input_fault);
  CHECK(pid.integral == i0, "integral untouched=%.4f", pid.integral);

  /* NaN 目标值 */
  u = pid_update(&pid, -make_nan(), 40.0f);
  CHECK(u == 0.0f && pid.input_fault == 1u, "NaN setpoint -> u=%.1f fault=%u", u, pid.input_fault);

  /* 超界测量值（>150℃） */
  u = pid_update(&pid, 45.0f, 999.0f);
  CHECK(u == 0.0f && pid.input_fault == 1u, "overrange(999C) -> u=%.1f fault=%u", u, pid.input_fault);

  /* 超界测量值（<-40℃） */
  u = pid_update(&pid, 45.0f, -100.0f);
  CHECK(u == 0.0f && pid.input_fault == 1u, "underrange(-100C) -> u=%.1f fault=%u", u, pid.input_fault);

  /* 恢复正常输入：故障标志清除，输出立即恢复合理值，
     积分从冻结值正常续算（err=5 -> +Ki*err*Ts = +0.25） */
  u = pid_update(&pid, 45.0f, 40.0f);
  CHECK(pid.input_fault == 0u, "fault cleared after valid input, fault=%u", pid.input_fault);
  CHECK(u >= 0.0f && u <= 100.0f, "recovered output u=%.1f in [0,100]", u);
  CHECK(fabsf(pid.integral - (i0 + 0.10f * 5.0f * 0.5f)) < 1e-4f,
        "integral resumes normally=%.4f", pid.integral);
}

/* ============================ T5 微分作用于测量值 ============================ */
static void t5_derivative_on_measurement(void)
{
  pid_t pid;
  float u0, u1;
  int k;

  printf("T5 derivative on measurement (no kick on setpoint step / start):\n");

  /* 5a. 稳态后设定值阶跃 +10℃：Kp=2, Ki=0, Kd=10。
         若 D 作用于误差：u 跳变 = Kp*10 + Kd*10/Ts = 20 + 200 -> 钳到 100；
         作用于测量值：u 跳变只有 Kp*10 = 20。 */
  pid_init(&pid, 2.0f, 0.0f, 10.0f, 0.5f);
  for (k = 0; k < 20; k++)
  {
    u0 = pid_update(&pid, 45.0f, 45.0f);
  }
  u1 = pid_update(&pid, 55.0f, 45.0f);
  CHECK(fabsf(u0) < 1e-4f, "steady: u=%.4f ~ 0", u0);
  CHECK(fabsf(u1 - 20.0f) < 1e-3f, "setpoint step +10C -> u=%.3f == Kp*10 = 20 (no D kick)", u1);

  /* 5b. 复位后第一次计算：测量值 25，目标 45，Kp=0 Ki=0 Kd=1.5。
         旧实现 prev_err=0 -> D = 1.5*20/0.5 = 60%；现在首次微分恒为 0。 */
  pid_init(&pid, 0.0f, 0.0f, 1.5f, 0.5f);
  u1 = pid_update(&pid, 45.0f, 25.0f);
  CHECK(fabsf(u1) < 1e-6f, "first call after reset: u=%.6f == 0 (no start-up kick)", u1);

  /* 5c. 测量值上升时微分项应为负（刹车），而不是正 */
  pid_init(&pid, 0.0f, 0.0f, 10.0f, 0.5f);
  (void)pid_update(&pid, 45.0f, 30.0f);
  u1 = pid_update(&pid, 45.0f, 31.0f);   /* dy/dt = +2 ℃/s -> D = -Kd*filtered(2) < 0，被下限钳到 0 */
  CHECK(u1 == 0.0f, "rising measurement -> D brakes (clamped at 0): u=%.3f", u1);
  CHECK(pid.d_state < 0.0f, "filtered derivative state negative: %.4f", pid.d_state);
}

/* ============================ T6 微分低通 ============================ */
static void t6_derivative_filter(void)
{
  pid_t pid;
  float raw, alpha, expected, d1;
  int k;

  printf("T6 derivative low-pass:\n");
  pid_init(&pid, 0.0f, 0.0f, 1.0f, 0.5f);
  pid.out_min = -1000.0f;               /* 测试注入：放开下限以观察负值微分 */
  pid.out_max = 1000.0f;
  (void)pid_update(&pid, 45.0f, 30.0f);
  d1 = pid_update(&pid, 45.0f, 32.0f);  /* 测量值单步跳 +2℃ */
  raw = -2.0f / 0.5f;                   /* -4 ℃/s */
  alpha = 0.5f / (pid.d_tau + 0.5f);
  expected = alpha * raw;               /* 第一拍只吃到 alpha 倍 */
  CHECK(fabsf(d1 - expected) < 1e-4f, "one-step jump smoothed: D=%.4f == alpha*raw=%.4f (alpha=%.3f)", d1, expected, alpha);
  CHECK(fabsf(d1) < fabsf(raw), "smaller than the unfiltered %.1f", raw);

  for (k = 0; k < 60; k++)              /* 测量值不再变化 -> 微分项衰减到 0 */
  {
    d1 = pid_update(&pid, 45.0f, 32.0f);
  }
  CHECK(fabsf(d1) < 1e-3f, "decays to 0 when measurement flat: D=%.6f", d1);
}

int main(void)
{
  printf("=== TempCtrl PID unit tests (host build, tests real pid.c) ===\n");
  t1_step_response();
  t2_output_clamp();
  t3_anti_windup();
  t4_invalid_input();
  t5_derivative_on_measurement();
  t6_derivative_filter();
  printf("=== %s: %d check(s) failed ===\n",
         g_failed == 0 ? "ALL PASS" : "FAILED", g_failed);
  return g_failed == 0 ? 0 : 1;
}
