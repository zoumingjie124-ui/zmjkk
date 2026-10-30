/**
  ******************************************************************************
  * @file    test_control.c
  * @brief   控制状态机宿主机测试：真实 control.c + pid.c，对象与传感器用桩
  *
  * 方法：#include "../App/Src/control.c" 与 pid.c（单编译单元），
  *       Heater_* / Fan_* / Sensor_Read 在本文件里用桩实现，逻辑零改编。
  *
  * 被控对象比 test_pid.c 更接近真实加热体（该文件的纯一阶模型不会超调，说明力有限）：
  *   - 纯滞后（热量传到传感器的传输延迟）：默认 6s = 12 个控制周期
  *   - 一阶惯性：K=40℃/100%，tau=60s（风扇开：tau=25s、增益 x0.6）
  *   - 传感器一阶滞后 tau_s=3s、0.1℃ 量化、±0.05℃ 噪声
  *   这些参数仍是**假设值**，真机必须用实测升温曲线重新辨识，见 docs/TUNING.md。
  *
  * 用例：
  *   C1 带滞后对象上的闭环阶跃（记录超调/调节时间，输出恒在 0~100%）
  *   C1b 默认参数对不同对象动态的敏感性（只记录）；C1c 辨识得到的 PI 在"难"对象上收敛
  *   C2 热失控：传感器一开始就脱落（读室温）-> 60s 左右 FAULT_RUNAWAY，加热关
  *   C3 热失控：运行中传感器脱落 -> FAULT_RUNAWAY
  *   C4 正常升温/稳态不误报热失控
  *   C5 超温 >75℃ 同周期 FAULT_OVERTEMP，加热关、风扇开
  *   C6 传感器无效：FAULT_SENSOR；Start 被拒；故障未排除时 AckFault 保持锁定
  *   C7 Flash 保存仅 STANDBY 允许
  *   C8 SIM 数据源：闸门恒关
  *   C9 热失控窗口：单 tick 条件失效只衰减 1 个 tick 的证据，不清零
  *   C10 大热容慢升温 + 周期性 1-tick 抖动：不误报热失控
  *   C11 运行中改目标：热失控窗口证据清零，不被跨目标的旧窗口误判
  *   C12 重发相同目标：不得清窗，热失控仍须被检出（上位机周期同步场景）
  *   C13 STOP 后的余热管理：加热立即关，风扇按迟滞继续吹到目标+0.5℃ 以内
  ******************************************************************************
  */
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "sensor.h"
#include "heater.h"
#include "fan.h"

/* ------------------------------------------------------------------ 桩 */
static uint16_t s_stub_permille = 0u;
static uint8_t  s_stub_gate     = 0u;
static uint8_t  s_stub_fan      = 0u;

void     Heater_Init(void)                    { s_stub_permille = 0u; s_stub_gate = 0u; }
void     Heater_SetDutyPermille(uint16_t p)   { s_stub_permille = (p > 1000u) ? 1000u : p; }
void     Heater_SetDuty(uint8_t d)            { Heater_SetDutyPermille((uint16_t)(d * 10u)); }
uint16_t Heater_GetDutyPermille(void)         { return s_stub_permille; }
uint8_t  Heater_GetDuty(void)                 { return (uint8_t)((s_stub_permille + 5u) / 10u); }
void     Heater_EnableOutput(uint8_t e)       { s_stub_gate = (e != 0u) ? 1u : 0u; }
uint8_t  Heater_OutputEnabled(void)           { return s_stub_gate; }
void     Heater_Off(void)                     { s_stub_permille = 0u; s_stub_gate = 0u; }
void     Heater_PanicOff(void)                { Heater_Off(); }

void     Fan_Init(void)                       { s_stub_fan = 0u; }
void     Fan_On(void)                         { s_stub_fan = 1u; }
void     Fan_Off(void)                        { s_stub_fan = 0u; }
void     Fan_Set(uint8_t on)                  { s_stub_fan = (on != 0u) ? 1u : 0u; }
uint8_t  Fan_IsOn(void)                       { return s_stub_fan; }

/* 传感器桩：由测试驱动 */
static float   g_meas  = 25.0f;
static uint8_t g_valid = 1u;
static uint8_t g_sim   = 0u;

