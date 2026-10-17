/**
  ******************************************************************************
  * @file    menu.h
  * @brief   按键菜单：启停/故障确认/在线调参/参数保存 + OLED 整屏渲染
  *
  * 按键定义：
  *   KEY1 短按  —— STANDBY/RUNNING 切换；FAULT 状态下为"故障确认"
  *   KEY2 短按  —— 切换编辑项：TGT -> KP -> KI -> KD -> 关闭编辑
  *   KEY2 长按  —— 保存当前参数到 Flash
  *   KEY3 短按  —— 当前项 +（目标 0.5℃，Kp 0.5，Ki 0.01，Kd 0.1）
  *   KEY3 长按  —— 当前项 -
  ******************************************************************************
  */
#ifndef __MENU_H
#define __MENU_H

#include "main.h"

void Menu_Init(void);
void Menu_Tick(void);                  /* 每 10ms：消费按键事件 */
void Menu_Render(uint32_t uptime_s);   /* 每 500ms：整屏刷新 */

#endif /* __MENU_H */
