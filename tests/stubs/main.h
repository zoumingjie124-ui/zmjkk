/* 宿主机测试专用的 main.h 桩：控制层 (control.c) 只需要 stdint；
   真实 main.h 带 HAL 头文件，不能在 PC 上编译。 */
#ifndef __MAIN_STUB_H
#define __MAIN_STUB_H
#include <stdint.h>
#endif
