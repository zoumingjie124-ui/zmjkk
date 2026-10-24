/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   中断服务程序：常规中断只负责"转交 HAL"；异常处理只做"最后关断"。
  *
  * 安全约定（第三轮审查整改）：
  *   所有会进入死循环的致命异常（NMI / HardFault / MemManage / BusFault /
  *   UsageFault）必须执行同一套关断序列 Heater_PanicOff()，不允许部分路径
  *   关输出、部分路径不关——否则一旦落进没处理的那条，PWM 会保持最后占空比
  *   直到 IWDG 复位（最坏约 1.33s）。
  *
  * 为什么不能只写 GPIOA->BRR：PA8 在 AF 模式下由 TIM1 驱动，GPIO 输出寄存器
  * 被旁路，写 BRR 无效。Heater_PanicOff() 会先把 PA8 切回通用推挽输出再拉低。
  ******************************************************************************
  */
#include "main.h"
#include "stm32f1xx_it.h"
#include "usart.h"
#include "heater.h"

/* ---------------------- Cortex-M3 内核异常 ---------------------- */

/**
  * @brief 统一的致命异常关断 + 停机等待 IWDG
  * @note  只做寄存器级操作，不调用 HAL，避免在异常上下文里再次触发故障
  */
static void fatal_stop(void)
{
  Heater_PanicOff();      /* MOE=0 + 停计数 + PA8 切 GPIO 输出低 + 风扇开 + __DSB */
  while (1)
  {
  }
}

void NMI_Handler(void)
{
  fatal_stop();
}

void HardFault_Handler(void)
{
  fatal_stop();
}

void MemManage_Handler(void)
{
  fatal_stop();
}

void BusFault_Handler(void)
{
  fatal_stop();
}

void UsageFault_Handler(void)
{
  fatal_stop();
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/**
  * @brief SysTick 1ms 中断：驱动 HAL 时基，HAL_GetTick() 全靠它
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/* ---------------------- 外设中断 ------------------------------- */

/**
  * @brief USART1 中断：收发中断全部转交 HAL
  */
void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart1);
}