void          Sensor_Init(void)        {}
uint8_t       Sensor_IsSimulated(void) { return g_sim; }
Sensor_Data_t Sensor_Read(void)
{
  Sensor_Data_t d;
  d.temperature_c = g_meas;
  d.humidity_rh   = -1.0f;
  d.is_simulated  = g_sim;
  d.is_valid      = g_valid;
  return d;
}

/* ------------------------------------------------------------------ 被测代码 */
#include "../App/Src/pid.c"
#include "../App/Src/control.c"

/* ------------------------------------------------------------------ 测试框架 */
static int g_failed = 0;

#define CHECK(cond, ...)                                            \
  do {                                                              \
    if (cond) { printf("  [PASS] " __VA_ARGS__); printf("\n"); }    \
    else      { printf("  [FAIL] " __VA_ARGS__); printf("\n"); g_failed++; } \
  } while (0)

/* ------------------------------------------------------------------ 被控对象 */
#define DELAY_MAX 64
typedef struct
{
  float   t_plant;                 /* 加热体温度 */
  float   t_sens;                  /* 传感器温度（对加热体的一阶滞后） */
  float   fifo[DELAY_MAX];         /* 功率传输延迟队列 */
  int     delay_ticks;
  float   tau_nat;                 /* 自然散热时间常数 s */
  float   tau_sens;                /* 传感器滞后 s */
  int     head;
  uint8_t detached;                /* 1 = 传感器脱落，读室温 */
  uint32_t rng;
} plant_t;

static float plant_rand(plant_t *pl)   /* [-0.5, 0.5) 的伪随机 */
{
  pl->rng = pl->rng * 1664525u + 1013904223u;
  return ((float)((pl->rng >> 8) & 0xFFFFu) / 65536.0f) - 0.5f;
}

static void plant_init(plant_t *pl, int delay_ticks)
{
  int i;
  memset(pl, 0, sizeof(*pl));
  pl->t_plant = 25.0f;
  pl->t_sens  = 25.0f;
  pl->delay_ticks = delay_ticks;
  pl->tau_nat  = 60.0f;
  pl->tau_sens = 3.0f;
  for (i = 0; i < DELAY_MAX; i++) pl->fifo[i] = 0.0f;
  pl->rng = 12345u;
}

/* 推进一个控制周期（0.5s）：以桩里"实际落到引脚上的功率"为输入 */
static void plant_step(plant_t *pl)
{
  const float DT = 0.5f, T_AMB = 25.0f;
  const float TAU_S = pl->tau_sens;
  float p_in = (Heater_OutputEnabled() ? (float)Heater_GetDutyPermille() / 1000.0f : 0.0f);
  float p_delayed, gain, tau, reading;

  pl->fifo[pl->head] = p_in;                                   /* 写入队尾 */
  pl->head = (pl->head + 1) % DELAY_MAX;
  p_delayed = pl->fifo[(pl->head + DELAY_MAX - 1 - pl->delay_ticks) % DELAY_MAX];

  gain = Fan_IsOn() ? 40.0f * 0.6f : 40.0f;
  tau  = Fan_IsOn() ? pl->tau_nat * (25.0f / 60.0f) : pl->tau_nat;
  pl->t_plant += DT * (T_AMB + gain * p_delayed - pl->t_plant) / tau;
  pl->t_sens  += DT * (pl->t_plant - pl->t_sens) / TAU_S;

  reading = pl->detached ? T_AMB : pl->t_sens;
  reading += 0.1f * plant_rand(pl);                            /* ±0.05℃ 噪声 */
  g_meas = floorf(reading * 10.0f + 0.5f) / 10.0f;             /* 0.1℃ 量化 */
}

static void reset_all(plant_t *pl, int delay_ticks)
{
  Control_Init();
  plant_init(pl, delay_ticks);
  g_meas = 25.0f; g_valid = 1u; g_sim = 0u;
  Control_Tick();                          /* INIT -> STANDBY */
}

