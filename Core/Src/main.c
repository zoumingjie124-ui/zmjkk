/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   智能温控与故障监测终端 —— 程序入口（阶段 2+3：传感器 + 加热/风扇驱动）
  *
  * 初始化顺序：
  *   1. HAL_Init / SystemClock_Config   72MHz + ADC 时钟 12MHz
  *   2. MX_GPIO_Init                    加热器/风扇先写低再配输出
  *   3. MX_TIM1_Init + Heater_Init      PWM 启动但占空比 0
  *   4. Sensor_Init                     上电自检：有 NTC 走真实模式，否则走模拟
  *   5. OLED_Init                       显示
  *   6. while(1)                        非阻塞时间片调度
  *
  * 时间片：
  *   500ms —— Sensor_Read + OLED 刷新（温度/加热功率/风扇/SIM-REAL）
  *   1000ms —— 运行秒数
  *
  * 演示序列（DEMO）：无按键阶段的联调脚本，阶段 6 按键接管后删除。
  *   警告：若 PA8 已接驱动电路，演示会真实输出 PWM！接入真实加热器前
  *         只允许 LED 假负载。不需要演示时把 DEMO_ENABLE 改为 0。
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "tim.h"
#include "gpio.h"
#include "oled.h"
#include "sensor.h"
#include "heater.h"
#include "fan.h"

/* Private defines -----------------------------------------------------------*/
#define DEMO_ENABLE   1     /* 联调演示序列开关：1=启用，0=加热/风扇全程关闭 */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void Display_SensorData(const Sensor_Data_t *d);
static void Demo_Sequence(uint32_t uptime_s);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  MX_TIM1_Init();
  Heater_Init();            /* PWM 已启动，占空比 0 */
  Fan_Init();               /* 风扇关 */

  Sensor_Init();            /* 上电自检决定 SIM/REAL，结果马上显示出来 */

  OLED_Init();
  OLED_Clear();

  uint32_t last_500ms  = HAL_GetTick();
  uint32_t last_1000ms = HAL_GetTick();
  uint32_t uptime_s    = 0;

  while (1)
  {
    uint32_t now = HAL_GetTick();

    if ((now - last_500ms) >= 500u)          /* 传感器 + 显示时间片 */
    {
      last_500ms += 500u;
      Sensor_Data_t d = Sensor_Read();
      Display_SensorData(&d);
    }

    if ((now - last_1000ms) >= 1000u)        /* 秒计数 + 演示序列时间片 */
    {
      last_1000ms += 1000u;
      uptime_s++;
      OLED_ShowNum(4, 8, uptime_s, 5);
#if DEMO_ENABLE
      Demo_Sequence(uptime_s);
#endif
    }
  }
}

/**
  * @brief OLED 刷新：第一行必须带 SIM/REAL 标识（约束 3）
  *
  * 布局（16 列 × 4 行）：
  *   L1: "SIM  T:25.3C" / "REAL T:25.3C" / 无效时 "REAL T: ERR "
  *   L2: "HEAT:030%"
  *   L3: "FAN: ON " / "FAN: OFF"
  *   L4: "UPTIME:00012"（秒数在 1s 时间片更新）
  */
static void Display_SensorData(const Sensor_Data_t *d)
{
  if (d->is_simulated)
  {
    OLED_ShowString(1, 1, "SIM  ");
  }
  else
  {
    OLED_ShowString(1, 1, "REAL ");
  }

  if (!d->is_valid)
  {
    OLED_ShowString(1, 7, "T:  ERR   ");
  }
  else
  {
    /* 一位小数显示：25.3 -> "25.3"；负温度超出本项目场景，直接显示 ERR */
    int32_t t10 = (int32_t)(d->temperature_c * 10.0f + 0.5f);
    if (t10 < 0 || t10 > 999)
    {
      OLED_ShowString(1, 7, "T:  ERR   ");
    }
    else
    {
      OLED_ShowString(1, 7, "T:");
      OLED_ShowNum(1, 9,  (uint32_t)(t10 / 10), 2);
      OLED_ShowChar(1, 11, '.');
      OLED_ShowNum(1, 12, (uint32_t)(t10 % 10), 1);
      OLED_ShowChar(1, 13, 'C');
    }
  }

  OLED_ShowString(2, 1, "HEAT:");
  OLED_ShowNum(2, 6, Heater_GetDuty(), 3);
  OLED_ShowChar(2, 9, '%');

  OLED_ShowString(3, 1, Fan_IsOn() ? "FAN: ON " : "FAN: OFF");
  OLED_ShowString(4, 1, "UPTIME:");
}

/**
  * @brief 联调演示序列：验证 PWM 输出、风扇开关、模拟热响应
  *         0-10s 全部关闭 | 10-40s 加热 30% | 40-70s 加热 50%+风扇
  *         | 70s 起全部关闭保持
  *        阶段 6 按键接管后删除本函数。
  */
static void Demo_Sequence(uint32_t uptime_s)
{
  if (uptime_s < 10u)
  {
    Heater_SetDuty(0);
    Fan_Off();
  }
  else if (uptime_s < 40u)
  {
    Heater_SetDuty(30);
  }
  else if (uptime_s < 70u)
  {
    Heater_SetDuty(50);
    Fan_On();
  }
  else
  {
    Heater_Off();
    Fan_Off();
  }
}

/**
  * @brief HSE 8MHz -> PLL x9 -> 72MHz；ADC 时钟 = PCLK2/6 = 12MHz（<=14MHz）
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }

  __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);
}

/**
  * @brief 错误处理：停在这里而不是继续跑（调试时打断点定位）
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
