/**
  ******************************************************************************
  * @file    param_codec.c
  * @brief   参数镜像序列化与 CRC32（纯 C，不依赖 HAL）
  ******************************************************************************
  */
#include "param_codec.h"
#include <string.h>

/* 默认参数：与 control.h 的默认值保持一致（首次上电/校验失败时使用） */
#define PARAM_DEF_TARGET   45.0f
#define PARAM_DEF_KP       8.0f
#define PARAM_DEF_KI       0.10f
#define PARAM_DEF_KD       1.5f

/* 小端写/读 float，避免结构体对齐问题 */
static void put_f32(uint8_t *buf, float v)
{
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
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

uint32_t Param_Crc32(const uint8_t *data, uint32_t len)
{
  uint32_t crc = 0xFFFFFFFFu;
  uint32_t i;
  uint8_t  b;

  for (i = 0; i < len; i++)
  {
    crc ^= ((uint32_t)data[i]) << 24;
    for (b = 0; b < 8u; b++)
    {
      if (crc & 0x80000000u)
      {
        crc = (crc << 1) ^ 0x04C11DB7u;
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  return crc;
}

void Param_SetDefaults(app_param_t *p)
{
  p->magic    = APP_PARAM_MAGIC;
  p->version  = APP_PARAM_VERSION;
  p->reserved = 0u;
  p->target_c = PARAM_DEF_TARGET;
  p->kp       = PARAM_DEF_KP;
  p->ki       = PARAM_DEF_KI;
  p->kd       = PARAM_DEF_KD;
  p->crc32    = 0u;              /* 序列化时重算 */
}

uint32_t Param_Serialize(const app_param_t *p, uint8_t *buf)
{
  uint32_t crc;

  buf[0] = (uint8_t)(p->magic & 0xFFu);
  buf[1] = (uint8_t)((p->magic >> 8) & 0xFFu);
  buf[2] = (uint8_t)((p->magic >> 16) & 0xFFu);
  buf[3] = (uint8_t)((p->magic >> 24) & 0xFFu);
  buf[4] = (uint8_t)(p->version & 0xFFu);
  buf[5] = (uint8_t)((p->version >> 8) & 0xFFu);
  buf[6] = (uint8_t)(p->reserved & 0xFFu);
  buf[7] = (uint8_t)((p->reserved >> 8) & 0xFFu);
  put_f32(&buf[8],  p->target_c);
  put_f32(&buf[12], p->kp);
  put_f32(&buf[16], p->ki);
  put_f32(&buf[20], p->kd);

  crc = Param_Crc32(buf, 24u);
  buf[24] = (uint8_t)(crc & 0xFFu);
  buf[25] = (uint8_t)((crc >> 8) & 0xFFu);
  buf[26] = (uint8_t)((crc >> 16) & 0xFFu);
  buf[27] = (uint8_t)((crc >> 24) & 0xFFu);
  return APP_PARAM_IMAGE_SIZE;
}

uint8_t Param_Deserialize(const uint8_t *buf, app_param_t *p)
{
  uint32_t magic, crc_stored, crc_calc;

  magic = ((uint32_t)buf[0])
        | ((uint32_t)buf[1] << 8)
        | ((uint32_t)buf[2] << 16)
        | ((uint32_t)buf[3] << 24);
  if (magic != APP_PARAM_MAGIC)
  {
    return 1u;
  }
  p->magic    = magic;
  p->version  = (uint16_t)(((uint16_t)buf[4]) | ((uint16_t)buf[5] << 8));
  if (p->version != APP_PARAM_VERSION)
  {
    return 1u;
  }
  p->reserved = (uint16_t)(((uint16_t)buf[6]) | ((uint16_t)buf[7] << 8));
  p->target_c = get_f32(&buf[8]);
  p->kp       = get_f32(&buf[12]);
  p->ki       = get_f32(&buf[16]);
  p->kd       = get_f32(&buf[20]);

  crc_stored = ((uint32_t)buf[24])
             | ((uint32_t)buf[25] << 8)
             | ((uint32_t)buf[26] << 16)
             | ((uint32_t)buf[27] << 24);
  crc_calc   = Param_Crc32(buf, 24u);
  if (crc_stored != crc_calc)
  {
    return 2u;
  }
  p->crc32 = crc_stored;
  return 0u;
}