/* ============================ C1 带滞后对象的闭环阶跃 ============================ */
static void c1_closed_loop(void)
{
  plant_t pl;
  int k, range_ok = 1, fault_seen = 0, last_out_of_band = 0;
  float max_t = 0.0f, err_end = 0.0f;
  float duty_max = 0.0f, duty_min = 100.0f;

  printf("C1 closed loop, delay=6s sensor-lag=3s (default PID 8/0.1/1.5, 25C -> 45C, 900s):\n");
  reset_all(&pl, 12);
  CHECK(Control_Start() == 0u, "start accepted");

  for (k = 0; k < 1800; k++)
  {
    float d;
    Control_Tick();
    plant_step(&pl);
    d = (float)Control_Get()->duty;
    if (d > 100.0f || d < 0.0f) range_ok = 0;
    if (d > duty_max) duty_max = d;
    if (d < duty_min) duty_min = d;
    if (Control_Get()->state == CTRL_FAULT) fault_seen = 1;
    if (g_meas > max_t) max_t = g_meas;
    if (fabsf(g_meas - 45.0f) > 1.0f) last_out_of_band = k;
  }
  err_end = 45.0f - g_meas;
  printf("  maxT=%.2fC overshoot=%.2fC end_err=%.2fC last |err|>1C at t=%.0fs duty range [%.0f,%.0f]%%\n",
         max_t, max_t - 45.0f, err_end, last_out_of_band * 0.5f, duty_min, duty_max);
  CHECK(!fault_seen, "no fault during normal heat-up (no false runaway)");
  CHECK(range_ok, "duty always in [0,100]");
  CHECK((max_t - 45.0f) < 5.0f, "overshoot %.2fC < 5C (record, retune on real hardware)", max_t - 45.0f);
  CHECK(fabsf(err_end) < 1.0f, "settled: |err|=%.2fC < 1C at 900s", fabsf(err_end));
}

/* C1b 敏感性：对象更快、滞后更大（θ/τ 大）时默认参数的表现——只记录，不当作合格线 */
static void c1b_sensitivity(void)
{
  static const struct { int delay; float tau; float tau_s; } cases[] = {
    { 12, 60.0f, 3.0f },    /* 基线 */
    { 20, 40.0f, 5.0f },
    { 30, 30.0f, 8.0f },
    { 40, 30.0f, 10.0f }
  };
  unsigned i;

  printf("C1b sensitivity of default PID to plant dynamics (record only):\n");
  for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
  {
    plant_t pl;
    int k, fault = 0, zero_cross = 0;
    float max_t = 0.0f, min_after = 1000.0f;
    int peak_seen = 0;

    reset_all(&pl, cases[i].delay);
    pl.tau_nat = cases[i].tau;
    pl.tau_sens = cases[i].tau_s;
    (void)Control_Start();
    for (k = 0; k < 2400; k++)
    {
      Control_Tick();
      plant_step(&pl);
      if (Control_Get()->state == CTRL_FAULT) fault = 1;
      if (g_meas > max_t) max_t = g_meas;
      if (g_meas >= 45.0f) peak_seen = 1;
      if (peak_seen && g_meas < min_after) min_after = g_meas;
      if (peak_seen && g_meas < 45.0f && k > 0) zero_cross++;
    }
    printf("  delay=%2.0fs tau=%2.0fs sensor_lag=%2.0fs : overshoot=%5.2fC undershoot_after_peak=%5.2fC end_err=%5.2fC fault=%d\n",
           cases[i].delay * 0.5f, cases[i].tau, cases[i].tau_s, max_t - 45.0f,
           peak_seen ? (45.0f - min_after) : 0.0f, 45.0f - g_meas, fault);
    CHECK(!fault, "no spurious fault (case %u)", i);
    (void)zero_cross;
  }
}

/* C1c 辨识后的 PI：同一个"难"对象（滞后 15s、tau 30s、传感器滞后 8s）。
   参数来自 host/tune_helper.py 对该对象开环 40% 阶跃数据的辨识结果
   （K=0.40 tau=31.6s theta=21.1s -> IMC PI Kp=1.871 Ki=0.0592），不是手调。
   对比 C1b 里默认 8/0.1/1.5 在该对象上持续振荡。注意：这仍是仿真，真机要重新辨识。 */
