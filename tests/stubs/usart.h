/* 宿主机测试专用的 usart.h 桩。
   真的 Core/Inc/usart.h 会经 main.h 拉进 HAL 头文件，PC 上编不了。
   这里只保留 comm.c 用到的部分，接口签名必须与真头保持一致——
   改 Core/Inc/usart.h 的接口时，这个桩要同步改，否则测试会与固件脱节。 */
#ifndef __USART_STUB_H
#define __USART_STUB_H

#include "main.h"

#define USART_RX_RING_SIZE  128u

uint8_t  Usart_ReadByte(uint8_t *b);
void     Usart_Send(const uint8_t *data, uint16_t len);
uint32_t Usart_OverflowCount(void);
uint32_t Usart_RxByteCount(void);

#endif /* __USART_STUB_H */
