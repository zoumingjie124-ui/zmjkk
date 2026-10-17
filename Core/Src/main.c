/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   智能温控与故障监测终端 —— 程序入口
  *           （阶段 4+5：PID 闭环 + 控制状态机 + 安全联锁）
  *
  * 初始化顺序：
  *   1. HAL_Init / SystemClock_Config   72MHz + ADC 时钟 12MHz
  *   2. MX_GPIO_Init                    加热器/风扇先写低再配输出
  *   3. Control_Init                    状态机 + PID + TIM1 PWM + 风扇（全关）
  *   4. Sensor_Init                     上电自检决定 SIM/REAL 模式
  *   5. OLED_Init                       显示
  *   6. while(1)                        非阻塞时间片调度
  *
  * 时间片：
  *   500ms —— Control_Tick()（传感器读取 + 联锁检查 + PID + 风扇）+ OLED 刷新
  *   1000ms —— 运行秒数
  *
  * 本阶段目标温度默认 45.0℃（control.h: CTRL_TARGET_DEFAULT_C），用于观察
  * 模拟/真实闭环收敛；阶段 6 由按键接管启停与设温。
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "tim.h"
#include "gpio.h"
#include "oled.h"
#include "sensor.h"
#include "control.h"

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void Display_Snapshot(void);
static void Display_Temp1(uint8_t line, uint8_t col, float temp_c);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  Sensor_Init();            /* 先确定 SIM/REAL（含 ADC 自校准） */
  Control_Init();           /* 状态机从 INIT 起步，加热/风扇默认全关 */

  OLED_Init();
  OLED_Clear();

  uint32_t last_500ms  = HAL_GetTick();
  uint32_t last_1000ms = HAL_GetTick();
  uint32_t uptime_s    = 0;

  while (1)
  {
    uint32_t now = HAL_GetTick();

    if ((now - last_500ms) >= 500u)          /* 控制周期 = 500ms */
    {
      last_500ms += 500u;
      Control_Tick();                        /* 联锁→PID→风扇，一个 tick 全做 */
      Display_Snapshot();
    }

    if ((now - last_1000ms) >= 1000u)        /* 秒计数时间片 */
    {
      last_1000ms += 1000u;
      uptime_s++;
      OLED_ShowNum(4, 11, uptime_s, 5);
    }
  }
}

/**
  * @brief OLED 刷新（16 列 × 4 行）：
  *   L1: "RUN  T:44.7C"   状态(INI/STB/RUN/FLT) + 当前温度
  *   L2: "SET :45.0C"     目标温度
  *   L3: "H:078% FAN:OFF" 加热占空比 + 风扇
  *   L4: "SIM  UP:00123"  数据源标识 + 运行秒数
  *   温度无效时 L1 显示 "T:  ERR"。SIM 标识永不隐藏（诚信约束）。
  */
static void Display_Snapshot(void)
{
  const ctrl_snapshot_t *s = Control_Get();

  OLED_ShowString(1, 1, Control_StateName());
  OLED_ShowChar(1, 4, ' ');
  if (s->temp_valid)
  {
    Display_Temp1(1, 7, s->temp_c);
  }
  else
  {
    OLED_ShowString(1, 7, "T:  ERR   ");
  }

  OLED_ShowString(2, 1, "SET :");
  Display_Temp1(2, 6, s->target_c);

  OLED_ShowString(3, 1, "H:");
  OLED_ShowNum(3, 3, s->duty, 3);
  OLED_ShowString(3, 6, "% FAN:");
  OLED_ShowString(3, 11, s->fan_on ? "ON " : "OFF");

  OLED_ShowString(4, 1, s->is_simulated ? "SIM  " : "REAL ");
  OLED_ShowString(4, 6, "UP:");
}

/**
  * @brief 温度一位小数显示（覆盖原位置 12 字符宽，避免残留旧字符）
  * @note  负温度与超 99.9℃ 不在本项目显示范围，统一显示 ERR
  */
static void Display_Temp1(uint8_t line, uint8_t col, float temp_c)
{
  int32_t t10 = (int32_t)(temp_c * 10.0f + 0.5f);
  if (t10 < 0 || t10 > 999)
  {
    OLED_ShowString(line, col, "    ERR   ");
    return;
  }
  OLED_ShowNum(line, col,     (uint32_t)(t10 / 10), 2);
  OLED_ShowChar(line, col + 2, '.');
  OLED_ShowNum(line, col + 3, (uint32_t)(t10 % 10), 1);
  OLED_ShowChar(line, col + 4, 'C');
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