static void c1c_identified_pi(void)
{
  plant_t pl;
  int k, peak = 0, fault = 0;
  float max_t = 0.0f, min_after = 1000.0f;

  printf("C1c identified PI (Kp=1.871 Ki=0.0592 Kd=0) on the harsh plant:\n");
  reset_all(&pl, 30);
  pl.tau_nat = 30.0f;
  pl.tau_sens = 8.0f;
  (void)Control_SetPid(1.871f, 0.0592f, 0.0f);
  (void)Control_Start();
  for (k = 0; k < 2400; k++)
  {
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) fault = 1;
    if (g_meas > max_t) max_t = g_meas;
    if (g_meas >= 45.0f) peak = 1;
    if (peak && g_meas < min_after) min_after = g_meas;
  }
  printf("  overshoot=%.2fC undershoot_after_peak=%.2fC end_err=%.2fC\n",
         max_t - 45.0f, peak ? 45.0f - min_after : 0.0f, 45.0f - g_meas);
  CHECK(!fault, "no fault");
  CHECK((max_t - 45.0f) < 3.0f, "overshoot %.2fC < 3C", max_t - 45.0f);
  CHECK(fabsf(45.0f - g_meas) < 0.5f, "settled: |err|=%.2fC < 0.5C", fabsf(45.0f - g_meas));
}

/* ============================ C2/C3 热失控 ============================ */
static void c2_runaway_from_start(void)
{
  plant_t pl;
  int k, fault_tick = -1;

  printf("C2 runaway: sensor detached from the start (reads ambient):\n");
  reset_all(&pl, 12);
  pl.detached = 1u;
  CHECK(Control_Start() == 0u, "start accepted (reading looks valid)");
  for (k = 0; k < 400; k++)
  {
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { fault_tick = k; break; }
  }
  CHECK(fault_tick >= 0, "fault raised (tick %d = %.0fs)", fault_tick, fault_tick * 0.5f);
  CHECK(fault_tick >= 0 && fault_tick <= (int)CTRL_RUNAWAY_WINDOW_TICKS + 2,
        "within window+2 ticks (<= %.0fs)", ((int)CTRL_RUNAWAY_WINDOW_TICKS + 2) * 0.5f);
  CHECK(Control_Get()->fault == CTRL_FAULT_RUNAWAY, "fault == RUNAWAY (%d)", (int)Control_Get()->fault);
  CHECK(Heater_GetDutyPermille() == 0u && Heater_OutputEnabled() == 0u, "heater logic 0 and gate closed");
  CHECK(Fan_IsOn() == 1u, "fan forced on");
}

static void c3_runaway_midrun(void)
{
  plant_t pl;
  int k, fault_tick = -1;

  printf("C3 runaway: sensor falls off after reaching target:\n");
  reset_all(&pl, 12);
  (void)Control_Start();
  for (k = 0; k < 1200; k++) { Control_Tick(); plant_step(&pl); }   /* 先正常稳定 600s */
  CHECK(Control_Get()->state == CTRL_RUNNING, "still RUNNING at steady state (T=%.1fC)", g_meas);
  pl.detached = 1u;
  for (k = 0; k < 600; k++)
  {
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { fault_tick = k; break; }
  }
  CHECK(fault_tick >= 0, "fault raised %.0fs after detach", fault_tick * 0.5f);
  CHECK(fault_tick >= 0 && fault_tick < 400, "detected within 200s (window 60s + PID ramp)");
  CHECK(Control_Get()->fault == CTRL_FAULT_RUNAWAY, "fault == RUNAWAY (%d)", (int)Control_Get()->fault);
  CHECK(Heater_GetDutyPermille() == 0u && Heater_OutputEnabled() == 0u, "heater off");
}

/* ============================ C4 不误报 ============================ */
static void c4_no_false_runaway(void)
{
  plant_t pl;
  int k, bad = 0;

  printf("C4 no false runaway on slow-but-healthy heating (delay 20s, 30C -> 60C target):\n");
  reset_all(&pl, 40);
  (void)Control_SetTarget(60.0f);
  (void)Control_Start();
  for (k = 0; k < 2400; k++)
  {
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { bad = 1; break; }
  }
  /* 60℃ 目标 = 环境+35℃，稳态占空比约 87.5%；离目标 <3℃ 后窗口不再计时 */
  CHECK(!bad, "no fault in 1200s (T=%.1fC)", g_meas);
}

