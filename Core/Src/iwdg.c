/**
  ******************************************************************************
  * @file    iwdg.c
  * @brief   独立看门狗实现。
  ******************************************************************************
  */
#include "iwdg.h"
#include "main.h"

static IWDG_HandleTypeDef hiwdg;
static uint8_t s_wdg_reset;      /* 本 boot 是否由看门狗复位引起 */

/**
  * @brief  读取并清除复位标志，记录本次复位原因
  * @note   必须在 MX_IWDG_Init() 之前调用：IWDG 一旦启动，
  *         后续复位标志的语义就不再是"上次"了。
  */
static void capture_reset_cause(void)
{
  s_wdg_reset = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) ? 1u : 0u;

  /* 把复位标志清掉，否则下次上电仍会读到旧值，误报"看门狗复位" */
  __HAL_RCC_CLEAR_RESET_FLAGS();
}

void MX_IWDG_Init(void)
{
  capture_reset_cause();

  /* LSI 40kHz / 64 = 625Hz；重装 625 -> 约 1.0s */
  hiwdg.Instance       = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg.Init.Reload    = 625u;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }

  /* HAL_IWDG_Init 内部先写 0xCCCC 启动、再写 0xAAAA 重装，
     返回时狗已经在跑了，所以下面紧接着喂一次，避免初始化剩余时间被计入 */
  (void)HAL_IWDG_Refresh(&hiwdg);
}

void IWDG_Feed(void)
{
  (void)HAL_IWDG_Refresh(&hiwdg);
}

uint8_t IWDG_ResetWasWatchdog(void)
{
  return s_wdg_reset;
}
