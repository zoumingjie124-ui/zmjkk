/**
  ******************************************************************************
  * @file    heater.c
  * @brief   加热器驱动：PA8 = TIM1_CH1，1kHz PWM 调功 + 软件输出许可（闸门）
  *
  * 安全约束：
  *   - 上电默认：逻辑占空比 0、**输出闸门关闭**（CCR1 = 0）
  *   - 闸门关闭时，Caller 传任何占空比都只更新逻辑值，PA8 保持低电平
  *   - 所有占空比写入都经过 0~100 限幅
  *   - Heater_Off() 是故障保护的统一入口：清零逻辑值 + 关闸
  ******************************************************************************
  */
#include "heater.h"
#include "tim.h"

static uint8_t s_duty  = 0;   /* 逻辑占空比（PID 想要的功率） */
/* 软件输出许可（闸门）：0=强制 CCR=0（上电默认）。
   注意：这只是软件变量，不保证物理断电；CPU 跑飞靠 IWDG，物理断电靠栅极下拉/串联温控开关。 */
static uint8_t s_hw_en = 0;

/* 把"逻辑占空比 + 闸门状态"落到定时器：闸门关 -> CCR 恒 0 */
static void hw_apply(void)
{
  uint32_t ccr = 0u;

  if (s_hw_en)
  {
    ccr = (uint32_t)s_duty * 10u;   /* ARR=999：1% = 10 个计数 */
  }
  __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr);
}

void Heater_Init(void)
{
  s_duty  = 0u;
  s_hw_en = 0u;                     /* 上电绝不输出 */
  hw_apply();
  if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
}

void Heater_SetDuty(uint8_t duty)
{
  if (duty > 100u)
  {
    duty = 100u;                    /* 限幅：调用方传错也不能超过 100% */
  }
  s_duty = duty;
  hw_apply();
}

uint8_t Heater_GetDuty(void)
{
  return s_duty;
}

void Heater_EnableOutput(uint8_t enable)
{
  s_hw_en = (enable != 0u) ? 1u : 0u;
  hw_apply();
}

uint8_t Heater_OutputEnabled(void)
{
  return s_hw_en;
}

void Heater_Off(void)
{
  s_duty  = 0u;
  s_hw_en = 0u;
  hw_apply();
}

/**
  * @brief 致命异常的最后关断（裸寄存器，不依赖 HAL 与外设状态）
  * @note  顺序不可调换，理由见每步注释
  */
void Heater_PanicOff(void)
{
  /* 0) 确保 GPIO 时钟开启：若在 MX_GPIO_Init 之前出错调用，
        不开启时钟的话后面写 CRH/BRR 会被硬件忽略 */
  RCC->APB2ENR |= (RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN);

  /* 1) 先把定时器通道的空闲电平定为低：若 BDTR.OSSI=1，MOE=0 时会直接
        进入 OIS1 定义的空闲态；置 0 可避免依赖复位默认值 */
  TIM1->CR2 &= ~TIM_CR2_OIS1;

  /* 2) 关 TIM1 主输出（MOE=0） */
  TIM1->BDTR &= ~TIM_BDTR_MOE;

  /* 3) 停计数器，避免残余比较事件再次驱动输出 */
  TIM1->CR1 &= ~TIM_CR1_CEN;

  /* 4) 先把 GPIO 输出锁存器清零（此时仍是 AF 模式，写 ODR 不影响引脚，
        只是为下一步切换做准备）——不能放在切换之后，否则若 ODR 残留为 1，
        切到通用输出的瞬间会先输出一个高电平毛刺 */
  GPIOA->BRR = HEATER_Pin;

  /* 5) PA8：复用推挽 -> 通用推挽输出
        GPIOA->CRH 的 bit[3:0] 是 PA8：MODE=10(输出2MHz) CNF=00(通用推挽)
        单次读改写，避免中间出现"输入模式"的悬空窗口；
        切换瞬间 GPIO 驱动器接管引脚，因 ODR=0 而立即为低 */
  GPIOA->CRH = (GPIOA->CRH & ~(0xFu << 0)) | (0x2u << 0);

  /* 6) 风扇全开（PB0 若尚未配置为输出则无效果，属可接受降级） */
  GPIOB->BSRR = FAN_Pin;

  /* 7) 等写操作落地再进入死循环（由调用方 while(1) 等待 IWDG 复位） */
  __DSB();
}
