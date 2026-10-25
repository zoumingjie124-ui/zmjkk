/**
  ******************************************************************************
  * @file    comm.c
  * @brief   通信协议实现：非阻塞状态机解析 + 命令分发 + 200ms 状态上报
  ******************************************************************************
  */
#include "comm.h"
#include "usart.h"
#include "control.h"
#include "flash_param.h"
#include <string.h>

#define FRAME_H1        0xAAu
#define FRAME_H2        0x55u
#define CMD_GET_STATUS  0x01u
#define CMD_SET_TARGET  0x02u
#define CMD_START       0x03u
#define CMD_STOP        0x04u
#define CMD_ACK_FAULT   0x05u
#define CMD_SET_PID     0x06u
#define CMD_SAVE_PARAMS 0x07u
#define CMD_GET_PARAMS  0x08u
#define CMD_GET_DIAG    0x09u   /* 诊断：读环形缓冲溢出/累计接收字节 */

#define RESP_FLAG       0x80u
#define CMD_NACK        0x7Fu

#define ACK_OK          0u
#define ACK_CRC_ERR     1u
#define ACK_LEN_ERR     2u
#define ACK_RANGE_ERR   3u
#define ACK_STATE_ERR   4u
#define ACK_FLASH_ERR   5u   /* Flash 擦除/写入/回读失败（第二轮审查新增） */
#define ACK_SENSOR_ERR  6u   /* 传感器无效，拒绝启动/无法确认故障（新增） */

/* Control 返回值 -> 通信错误码映射（0 成功/1 参数/2 状态/3 传感器无效） */
static uint8_t map_ctrl_status(uint8_t st)
{
  switch (st)
  {
    case 0u:  return ACK_OK;
    case 1u:  return ACK_RANGE_ERR;
    case 3u:  return ACK_SENSOR_ERR;
    default:  return ACK_STATE_ERR;
  }
}

#define MAX_PAYLOAD     32u
#define REPORT_PERIOD_MS 200u

/* 一次 tick 的接收消化上限。两个上限各管一件事：
   字节数 = 环形缓冲容量 128B，10ms 时基下等于 12.8KB/s，刚好盖住 115200 8N1 的
   11.5KB/s（原来的 64B 只有 6.4KB/s，全速灌包必然溢出丢字节）。
   时间 = 3ms，防 NACK 风暴：连续坏帧每个都会触发一次阻塞发送，
   若不定时上限，约 40 个 NACK 能把主循环拖住近 30ms，挤掉按键与 PID 的时间片。
   正常路径消化 128 字节只要几十微秒，这个时间上限碰不到。 */
#define RX_DRAIN_MAX_BYTES  USART_RX_RING_SIZE
#define RX_DRAIN_MAX_MS     3u

typedef enum
{
  PS_H1 = 0,
  PS_H2,
  PS_CMD,
  PS_LEN,
  PS_PAYLOAD,
  PS_CRC_L,
  PS_CRC_H
} parse_state_t;

static parse_state_t s_ps;
static uint8_t  s_cmd;
static uint8_t  s_len;
static uint8_t  s_idx;
static uint8_t  s_payload[MAX_PAYLOAD];
static uint16_t s_crc_rx;
static uint32_t s_last_report_ms;

/* ---------------- CRC16 / 字节序 ---------------- */

