/**
  ******************************************************************************
  * @file    key.h
  * @brief   按键驱动：PB12/PB13/PB14，非阻塞状态机消抖，识别单击/长按
  *
  * 设计要点：
  *   - 严禁 HAL_Delay：全靠 10ms 节拍推进状态机
  *   - 消抖 20ms（2 个节拍），长按 1s（100 个节拍），阈值都做成宏
  *   - 事件"消费式"读取：Key_GetEvent 取走并清除，避免重复触发
  ******************************************************************************
  */
#ifndef __KEY_H
#define __KEY_H

#include "main.h"

typedef enum
{
  KEY_EVENT_NONE = 0,
  KEY_EVENT_SHORT,     /* 短按：按下后 1s 内释放 */
  KEY_EVENT_LONG       /* 长按：按住满 1s（只触发一次，松手不再产生短按） */
} key_event_t;

typedef enum
{
  KEY_ID_1 = 0,        /* PB12 */
  KEY_ID_2,            /* PB13 */
  KEY_ID_3,            /* PB14 */
  KEY_ID_MAX
} key_id_t;

void        Key_Init(void);
void        Key_Scan(void);                  /* 必须每 10ms 调用一次 */
key_event_t Key_GetEvent(key_id_t id);       /* 读取并清除事件 */

#endif /* __KEY_H */
