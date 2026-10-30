/* 宿主机测试专用的 main.h 桩：只提供被测模块真正需要的最小符号。
   真实 main.h 带 HAL 头文件，不能在 PC 上编译。

   注意：本文件通过 -I stubs 遮蔽真的 Core/Inc/main.h。被测模块一旦新增
   HAL 依赖，这里必须同步补桩，否则宿主机构建会断——这是有意暴露的耦合点。 */
#ifndef __MAIN_STUB_H
#define __MAIN_STUB_H
#include <stdint.h>

/* comm.c 计时用（定时上报 + 帧内字节间隔超时）。由测试用例控制返回值。 */
uint32_t HAL_GetTick(void);

/* usart.h 里有 `extern UART_HandleTypeDef huart1;`，宿主机不需要真实外设，
   给一个占位类型让这条声明成立即可。 */
typedef struct
{
  void *Instance;
} UART_HandleTypeDef;

#endif /* __MAIN_STUB_H */