/* ============================ C5 超温 ============================ */
static void c5_overtemp(void)
{
  plant_t pl;

  printf("C5 overtemp (>75C):\n");
  reset_all(&pl, 12);
  (void)Control_Start();
  Control_Tick();
  g_meas = 80.0f;
  Control_Tick();
  CHECK(Control_Get()->state == CTRL_FAULT && Control_Get()->fault == CTRL_FAULT_OVERTEMP,
        "FAULT_OVERTEMP in the same tick (state=%d fault=%d)", (int)Control_Get()->state, (int)Control_Get()->fault);
  CHECK(Heater_GetDutyPermille() == 0u && Heater_OutputEnabled() == 0u, "heater off, gate closed");
  CHECK(Fan_IsOn() == 1u, "fan on");
  g_meas = 30.0f;
  Control_Tick();
  CHECK(Control_Get()->state == CTRL_FAULT, "latched: stays FAULT after temperature recovers");
  CHECK(Control_AckFault() == 0u && Control_Get()->state == CTRL_STANDBY, "ack -> STANDBY (no auto-restart)");
}

/* ============================ C6 传感器无效 ============================ */
static void c6_sensor_invalid(void)
{
  plant_t pl;

  printf("C6 sensor invalid:\n");
  reset_all(&pl, 12);
  g_valid = 0u;
  CHECK(Control_Start() == 3u, "Start rejected with invalid sensor (ret 3)");
  CHECK(Control_Get()->state == CTRL_FAULT && Control_Get()->fault == CTRL_FAULT_SENSOR, "FAULT_SENSOR latched");
  CHECK(Control_AckFault() == 3u && Control_Get()->state == CTRL_FAULT, "ack refused while sensor still invalid");
  CHECK(Fan_IsOn() == 1u, "fan stays on (not switched off by refused ack)");
  g_valid = 1u;
  CHECK(Control_AckFault() == 0u && Control_Get()->state == CTRL_STANDBY, "ack accepted after sensor recovers");
  CHECK(Control_Start() == 0u, "start accepted again");
}

/* ============================ C7 保存门禁 ============================ */
static void c7_save_gate(void)
{
  plant_t pl;

  printf("C7 flash save gate:\n");
  reset_all(&pl, 12);
  CHECK(Control_CanSaveParams() == 1u, "allowed in STANDBY");
  (void)Control_Start();
  CHECK(Control_CanSaveParams() == 0u, "refused in RUNNING");
  g_valid = 0u;
  Control_Tick();
  CHECK(Control_CanSaveParams() == 0u, "refused in FAULT");
}

/* ============================ C8 SIM 数据源 ============================ */
static void c8_sim_gate(void)
{
  plant_t pl;
  int k;

  printf("C8 simulated data source never opens the output gate:\n");
  reset_all(&pl, 12);
  g_sim = 1u;
  (void)Control_Start();
  for (k = 0; k < 20; k++)
  {
    Control_Tick();
    plant_step(&pl);
  }
  CHECK(Heater_OutputEnabled() == 0u, "gate closed in SIM mode");
  CHECK(Heater_GetDutyPermille() > 0u, "logic duty still computed (%u permille)", (unsigned)Heater_GetDutyPermille());
}

/* ============================ C9 热失控窗口：单 tick 抖动不清零 ============================ */
static void c9_dip_does_not_reset_window(void)
{
  plant_t pl;
  int k, fault_tick = -1;
  uint16_t before, after;

  printf("C9 runaway window: one-tick condition dip must decay, not reset:\n");
  reset_all(&pl, 12);
  pl.detached = 1u;                       /* 读数钉在室温：PID 持续饱和 */
  CHECK(Control_Start() == 0u, "start accepted");

  for (k = 0; k < 100; k++)
  {
    Control_Tick();
    plant_step(&pl);
  }
  before = s_rw_ticks;                    /* 同 TU 内可见（control.c 被 include） */
  CHECK(before >= 95u, "evidence accumulated (%u ticks)", (unsigned)before);

  /* 制造恰好一个 tick 的"条件不成立"：临时把 PID 输出上限压到 50%，
     使该 tick 占空比 < 800‰（不能用改目标，因为改目标本身会清空证据） */
  s_pid.out_max = 50.0f;
  Control_Tick();
  plant_step(&pl);
  s_pid.out_max = 100.0f;
  after = s_rw_ticks;
  CHECK(after == (uint16_t)(before - 1u),
        "dip decays evidence by 1, not reset (%u -> %u)",
        (unsigned)before, (unsigned)after);

  for (k = 0; k < 60; k++)
  {
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { fault_tick = k; break; }
  }
  CHECK(fault_tick >= 0, "fault still raised after the dip (tick %d)", fault_tick);
  CHECK(fault_tick >= 0 && fault_tick <= 40, "raised without restarting a full window (<=20s)");
  CHECK(Control_Get()->fault == CTRL_FAULT_RUNAWAY, "fault == RUNAWAY (%d)", (int)Control_Get()->fault);
}

