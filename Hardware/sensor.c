/**
  ******************************************************************************
  * @file    sensor.c
  * @brief   温度采集模块：真实 NTC（ADC1_IN0/PA0）+ 软件热力学模拟，双底层
  *
  * ============================ 底层 A：真实 NTC ============================
  * 接线假设（默认分压拓扑，接错会在 Sensor_Init 自检时被判为"无传感器"）：
  *   3.3V ── R_fixed(10kΩ) ──┬── PA0
  *                           └── NTC(10kΩ B3950) ── GND
  * 换算：adc -> R_ntc = R_fixed * adc / (4095 - adc)
  *       Beta 公式 -> T(K) = 1 / (1/298.15 + ln(R_ntc/10000) / 3950)
  * 注意：NTC 具体阻值/B 值未经实测校准，读数可能有 ±2℃ 量级误差（待硬件确认）。
  *
  * ============================ 底层 B：软件模拟 ============================
  * 一阶惯性环节（逼近小型环境箱的热响应）：
  *   稳态温度 steady = 环境温度 + 加热增益 × 占空比（风扇开启时散热增强，增益打折）
  *   每次读取按 dt 向 steady 指数趋近：T += (steady - T) × dt / τ
  *   风扇开启 → τ 变小（冷却更快）
  * 模拟输入取自 heater/fan 模块的当前真实输出状态，因此可验证完整闭环逻辑。
  *
  * ============================ 模式选择 ====================================
  * Sensor_Init 上电自检：ADC 读数落在 (5, 4090) 合理区间 -> 真实模式；
  * 贴轨（≈0 或 ≈4095，即分压点开路/短路，通常是没接 NTC）-> 自动切模拟模式。
  * 模拟模式下 is_simulated=1，显示层必须显示 SIM 标识（约束 3）。
  ******************************************************************************
  */
#include "sensor.h"
#include "adc.h"
#include "heater.h"
#include "fan.h"
#include <math.h>

/* ---------------- NTC 参数（待硬件确认后校准） ---------------- */
#define NTC_R_FIXED       10000.0f    /* 分压上拉电阻 Ω */
#define NTC_R0            10000.0f    /* NTC 标称阻值 Ω @25℃ */
#define NTC_BETA          3950.0f     /* B 值 */
#define NTC_T0_K          298.15f     /* 25℃ 开尔文 */
#define ADC_FULL_SCALE    4095.0f
#define ADC_RAIL_LOW      5u          /* 贴轨判定：低于此视为对地短路/未接 */
#define ADC_RAIL_HIGH     4090u       /* 贴轨判定：高于此视为开路/未接 */

/* ---------------- 模拟热源参数（一阶惯性） ---------------- */
#define SIM_AMBIENT_C     25.0f       /* 环境温度 */
#define SIM_HEAT_GAIN     40.0f       /* 100% 占空比的稳态温升 ℃ */
#define SIM_TAU_NATURAL   60.0f       /* 自然散热时间常数 s */
#define SIM_TAU_FAN       25.0f       /* 风扇散热时间常数 s */
#define SIM_FAN_DERATE    0.6f        /* 风扇开启时加热增益系数 */

static uint8_t  s_simulated   = 1;    /* 默认模拟，自检通过才切真实 */
static float    s_sim_temp    = SIM_AMBIENT_C;
static uint32_t s_sim_last_ms = 0;

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
  */
static float ntc_raw_to_celsius(uint16_t raw)
{
  float r_ntc = NTC_R_FIXED * ((float)raw / (ADC_FULL_SCALE - (float)raw));
  float t_k   = 1.0f / (1.0f / NTC_T0_K + logf(r_ntc / NTC_R0) / NTC_BETA);
  return t_k - 273.15f;
}

/* ---------------- 底层 B：软件模拟 ---------------- */

/**
  * @brief 按一阶惯性环节推进模拟温度
  *        dt 用 HAL_GetTick 实测间隔，模拟精度不依赖调用周期是否均匀
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

/* ---------------- 统一接口 ---------------- */

void Sensor_Init(void)
{
  uint16_t raw;

  MX_ADC1_Init();
  HAL_ADCEx_Calibration_Start(&hadc1);   /* F1 上电自校准，提高绝对精度 */

  /* 上电自检：贴轨判定 NTC 是否接入 */
  raw = ntc_read_raw();
  if (raw > ADC_RAIL_LOW && raw < ADC_RAIL_HIGH)
  {
    s_simulated = 0;                     /* 分压点电平合理：真实模式 */
  }
  else
  {
    s_simulated = 1;                     /* 未接/异常：模拟模式 */
  }

  s_sim_temp    = SIM_AMBIENT_C;
  s_sim_last_ms = HAL_GetTick();
}

Sensor_Data_t Sensor_Read(void)
{
  Sensor_Data_t d;

  d.humidity_rh  = -1.0f;                /* NTC 无湿度功能 */
  d.is_simulated = s_simulated;
  d.is_valid     = 1;

  if (s_simulated)
  {
    sim_update();
    d.temperature_c = s_sim_temp;
  }
  else
  {
    uint16_t raw = ntc_read_raw();
    if (raw == 0xFFFFu || raw <= ADC_RAIL_LOW || raw >= ADC_RAIL_HIGH)
    {
      /* 运行中失联/贴轨：标无效，由上层（后续阶段的控制状态机）决定关加热 */
      d.temperature_c = 0.0f;
      d.is_valid      = 0;
    }
    else
    {
      d.temperature_c = ntc_raw_to_celsius(raw);
    }
  }
  return d;
}

void Sensor_ForceSimulation(uint8_t enable)
{
  s_simulated   = enable ? 1u : 0u;
  s_sim_temp    = SIM_AMBIENT_C;
  s_sim_last_ms = HAL_GetTick();
}

uint8_t Sensor_IsSimulated(void)
{
  return s_simulated;
}
