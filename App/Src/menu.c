/**
  ******************************************************************************
  * @file    menu.c
  * @brief   按键菜单与显示渲染
  ******************************************************************************
  */
#include "menu.h"
#include "key.h"
#include "control.h"
#include "oled.h"
#include "flash_param.h"
#include <string.h>

typedef enum
{
  EDIT_OFF = 0,
  EDIT_TGT,
  EDIT_KP,
  EDIT_KI,
  EDIT_KD,
  EDIT_COUNT
} edit_item_t;

static uint8_t s_edit = EDIT_OFF;
static char    s_msg[17];
static uint8_t s_msg_ttl;      /* 单位：500ms 渲染次数 */

static void msg_set(const char *m)
{
  strncpy(s_msg, m, 16);
  s_msg[16] = '\0';
  s_msg_ttl = 4u;              /* 显示约 2s */
}

void Menu_Init(void)
{
  s_edit    = EDIT_OFF;
  s_msg[0]  = '\0';
  s_msg_ttl = 0u;
}

/* 把 ±step 应用到当前编辑项（统一走 Control 的校验接口） */
static void adjust_current(int8_t dir)
{
  const ctrl_params_t *p = Control_GetParams();
  float t;

  switch (s_edit)
  {
    case EDIT_TGT:
      t = p->target_c + (dir > 0 ? 0.5f : -0.5f);
      if (Control_SetTarget(t) != 0u)
      {
        msg_set("TGT RANGE ERR");
      }
      break;

    case EDIT_KP:
      t = p->kp + (dir > 0 ? 0.5f : -0.5f);
      if (Control_SetPid(t, p->ki, p->kd) != 0u)
      {
        msg_set("KP RANGE ERR");
      }
      break;

    case EDIT_KI:
      t = p->ki + (dir > 0 ? 0.01f : -0.01f);
      if (Control_SetPid(p->kp, t, p->kd) != 0u)
      {
        msg_set("KI RANGE ERR");
      }
      break;

    case EDIT_KD:
      t = p->kd + (dir > 0 ? 0.1f : -0.1f);
      if (Control_SetPid(p->kp, p->ki, t) != 0u)
      {
        msg_set("KD RANGE ERR");
      }
      break;

    default:
      break;
  }
}

void Menu_Tick(void)
{
  key_event_t e1 = Key_GetEvent(KEY_ID_1);
  key_event_t e2 = Key_GetEvent(KEY_ID_2);
  key_event_t e3 = Key_GetEvent(KEY_ID_3);
  const ctrl_snapshot_t *s = Control_Get();

  /* ---- KEY1：启停 / 故障确认 ---- */
  if (e1 == KEY_EVENT_SHORT)
  {
    if (s->state == CTRL_FAULT)
    {
      Control_AckFault();
      msg_set("FAULT CLEARED");
    }
    else if (s->state == CTRL_RUNNING)
    {
      (void)Control_Stop();
      msg_set("STOPPED");
    }
    else if (s->state == CTRL_STANDBY)
    {
      (void)Control_Start();
      msg_set("STARTED");
    }
    else
    {
      /* INIT 状态忽略 */
    }
  }

  /* ---- KEY2：编辑项切换 / 长按保存 ---- */
  if (e2 == KEY_EVENT_SHORT && s->state != CTRL_FAULT)
  {
    s_edit = (uint8_t)((s_edit + 1u) % (uint8_t)EDIT_COUNT);
  }
  if (e2 == KEY_EVENT_LONG)
  {
    const ctrl_params_t *p = Control_GetParams();
    uint8_t ret = Param_SaveValues(p->target_c, p->kp, p->ki, p->kd);
    msg_set(ret == 0u ? "SAVED OK" : "SAVE FAIL");
  }

  /* ---- KEY3：步进调整 ---- */
  if (e3 == KEY_EVENT_SHORT && s_edit != EDIT_OFF)
  {
    adjust_current(+1);
  }
  if (e3 == KEY_EVENT_LONG && s_edit != EDIT_OFF)
  {
    adjust_current(-1);
  }
}