/* ============================ C10 大热容 + 抖动不误报 ============================ */
static void c10_slow_plant_with_dips_no_false_trip(void)
{
  plant_t pl;
  int k, faults = 0;

  printf("C10 slow plant (tau_nat=240s) with periodic one-tick dips: no false runaway:\n");
  reset_all(&pl, 12);
  pl.tau_nat = 240.0f;                    /* 热容比默认大 4 倍 */
  (void)Control_SetTarget(35.0f);         /* 只升 10℃ */
  CHECK(Control_Start() == 0u, "start accepted");

  for (k = 0; k < 600; k++)               /* 300s */
  {
    if ((k % 40) == 0 && k > 0)           /* 每 20s 制造一个条件失效 tick（压占空比，不改目标） */
    {
      s_pid.out_max = 50.0f;
      Control_Tick();
      plant_step(&pl);
      s_pid.out_max = 100.0f;
    }
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { faults++; break; }
  }
  CHECK(faults == 0, "no false runaway within 300s");
  CHECK(Control_Get()->temp_c > 30.0f, "still heating (T=%.1fC)", (double)Control_Get()->temp_c);
}

/* ============================ C11 改目标清空热失控窗口 ============================ */
static void c11_target_change_resets_window(void)
{
  plant_t pl;
  int k, fault_early = -1;

  printf("C11 target change clears runaway evidence (no cross-setpoint window):\n");
  reset_all(&pl, 12);
  pl.detached = 1u;                       /* 传感器钉在室温，制造持续大误差 */
  CHECK(Control_Start() == 0u, "start accepted");

  for (k = 0; k < 100; k++)
  {
    Control_Tick();
    plant_step(&pl);
  }
  CHECK(s_rw_ticks >= 95u, "evidence accumulated before change (%u)", (unsigned)s_rw_ticks);

  CHECK(Control_SetTarget(60.0f) == 0u, "target changed to 60C");
  CHECK(s_rw_ticks == 0u, "window cleared on setpoint change (%u)", (unsigned)s_rw_ticks);

  for (k = 0; k < 110; k++)               /* 新窗口还没走完，不应触发 */
  {
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { fault_early = k; break; }
  }
  CHECK(fault_early < 0, "no fault before the fresh window completes");

  for (k = 0; k < 30; k++)                /* 真正的失控仍要被抓到 */
  {
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { break; }
  }
  CHECK(Control_Get()->state == CTRL_FAULT && Control_Get()->fault == CTRL_FAULT_RUNAWAY,
        "real runaway still detected after reset (%d)", (int)Control_Get()->fault);
}

/* ============================ C12 重发同一目标不得压住热失控判据 ============================ */
static void c12_same_target_resend_keeps_window(void)
{
  plant_t pl;
  int k, fault_tick = -1;

  printf("C12 re-sending the SAME target must not suppress runaway detection:\n");
  reset_all(&pl, 12);
  pl.detached = 1u;                       /* 传感器钉在室温，加热器持续满功率 */
  CHECK(Control_Start() == 0u, "start accepted");

  for (k = 0; k < 400; k++)               /* 200s，每 5s 重发一次相同目标（模拟上位机周期同步） */
  {
    if ((k % 10) == 0)
    {
      (void)Control_SetTarget(Control_GetParams()->target_c);
    }
    Control_Tick();
    plant_step(&pl);
    if (Control_Get()->state == CTRL_FAULT) { fault_tick = k; break; }
  }
  CHECK(fault_tick >= 0, "fault raised despite periodic same-target commands (tick %d)", fault_tick);
  CHECK(fault_tick >= 0 && fault_tick <= 125, "within the normal 60s window (+ramp)");
  CHECK(Control_Get()->fault == CTRL_FAULT_RUNAWAY, "fault == RUNAWAY (%d)", (int)Control_Get()->fault);
  CHECK(Heater_GetDutyPermille() == 0u, "heater off after fault");
}

