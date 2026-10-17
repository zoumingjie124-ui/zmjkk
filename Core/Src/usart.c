/**
  ******************************************************************************
  * @file    usart.c
  * @brief   USART1 驱动：1 字节中断接收 + 128 字节环形缓冲
  *
  * 为什么用环形缓冲：中断里只搬 1 字节立刻退出，协议解析放在主循环
  * （Comm_Tick）里做，接收不会因为解析耗时丢数据（缓冲溢出会计数）。
  ******************************************************************************
  */
#include "usart.h"

#define RX_RING_SIZE  128u

UART_HandleTypeDef huart1;

static uint8_t           s_rx_byte;                  /* 中断接收暂存 */
static volatile uint8_t  s_ring[RX_RING_SIZE];
static volatile uint16_t s_head;                     /* 写入位置（中断改） */
static volatile uint16_t s_tail;                     /* 读取位置（主循环改） */
static uint32_t           s_overflow;

void MX_USART1_UART_Init(void)
{
  huart1.Instance          = USART1;
  huart1.Init.BaudRate     = 115200;
  huart1.Init.WordLength   = UART_WORDLENGTH_8B;
  huart1.Init.StopBits     = UART_STOPBITS_1;
  huart1.Init.Parity       = UART_PARITY_NONE;
  huart1.Init.Mode         = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

void Usart_StartRx(void)
{
  s_head     = 0u;
  s_tail     = 0u;
  s_overflow = 0u;
  (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
}

/**
  * @brief 接收完成回调（中断上下文）：1 字节入环，立即重新武装
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  uint16_t next;

  if (huart->Instance == USART1)
  {
    next = (uint16_t)((s_head + 1u) % RX_RING_SIZE);
    if (next != s_tail)
    {
      s_ring[s_head] = s_rx_byte;
      s_head = next;
    }
    else
    {
      s_overflow++;              /* 缓冲满：丢弃新字节并计数 */
    }
    (void)HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u);
  }
}

/**
  * @brief 主循环取字节
  * @retval 1 = 取到；0 = 空
  */
uint8_t Usart_ReadByte(uint8_t *b)
{
  if (s_tail == s_head)
  {
    return 0u;
  }
  *b = s_ring[s_tail];
  s_tail = (uint16_t)((s_tail + 1u) % RX_RING_SIZE);
  return 1u;
}

void Usart_Send(const uint8_t *data, uint16_t len)
{
  /* 阻塞发送：单帧最长 40 字节 = 3.5ms @115200，可接受的短阻塞 */
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 100u);
}

uint32_t Usart_OverflowCount(void)
{
  return s_overflow;
}