static uint16_t crc16_ccitt(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFu;
  uint16_t i;
  uint8_t  b;

  for (i = 0; i < len; i++)
  {
    crc ^= (uint16_t)((uint16_t)data[i] << 8);
    for (b = 0; b < 8u; b++)
    {
      if (crc & 0x8000u)
      {
        crc = (uint16_t)((crc << 1) ^ 0x1021u);
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  return crc;
}

static void put_f32(uint8_t *buf, float v)
{
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
  buf[0] = (uint8_t)(u & 0xFFu);
  buf[1] = (uint8_t)((u >> 8) & 0xFFu);
  buf[2] = (uint8_t)((u >> 16) & 0xFFu);
  buf[3] = (uint8_t)((u >> 24) & 0xFFu);
}

static void put_u32(uint8_t *buf, uint32_t u)
{
  buf[0] = (uint8_t)(u & 0xFFu);
  buf[1] = (uint8_t)((u >> 8) & 0xFFu);
  buf[2] = (uint8_t)((u >> 16) & 0xFFu);
  buf[3] = (uint8_t)((u >> 24) & 0xFFu);
}

static float get_f32(const uint8_t *buf)
{
  uint32_t u;
  float    v;
  u = ((uint32_t)buf[0])
    | ((uint32_t)buf[1] << 8)
    | ((uint32_t)buf[2] << 16)
    | ((uint32_t)buf[3] << 24);
  memcpy(&v, &u, sizeof(v));
  return v;
}

/* ---------------- 发送 ---------------- */

static void send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len)
{
  uint8_t  frame[8u + MAX_PAYLOAD];
  uint16_t crc;
  uint8_t  n = 0u;
  uint8_t  i;

  frame[n++] = FRAME_H1;
  frame[n++] = FRAME_H2;
  frame[n++] = cmd;
  frame[n++] = len;
  for (i = 0; i < len; i++)
  {
    frame[n++] = payload[i];
  }
  crc = crc16_ccitt(&frame[2], (uint16_t)(len + 2u));
  frame[n++] = (uint8_t)(crc & 0xFFu);
  frame[n++] = (uint8_t)(crc >> 8);
  Usart_Send(frame, n);
}

static void send_ack(uint8_t cmd, uint8_t status)
{
  send_frame((uint8_t)(cmd | RESP_FLAG), &status, 1u);
}

static void send_nack(uint8_t code)
{
  send_frame(CMD_NACK, &code, 1u);
}

static void send_status(void)
{
  const ctrl_snapshot_t *s = Control_Get();
  const ctrl_params_t   *p = Control_GetParams();
  uint8_t pl[25];

  put_f32(&pl[0],  s->temp_valid ? s->temp_c : -999.0f);
  put_f32(&pl[4],  p->target_c);
  pl[8]  = s->duty;
  pl[9]  = s->fan_on;
  pl[10] = (uint8_t)s->state;
  pl[11] = s->is_simulated;
  pl[12] = (uint8_t)s->fault;
  put_f32(&pl[13], p->kp);
  put_f32(&pl[17], p->ki);
  put_f32(&pl[21], p->kd);
  send_frame((uint8_t)(CMD_GET_STATUS | RESP_FLAG), pl, 25u);
}

static void send_params(void)
{
  const ctrl_params_t *p = Control_GetParams();
  uint8_t pl[16];

  put_f32(&pl[0],  p->target_c);
  put_f32(&pl[4],  p->kp);
  put_f32(&pl[8],  p->ki);
  put_f32(&pl[12], p->kd);
  send_frame((uint8_t)(CMD_GET_PARAMS | RESP_FLAG), pl, 16u);
}

/* 诊断帧：让"链路上到底丢了多少字节"变成可读的数字。
   在此之前 Usart_OverflowCount() 只有定义没有调用者，溢出在固件侧完全不可见。 */
static void send_diag(void)
{
  uint8_t pl[8];

  put_u32(&pl[0], Usart_OverflowCount());   /* 环形缓冲溢出丢弃的字节数 */
  put_u32(&pl[4], Usart_RxByteCount());     /* 累计成功入环的字节数 */
  send_frame((uint8_t)(CMD_GET_DIAG | RESP_FLAG), pl, 8u);
}

/* ---------------- 命令分发 ---------------- */

static void handle_frame(uint8_t cmd, const uint8_t *pl, uint8_t len)
{
  uint8_t status;

  switch (cmd)
  {
    case CMD_GET_STATUS:
      if (len != 0u) { send_ack(cmd, ACK_LEN_ERR); break; }
      send_status();
      break;

    case CMD_SET_TARGET:
      if (len != 4u) { send_ack(cmd, ACK_LEN_ERR); break; }
      status = Control_SetTarget(get_f32(pl));
      send_ack(cmd, (uint8_t)(status == 0u ? ACK_OK : ACK_RANGE_ERR));
      break;

    case CMD_START:
      if (len != 0u) { send_ack(cmd, ACK_LEN_ERR); break; }
      status = Control_Start();          /* 内部做传感器新鲜读数校验 */
      send_ack(cmd, map_ctrl_status(status));
      break;

    case CMD_STOP:
      if (len != 0u) { send_ack(cmd, ACK_LEN_ERR); break; }
      status = Control_Stop();
      send_ack(cmd, map_ctrl_status(status));
      break;

    case CMD_ACK_FAULT:
      if (len != 0u) { send_ack(cmd, ACK_LEN_ERR); break; }
      status = Control_AckFault();       /* 传感器仍无效时保持锁定 */
      send_ack(cmd, map_ctrl_status(status));
      break;

    case CMD_SET_PID:
      if (len != 12u) { send_ack(cmd, ACK_LEN_ERR); break; }
      status = Control_SetPid(get_f32(&pl[0]), get_f32(&pl[4]), get_f32(&pl[8]));
      send_ack(cmd, (uint8_t)(status == 0u ? ACK_OK : ACK_RANGE_ERR));
      break;

    case CMD_SAVE_PARAMS:
    {
      const ctrl_params_t *p = Control_GetParams();
      if (len != 0u) { send_ack(cmd, ACK_LEN_ERR); break; }
      status = Param_SaveValues(p->target_c, p->kp, p->ki, p->kd);
      send_ack(cmd, (uint8_t)(status == 0u ? ACK_OK : ACK_FLASH_ERR));
      break;
    }

    case CMD_GET_PARAMS:
      if (len != 0u) { send_ack(cmd, ACK_LEN_ERR); break; }
      send_params();
      break;

    case CMD_GET_DIAG:
      if (len != 0u) { send_ack(cmd, ACK_LEN_ERR); break; }
      send_diag();
      break;

    default:
      send_nack(ACK_LEN_ERR);      /* 未知命令统一按长度/协议错误回报 */
      break;
  }
}

/* ---------------- 非阻塞解析 ---------------- */

static void parser_reset(void)
{
  s_ps  = PS_H1;
  s_idx = 0u;
}

static void parser_feed(uint8_t b)
{
  switch (s_ps)
  {
    case PS_H1:
      if (b == FRAME_H1) { s_ps = PS_H2; }
      break;

    case PS_H2:
      if (b == FRAME_H2)      { s_ps = PS_CMD; }
      else if (b == FRAME_H1) { s_ps = PS_H2;  } /* 连续 0xAA：当前字节可能为真帧头 H1 */
      else                    { s_ps = PS_H1;  }
      break;

    case PS_CMD:
      s_cmd = b;
      s_ps  = PS_LEN;
      break;

    case PS_LEN:
      s_len = b;
      s_idx = 0u;
      if (s_len > MAX_PAYLOAD)
      {
        send_nack(ACK_LEN_ERR);
        parser_reset();
      }
      else
      {
        s_ps = (s_len == 0u) ? PS_CRC_L : PS_PAYLOAD;
      }
      break;

    case PS_PAYLOAD:
      s_payload[s_idx++] = b;
      if (s_idx >= s_len)
      {
        s_ps = PS_CRC_L;
      }
      break;

    case PS_CRC_L:
      s_crc_rx = b;
      s_ps     = PS_CRC_H;
      break;

    case PS_CRC_H:
    {
      uint8_t  crcbuf[2u + MAX_PAYLOAD];
      uint16_t crc;
      uint8_t  i;

      s_crc_rx |= (uint16_t)((uint16_t)b << 8);
      crcbuf[0] = s_cmd;
      crcbuf[1] = s_len;
      for (i = 0; i < s_len; i++)
      {
        crcbuf[2u + i] = s_payload[i];
      }
      crc = crc16_ccitt(crcbuf, (uint16_t)(s_len + 2u));

      if (crc != s_crc_rx)
      {
        send_nack(ACK_CRC_ERR);       /* 校验错：丢弃整帧 */
      }
      else
      {
        handle_frame(s_cmd, s_payload, s_len);
      }
      parser_reset();
      break;
    }

    default:
      parser_reset();
      break;
  }
}

void Comm_Init(void)
{
  parser_reset();
  s_last_report_ms = HAL_GetTick();
}

void Comm_Tick(void)
{
  uint8_t  b;
  uint16_t guard = 0u;
  uint32_t t0;
  uint32_t now   = HAL_GetTick();

  /* 每 tick 最多消化 128 字节（环容量）且不超过 3ms，见 RX_DRAIN_MAX_* 的说明。
     触到上限时剩余字节留在环里，下个 tick 继续，不丢数据。 */
  t0 = now;
  while ((guard++ < RX_DRAIN_MAX_BYTES) &&
         ((HAL_GetTick() - t0) < RX_DRAIN_MAX_MS) &&
         Usart_ReadByte(&b))
  {
    parser_feed(b);
  }

  /* 定时上报状态：上位机无需轮询即可绘图 */
  if ((now - s_last_report_ms) >= REPORT_PERIOD_MS)
  {
    s_last_report_ms = now;
    send_status();
  }
}