/* ==================== C13 停机余热管理：有限窗口 + 绝对阈值 ==================== */
static void c13_stop_keeps_fan_for_residual_heat(void)
{
  plant_t pl;
  int k;

  printf("C13 residual-heat cooldown: bounded window + absolute thresholds:\n");
  reset_all(&pl, 12);                       /* 目标默认 45℃ */

  CHECK(Control_Start() == 0u, "start accepted");

  g_meas = 48.0f;                           /* 运行期余热：> 45+2.0 */
  Control_Tick();
  CHECK(Fan_IsOn() == 1u, "fan on while hot in RUNNING");

  CHECK(Control_Stop() == 0u, "stop accepted");
  CHECK(Heater_GetDutyPermille() == 0u, "heater off immediately after STOP");
  CHECK(Fan_IsOn() == 1u, "fan NOT killed by STOP");

  g_meas = 41.0f;                           /* > 40：窗口内继续吹 */
  Control_Tick();
  CHECK(Fan_IsOn() == 1u, "fan on above 40C inside the window");

  g_meas = 39.0f;                           /* 38 < T < 40：迟滞带内保持 */
  Control_Tick();
  CHECK(Fan_IsOn() == 1u, "hysteresis band keeps the fan on");

  g_meas = 37.0f;                           /* < 38 */
  Control_Tick();
  CHECK(Fan_IsOn() == 0u, "fan off below 38C");

  /* 窗口边界：温度一直 45℃（高于阈值），窗口过期后必须停风 */
  g_meas = 45.0f;
  for (k = 0; k < (int)CTRL_COOLDOWN_TICKS + 2; k++)
  {
    Control_Tick();
  }
  CHECK(Fan_IsOn() == 0u, "window expired -> fan forced off even if still warm");
  CHECK(Control_Get()->state == CTRL_STANDBY, "still STANDBY");

  /* 关键回归（评审指出的副作用）：目标低于室温时，待机不得长期吹风 */
  CHECK(Control_Start() == 0u, "restart accepted");
  CHECK(Control_SetTarget(20.0f) == 0u, "target set to 20C (below room temp)");
  g_meas = 28.0f;                           /* 室温 28℃ */
  Control_Tick();
  CHECK(Fan_IsOn() == 1u, "RUNNING: 28C > 20+2 so fan runs");
  CHECK(Control_Stop() == 0u, "stop accepted again");
  Control_Tick();
  CHECK(Fan_IsOn() == 0u, "STANDBY: 28C < 40C -> no fan (target below room temp)");

  /* 故障确认走同一窗口 */
  g_meas = 80.0f;
  Control_Tick();
  CHECK(Control_Get()->state == CTRL_FAULT && Fan_IsOn() == 1u, "overtemp -> FAULT with fan on");
  g_meas = 60.0f;
  CHECK(Control_AckFault() == 0u, "ack accepted after cooling");
  CHECK(Fan_IsOn() == 1u, "fan kept on after ACK (60C > 40C, window open)");
  CHECK(Heater_GetDutyPermille() == 0u, "no heating after ACK");
  g_meas = 30.0f;
  Control_Tick();
  CHECK(Fan_IsOn() == 0u, "fan off once cooled below 38C");
}

int main(void)
{
  printf("=== TempCtrl control tests (host build, tests real control.c + pid.c) ===\n");
  c1_closed_loop();
  c1b_sensitivity();
  c1c_identified_pi();
  c2_runaway_from_start();
  c3_runaway_midrun();
  c4_no_false_runaway();
  c5_overtemp();
  c6_sensor_invalid();
  c7_save_gate();
  c8_sim_gate();
  c9_dip_does_not_reset_window();
  c10_slow_plant_with_dips_no_false_trip();
  c11_target_change_resets_window();
  c12_same_target_resend_keeps_window();
  c13_stop_keeps_fan_for_residual_heat();
  printf("=== %s: %d check(s) failed ===\n", g_failed == 0 ? "ALL PASS" : "FAILED", g_failed);
  return g_failed == 0 ? 0 : 1;
}
