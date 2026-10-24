/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   中断服务程序：只做"转交 HAL"这一件事，业务逻辑不写在这里。
  *          第一阶段没有外设中断；后续阶段在此添加 USART1/TIM 等转交。
  ******************************************************************************
  */
#include "main.h"
#include "stm32f1xx_it.h"
#include "usart.h"

/* ---------------------- Cortex-M3 内核异常 ---------------------- */
void NMI_Handler(void)
{
  while (1) { }
}

void HardFault_Handler(void)
{
  /* 硬件异常瞬间立即关闭 TIM1 主输出（MOE=0）并拉低 PA8，随后等待 IWDG 硬件复位 */
  TIM1->BDTR &= ~TIM_BDTR_MOE;
  GPIOA->BRR  = HEATER_Pin;
  GPIOB->BSRR = FAN_Pin;
  while (1) { }
}

void MemManage_Handler(void)
{
  while (1) { }
}

void BusFault_Handler(void)
{
  while (1) { }
}

void UsageFault_Handler(void)
{
  while (1) { }
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
