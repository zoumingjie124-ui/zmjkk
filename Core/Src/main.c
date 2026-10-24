/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   基于 STM32 的智能温控与故障监测终端 —— 程序入口
  *           （阶段 6/7/8：按键菜单 + Flash 参数 + USART1 上位机通信）
  *
  * 初始化顺序：
  *   1. HAL_Init / SystemClock_Config   72MHz + ADC 时钟 12MHz
  *   2. MX_GPIO_Init                    加热器/风扇先写低再配输出
  *   3. Sensor_Init                     上电自检决定 is_valid（含 ADC 自校准）；
  *                                      SIM 模式已改为编译期开关 SENSOR_SIM_ENABLE
  *   4. MX_TIM1_Init + Control_Init      状态机从 INIT 起步，加热/风扇全关
  *   5. Param_Load                      读 Flash 末页，校验失败回默认
  *   6. Menu_Init / USART1 / Comm_Init   按键与通信
  *   7. MX_IWDG_Init                    看门狗（必须在所有初始化之后启动）
  *   8. while(1)                        三档非阻塞时间片，每轮开头喂狗
  *
  * 时间片：
  *   10ms  —— Key_Scan + Menu_Tick + Comm_Tick（按键/协议解析，全部非阻塞）
  *   500ms —— Control_Tick（联锁→PID→风扇）+ Menu_Render（整屏刷新）
  *   1000ms—— 运行秒计数
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
#include "menu.h"
#include "key.h"
#include "comm.h"
#include "usart.h"
#include "flash_param.h"
#include "param_codec.h"
#include "iwdg.h"

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

int main(void)
{
  uint32_t last_10ms   = 0u;
  uint32_t last_500ms  = 0u;
  uint32_t last_1000ms = 0u;
  uint32_t uptime_s    = 0u;

  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();

  Sensor_Init();            /* 传感器自检：决定 is_valid（SIM 由编译期开关决定） */
  MX_TIM1_Init();           /* TIM1 PWM 必须在 Heater_Init 之前 */
  Control_Init();           /* INIT 状态，加热 0%、风扇关 */

  {
    /* Flash 参数加载：首次上电或校验失败会自动回默认值 */
    app_param_t  cfg;
    ctrl_params_t cp;

    (void)Param_Load(&cfg);
    cp.target_c = cfg.target_c;
    cp.kp       = cfg.kp;
    cp.ki       = cfg.ki;
    cp.kd       = cfg.kd;
    Control_ApplyParams(&cp);   /* 内部再校验一次范围，脏数据不会生效 */
  }

  Menu_Init();
  MX_USART1_UART_Init();
  Comm_Init();

  OLED_Init();
  OLED_Clear();
  Usart_StartRx();            /* 接收中断最后开启 */

  last_10ms   = HAL_GetTick();
  last_500ms  = last_10ms;
  last_1000ms = last_10ms;

  /* 看门狗最后启动：前面所有初始化都已完成，不会把慢速初始化误判成死机。
     一旦启动无法关闭，复位后 PA8 回到浮空输入（板上 10k 下拉），加热器关断。 */
  MX_IWDG_Init();

  while (1)
  {
    uint32_t now = HAL_GetTick();

    /* 喂狗放在分支之前：只要主循环还在转就一定喂到；
       卡在 Key_Scan / Comm_Tick / Control_Tick / Menu_Render 任一处都会停止喂狗 */
    IWDG_Feed();

    if ((now - last_10ms) >= 10u)          /* 按键 + 通信时间片 */
    {
      /* 用 "= now" 而不是 "+="：丢弃错过的时间片，绝不补执行。
         否则一次长阻塞（UART 阻塞发送、Flash 擦写）会让下一个循环连续
         触发多次 Control_Tick，而 PID 内部仍按 Ts=0.5s 计算 -> 等效增益放大 */
      last_10ms = now;
      Key_Scan();
      Menu_Tick();
      Comm_Tick();
    }

    if ((now - last_500ms) >= 500u)        /* 控制 + 显示时间片 */
    {
      last_500ms = now;
      Control_Tick();
      Menu_Render(uptime_s);
    }

    if ((now - last_1000ms) >= 1000u)      /* 秒计数 */
    {
      last_1000ms = now;
      uptime_s++;
    }
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
  * @brief 错误处理：停在这里而不是继续跑（调试时在此打断点）
  */
void Error_Handler(void)
{
  __disable_irq();
  /* 异常兜底：无需苦等 1s 看门狗超时，进入死循环前微秒级切断 TIM1 主输出使能（MOE）
     并拉低 PA8、拉高 PB0（开风扇），同时防止 MX_IWDG_Init 之前报错导致无看门狗带载停机 */
  TIM1->BDTR &= ~TIM_BDTR_MOE;
  GPIOA->BRR  = HEATER_Pin;
  GPIOB->BSRR = FAN_Pin;
  while (1)
  {
  }
}
