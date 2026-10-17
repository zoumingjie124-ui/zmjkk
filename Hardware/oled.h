/**
  ******************************************************************************
  * @file    oled.h
  * @brief   OLED(SSD1306 兼容, I2C 地址 0x78) 显示 API
  *          移植自课程例程 4-1（标准库软件 I2C），本工程改为 HAL + PB6/PB7
  ******************************************************************************
  */
#ifndef __OLED_H
#define __OLED_H

#include "main.h"

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

void OLED_ShowChinese(uint8_t Line, uint8_t Column, uint8_t Index);

#endif
