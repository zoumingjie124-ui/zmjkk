/**
  ******************************************************************************
  * @file    param_codec.h
  * @brief   参数镜像的序列化 / CRC32 校验（纯 C，无 HAL 依赖，可宿主机测试）
  *
  * Flash 镜像固定 28 字节，与小端字节序绑定（Cortex-M3 与 x86 都是小端）：
  *   偏移 0..3   magic   0x54434D31 ("TCM1")
  *   偏移 4..5   version 1
  *   偏移 6..7   reserved
  *   偏移 8..11  target_c (float)
  *   偏移 12..15 kp (float)
  *   偏移 16..19 ki (float)
  *   偏移 20..23 kd (float)
  *   偏移 24..27 crc32（对 0..23 字节计算）
  *
  * 序列化不直接强转结构体指针，避免编译器对齐/填充造成的 ABI 差异。
  ******************************************************************************
  */
#ifndef __PARAM_CODEC_H
#define __PARAM_CODEC_H

#include <stdint.h>

#define APP_PARAM_MAGIC        0x54434D31u
#define APP_PARAM_VERSION      1u
#define APP_PARAM_IMAGE_SIZE   28u

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  float    target_c;
  float    kp;
  float    ki;
  float    kd;
  uint32_t crc32;
} app_param_t;

/* CRC-32/MPEG-2：poly 0x04C11DB7，init 0xFFFFFFFF，不反射，无 xorout */
uint32_t Param_Crc32(const uint8_t *data, uint32_t len);

void     Param_SetDefaults(app_param_t *p);

/* 返回写入字节数（恒为 APP_PARAM_IMAGE_SIZE） */
uint32_t Param_Serialize(const app_param_t *p, uint8_t *buf);

/* 0 = 校验通过；1 = magic/version 不符；2 = CRC 不符 */
uint8_t  Param_Deserialize(const uint8_t *buf, app_param_t *p);

#endif /* __PARAM_CODEC_H */
