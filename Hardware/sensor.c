/**
  ******************************************************************************
  * @file    sensor.c
  * @brief   温度采集：真实 NTC（ADC1_IN0/PA0）+ 可选软件模拟（编译期开关）
  *
  * ============================ 底层 A：真实 NTC ============================
  * 接线假设（分压拓扑）：
  *   3.3V ── R_fixed(10kΩ) ──┬── PA0
  *                           └── NTC(10kΩ B3950) ── GND
  * 换算：R_ntc = R_fixed * adc / (4095 - adc)
  *       T(K) = 1 / (1/298.15 + ln(R_ntc/10000) / 3950)
  * 合法性：贴轨（开路/短路）或换算结果超出 [-40℃, 150℃] 一律判为无效。
  * 注意：NTC 阻值/B 值未经实测校准，读数可能有 ±2℃ 量级误差（待硬件确认）。
  *
  * ============================ 底层 B：软件模拟 ============================
  * 仅在 SENSOR_SIM_ENABLE=1 时编入（台架算法验证用）。一阶惯性：
  *   稳态 = 环境温度 + 加热增益 × 逻辑占空比（风扇开启时散热增强）
  *   T += (steady - T) × dt / τ
  * 模拟模型取的是 **逻辑占空比**（Heater_GetDuty），与硬件闸门无关，
  * 因此模拟模式下闭环仍可观察，但 PA8 不会有任何输出。
  *
  * ============================ 模式选择 ====================================
  * 没有"运行时自动切换"：模式在编译期确定，避免真实负载被模拟数据驱动。
  ******************************************************************************
  */
#include "sensor.h"
#include "adc.h"
#if SENSOR_SIM_ENABLE
#include "heater.h"
#include "fan.h"
#endif
#include <math.h>

/* ---------------- NTC 参数（待硬件确认后校准） ---------------- */
#define NTC_R_FIXED       10000.0f    /* 分压上拉电阻 Ω */
#define NTC_R0            10000.0f    /* NTC 标称阻值 Ω @25℃ */
#define NTC_BETA          3950.0f     /* B 值 */
#define NTC_T0_K          298.15f     /* 25℃ 开尔文 */
#define ADC_FULL_SCALE    4095.0f
#define ADC_RAIL_LOW      5u          /* 贴轨：低于此视为对地短路/未接 */
#define ADC_RAIL_HIGH     4090u       /* 贴轨：高于此视为开路/未接 */

/* 换算结果的物理合理性窗口：超出即判传感器异常，不做 clamp */
#define NTC_T_MIN_C       (-40.0f)
#define NTC_T_MAX_C       (150.0f)

#if SENSOR_SIM_ENABLE
/* ---------------- 模拟热源参数 ---------------- */
#define SIM_AMBIENT_C     25.0f
#define SIM_HEAT_GAIN     40.0f       /* 100% 占空比稳态温升 ℃ */
#define SIM_TAU_NATURAL   60.0f       /* 自然散热时间常数 s */
#define SIM_TAU_FAN       25.0f       /* 风扇散热时间常数 s */
#define SIM_FAN_DERATE    0.6f        /* 风扇开启时加热增益系数 */

static float    s_sim_temp    = SIM_AMBIENT_C;
static uint32_t s_sim_last_ms = 0;
#endif

/* ---------------- 底层 A：真实 NTC ---------------- */

/**
  * @brief 读 ADC 原始值（8 次平均去噪）
  * @retval 0~4095；ADC 出错返回 0xFFFF
  */
static uint16_t ntc_read_raw(void)
{
  uint32_t sum = 0;
  uint8_t  i;

  for (i = 0; i < 8u; i++)
  {
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
    {
      return 0xFFFFu;
    }
    if (HAL_ADC_PollForConversion(&hadc1, 10u) != HAL_OK)
    {
      (void)HAL_ADC_Stop(&hadc1);
      return 0xFFFFu;
    }
    sum += HAL_ADC_GetValue(&hadc1);
  }
  (void)HAL_ADC_Stop(&hadc1);
  return (uint16_t)(sum / 8u);
}

/**
  * @brief ADC 原始值 -> 温度（Beta 公式）
  * @note  调用方须保证 raw 不贴轨（5 < raw < 4090），否则分母趋零
  */
static float ntc_raw_to_celsius(uint16_t raw)
{
  float r_ntc = NTC_R_FIXED * ((float)raw / (ADC_FULL_SCALE - (float)raw));
  float t_k   = 1.0f / (1.0f / NTC_T0_K + logf(r_ntc / NTC_R0) / NTC_BETA);
  return t_k - 273.15f;
}

#if SENSOR_SIM_ENABLE
/* ---------------- 底层 B：软件模拟 ---------------- */

/**
  * @brief 按一阶惯性推进模拟温度（dt 用 HAL_GetTick 实测间隔）
  */
static void sim_update(void)
{
  uint32_t now  = HAL_GetTick();
  float    dt_s = (float)(now - s_sim_last_ms) / 1000.0f;
  float    steady, tau, gain;

  s_sim_last_ms = now;
  if (dt_s <= 0.0f)
  {
    return;
  }

  gain   = Fan_IsOn() ? (SIM_HEAT_GAIN * SIM_FAN_DERATE) : SIM_HEAT_GAIN;
  steady = SIM_AMBIENT_C + gain * ((float)Heater_GetDuty() / 100.0f);
  tau    = Fan_IsOn() ? SIM_TAU_FAN : SIM_TAU_NATURAL;

  s_sim_temp += (steady - s_sim_temp) * (dt_s / tau);
}
#endif

/* ---------------- 统一接口 ---------------- */

void Sensor_Init(void)
{
  MX_ADC1_Init();          /* ADC 自校准已在 adc.c 的 MX_ADC1_Init 末尾完成 */

#if SENSOR_SIM_ENABLE
  s_sim_temp    = SIM_AMBIENT_C;
  s_sim_last_ms = HAL_GetTick();
#else
  /* 不做模式自检、不做自动切换：上电只初始化 ADC。
     若分压电路不在位，Sensor_Read 会返回 invalid，
     控制层 INIT 阶段将直接进入 FAULT，要求人工处理。 */
  (void)ntc_read_raw();
#endif
}

Sensor_Data_t Sensor_Read(void)
{
  Sensor_Data_t d;

  d.humidity_rh  = -1.0f;                    /* NTC 无湿度功能 */

#if SENSOR_SIM_ENABLE
  sim_update();
  d.temperature_c = s_sim_temp;
  d.is_simulated  = 1u;
  d.is_valid      = 1u;                      /* 模拟值恒"有效"，但带 SIM 标识 */
#else
  {
    uint16_t raw = ntc_read_raw();
    float    t;

    d.is_simulated = 0u;
    if (raw == 0xFFFFu || raw <= ADC_RAIL_LOW || raw >= ADC_RAIL_HIGH)
    {
      /* 开路/短路/ADC 故障：无效读数，上层必须按故障处理 */
      d.temperature_c = 0.0f;
      d.is_valid      = 0u;
    }
    else
    {
      t = ntc_raw_to_celsius(raw);
      if (t < NTC_T_MIN_C || t > NTC_T_MAX_C)
      {
        /* 换算结果超出物理合理性（多半是接线/分压电阻不对）：判无效 */
        d.temperature_c = 0.0f;
        d.is_valid      = 0u;
      }
      else
      {
        d.temperature_c = t;
        d.is_valid      = 1u;
      }
    }
  }
#endif
  return d;
}

uint8_t Sensor_IsSimulated(void)
{
  return (SENSOR_SIM_ENABLE != 0) ? 1u : 0u;
}