/* ---------------- 渲染辅助 ---------------- */

/* 温度 1 位小数：xx.xC（col 起 5 字符） */
static void draw_temp1(uint8_t line, uint8_t col, float v)
{
  int32_t t10 = (int32_t)(v * 10.0f + 0.5f);

  if (t10 < 0 || t10 > 999)
  {
    OLED_ShowString(line, col, "  ERR");
    return;
  }
  OLED_ShowNum(line, col, (uint32_t)(t10 / 10), 2);
  OLED_ShowChar(line, col + 2, '.');
  OLED_ShowNum(line, col + 3, (uint32_t)(t10 % 10), 1);
  OLED_ShowChar(line, col + 4, 'C');
}

/* 参数 2 位小数：xx.yy（col 起 5 字符） */
static void draw_float2(uint8_t line, uint8_t col, float v)
{
  int32_t x100 = (int32_t)(v * 100.0f + 0.5f);
  int32_t ip;
  int32_t fp;

  if (x100 < 0)
  {
    x100 = 0;
  }
  ip = x100 / 100;
  fp = x100 % 100;
  if (ip > 99)
  {
    ip = 99;
  }
  OLED_ShowNum(line, col, (uint32_t)ip, 2);
  OLED_ShowChar(line, col + 2, '.');
  OLED_ShowNum(line, col + 3, (uint32_t)fp, 2);
}

static const char *edit_name(void)
{
  switch (s_edit)
  {
    case EDIT_TGT: return "TGT";
    case EDIT_KP:  return "KP ";
    case EDIT_KI:  return "KI ";
    case EDIT_KD:  return "KD ";
    default:       return "---";
  }
}

void Menu_Render(uint32_t uptime_s)
{
  const ctrl_snapshot_t *s = Control_Get();
  const ctrl_params_t   *p = Control_GetParams();

  if (s_msg_ttl > 0u)
  {
    s_msg_ttl--;
  }

  /* ---- L1：状态 + 当前温度 ---- */
  OLED_ShowString(1, 1, Control_StateName());
  OLED_ShowChar(1, 4, ' ');
  if (s->temp_valid)
  {
    OLED_ShowString(1, 5, "T:");
    draw_temp1(1, 7, s->temp_c);
  }
  else
  {
    OLED_ShowString(1, 5, "T:  ERR   ");
  }

  /* ---- L2：目标温度 ---- */
  OLED_ShowString(2, 1, "SET:");
  draw_temp1(2, 5, p->target_c);

  /* ---- L3：正常页 = 占空比/风扇；编辑页 = 编辑项与数值 ---- */
  if (s_edit == EDIT_OFF)
  {
    OLED_ShowString(3, 1, "H:");
    OLED_ShowNum(3, 3, s->duty, 3);
    OLED_ShowChar(3, 6, '%');
    OLED_ShowString(3, 8, "F:");
    OLED_ShowString(3, 10, s->fan_on ? "ON " : "OFF");
  }
  else
  {
    OLED_ShowString(3, 1, "EDIT ");
    OLED_ShowString(3, 6, edit_name());
    OLED_ShowChar(3, 9, ' ');
    if (s_edit == EDIT_TGT)
    {
      draw_temp1(3, 10, p->target_c);
    }
    else if (s_edit == EDIT_KP)
    {
      draw_float2(3, 10, p->kp);
    }
    else if (s_edit == EDIT_KI)
    {
      draw_float2(3, 10, p->ki);
    }
    else
    {
      draw_float2(3, 10, p->kd);
    }
  }

  /* ---- L4：SIM/REAL + 秒数；有消息时优先显示消息 ---- */
  if (s_msg_ttl > 0u)
  {
    OLED_ShowString(4, 1, s_msg);
  }
  else if (s_edit != EDIT_OFF)
  {
    OLED_ShowString(4, 1, "K2:NXT K3:+-");
  }
  else
  {
    OLED_ShowString(4, 1, s->is_simulated ? "SIM  " : "REAL ");
    OLED_ShowString(4, 6, "UP:");
    OLED_ShowNum(4, 9, uptime_s, 5);
  }
}
