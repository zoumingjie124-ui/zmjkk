/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   智能温控与故障监测终端 —— 程序入口（第一阶段：工程骨架 + OLED 固定显示）
  *
  * 初始化顺序（顺序错了会 hardfault 或外设不工作）：
  *   1. HAL_Init()           重置外设、配置 SysTick 1ms 时基
  *   2. SystemClock_Config() HSE 8MHz -> PLL -> 72MHz
  *   3. MX_GPIO_Init()       加热器/风扇先写低再配输出（安全默认关闭）
  *   4. OLED_Init()          显示固定文字，验证显示链路
  *   5. while(1)             非阻塞调度骨架：用 HAL_GetTick 分时间片，禁止长 Delay
  *
  * 本阶段安全状态：PA8(加热)=低、PB0(风扇)=低，全程不变。
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "gpio.h"
#include "oled.h"

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

int main(void)
{
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  /* OLED 上电初始化并显示固定文字 —— 第一阶段的验收依据 */
  OLED_Init();
  OLED_ShowString(1, 1, "TEMP-CTRL  v0.1");
  OLED_ShowString(2, 1, "STAGE1 SKELETON");
  OLED_ShowString(3, 1, "UPTIME:");

  /* 非阻塞调度骨架：记录上一次任务执行时刻，主循环不调用长时间 HAL_Delay。
     后续阶段的任务（传感器采样 500ms、PID 计算、菜单刷新、串口上报）
     都按这个模式挂在主循环里。 */
  uint32_t last_tick = HAL_GetTick();
  uint32_t uptime_s  = 0;

  while (1)
  {
    uint32_t now = HAL_GetTick();
    if ((now - last_tick) >= 1000u)   /* 1s 时间片：刷新运行秒数 */
    {
      last_tick += 1000u;             /* 累加而不是赋值 now，避免周期漂移 */
      uptime_s++;
      OLED_ShowNum(3, 8, uptime_s, 5);
    }
    /* 其余时间片（按键扫描 10ms / 传感器 500ms ...）后续阶段添加 */
  }
}

/**
  * @brief HSE 8MHz -> PLL x9 -> 72MHz（与 DataTerm 一致；若板子无外部晶振需改 HSI）
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;   /* APB1 = 36MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;   /* APB2 = 72MHz（TIM1 用） */

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief 错误处理：停在这里而不是继续跑。
  *        调试时在此打断点，通常是时钟配置失败（如外部晶振不起振）。
  */
void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
