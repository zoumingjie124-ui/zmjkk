/**
  ******************************************************************************
  * @file    key.c
  * @brief   按键状态机：每 10ms 推进一次，无任何阻塞延时
  *
  * 状态机（每个按键独立）：
  *
  *   IDLE ──按下──> DEBOUNCE_DOWN ──稳定20ms──> PRESSED ──松手──> DEBOUNCE_UP
  *                      │                        │                    │
  *                      └──抖动回弹──> IDLE       ├──按住1s──> LONG_SENT ──松手──> DEBOUNCE_UP
  *                                               └──松手(短按)──> 产生 SHORT 事件
  *
  * 事件产生时机：
  *   短按 = 在 PRESSED 态松手（保证松手才触发，避免按下瞬间误动作）
  *   长按 = 在 PRESSED 态累计满 KEY_LONG_TICKS（不等松手，手感更跟手）
  ******************************************************************************
  */
#include "key.h"

#define KEY_DEBOUNCE_TICKS  2u      /* 2 × 10ms = 20ms 消抖 */
#define KEY_LONG_TICKS      100u    /* 100 × 10ms = 1s 长按 */

typedef enum
{
  KS_IDLE = 0,
  KS_DEBOUNCE_DOWN,
  KS_PRESSED,
  KS_LONG_SENT,
  KS_DEBOUNCE_UP
} key_state_t;

typedef struct
{
  key_state_t state;
  uint16_t    ticks;
  key_event_t event;      /* 待消费事件 */
} key_fsm_t;

static key_fsm_t s_keys[KEY_ID_MAX];

/* 引脚号集中在这里；极性：按下接地 = 低电平 */
static uint16_t key_pin(key_id_t id)
{
  switch (id)
  {
    case KEY_ID_1: return KEY1_Pin;
    case KEY_ID_2: return KEY2_Pin;
    default:       return KEY3_Pin;
  }
}

static uint8_t key_is_pressed(key_id_t id)
{
  return (HAL_GPIO_ReadPin(GPIOB, key_pin(id)) == GPIO_PIN_RESET) ? 1u : 0u;
}

void Key_Init(void)
{
  uint8_t i;
  for (i = 0; i < (uint8_t)KEY_ID_MAX; i++)
  {
    s_keys[i].state = KS_IDLE;
    s_keys[i].ticks = 0u;
    s_keys[i].event = KEY_EVENT_NONE;
  }
  /* 引脚已由 MX_GPIO_Init 配置为内部上拉输入，这里不再重复配置 */
}

void Key_Scan(void)
{
  uint8_t i;

  for (i = 0; i < (uint8_t)KEY_ID_MAX; i++)
  {
    key_fsm_t *k     = &s_keys[i];
    uint8_t    down  = key_is_pressed((key_id_t)i);

    switch (k->state)
    {
      case KS_IDLE:
        if (down)
        {
          k->state = KS_DEBOUNCE_DOWN;
          k->ticks = 0u;
        }
        break;

      case KS_DEBOUNCE_DOWN:
        if (down)
        {
          if (++k->ticks >= KEY_DEBOUNCE_TICKS)
          {
            k->state = KS_PRESSED;
            k->ticks = 0u;
          }
        }
        else
        {
          k->state = KS_IDLE;          /* 抖动，判为无效 */
          k->ticks = 0u;
        }
        break;

      case KS_PRESSED:
        if (down)
        {
          if (++k->ticks >= KEY_LONG_TICKS)
          {
            k->event = KEY_EVENT_LONG;
            k->state = KS_LONG_SENT;
          }
        }
        else
        {
          k->event = KEY_EVENT_SHORT;  /* 松手产生短按 */
          k->state = KS_DEBOUNCE_UP;
          k->ticks = 0u;
        }
        break;

      case KS_LONG_SENT:
        if (!down)
        {
          k->state = KS_DEBOUNCE_UP;
          k->ticks = 0u;
        }
        break;

      case KS_DEBOUNCE_UP:
        if (!down)
        {
          if (++k->ticks >= KEY_DEBOUNCE_TICKS)
          {
            k->state = KS_IDLE;
          }
        }
        else
        {
          k->state = KS_PRESSED;       /* 抖动回来，重新按下（不重复发短按） */
          k->ticks = 0u;
        }
        break;

      default:
        k->state = KS_IDLE;
        break;
    }
  }
}

key_event_t Key_GetEvent(key_id_t id)
{
  key_event_t e;

  if (id >= KEY_ID_MAX)
  {
    return KEY_EVENT_NONE;
  }
  e = s_keys[id].event;
  s_keys[id].event = KEY_EVENT_NONE;   /* 消费即清除 */
  return e;
}
