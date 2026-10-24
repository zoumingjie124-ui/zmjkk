/**
  ******************************************************************************
  * @file    flash_param.c
  * @brief   参数 Flash 存储（末页 63，0x0800FC00）
  *
  * 写入策略：整页擦除 -> 按字（32bit）写入 7 个字（28 字节）-> 回读校验
  * 中断保护：擦写全程关中断（F1 单 bank，取指停顿会打破中断时序假设）
  ******************************************************************************
  */
#include "flash_param.h"
#include <string.h>

static uint8_t s_default_restored = 0u;

/* 从字节缓冲取第 i 个字（小端） */
static uint32_t image_word(const uint8_t *buf, uint32_t i)
{
  const uint8_t *p = &buf[i * 4u];
  return ((uint32_t)p[0])
       | ((uint32_t)p[1] << 8)
       | ((uint32_t)p[2] << 16)
       | ((uint32_t)p[3] << 24);
}

uint8_t Param_Load(app_param_t *p)
{
  const uint8_t *flash = (const uint8_t *)PARAM_FLASH_ADDR;
  uint8_t        buf[APP_PARAM_IMAGE_SIZE];
  uint8_t        ret;

  memcpy(buf, flash, APP_PARAM_IMAGE_SIZE);
  ret = Param_Deserialize(buf, p);
  if (ret != 0u)
  {
    /* 首次上电（全 0xFF）或数据被破坏：回默认值并置标志 */
    Param_SetDefaults(p);
    s_default_restored = 1u;
    return 0u;
  }

  /* 第二轮审查：CRC 正确不代表数值合理（脏数据/误写同样能算对 CRC），
     加载层必须自己把关，不能只依赖上层限幅 */
  if (Param_ValidateRanges(p) != 0u)
  {
    Param_SetDefaults(p);
    s_default_restored = 1u;
    return 0u;
  }

  s_default_restored = 0u;
  return 1u;
}

uint8_t Param_Save(const app_param_t *p)
{
  uint8_t  buf[APP_PARAM_IMAGE_SIZE];
  uint8_t  verify[APP_PARAM_IMAGE_SIZE];
  uint32_t primask;
  uint32_t page_err = 0u;
  uint32_t i;
  FLASH_EraseInitTypeDef erase;
  app_param_t dummy;

  Param_Serialize(p, buf);

  /* ---- 关中断进入擦写临界区 ---- */
  primask = __get_PRIMASK();
  __disable_irq();

  HAL_FLASH_Unlock();

  erase.TypeErase   = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = PARAM_FLASH_ADDR;
  erase.NbPages     = 1u;
  if (HAL_FLASHEx_Erase(&erase, &page_err) != HAL_OK)
  {
    HAL_FLASH_Lock();
    __set_PRIMASK(primask);
    return 1u;
  }

  for (i = 0; i < (APP_PARAM_IMAGE_SIZE / 4u); i++)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                          PARAM_FLASH_ADDR + i * 4u,
                          image_word(buf, i)) != HAL_OK)
    {
      HAL_FLASH_Lock();
      __set_PRIMASK(primask);
      return 1u;
    }
  }

  HAL_FLASH_Lock();
  __set_PRIMASK(primask);
  /* ---- 退出临界区 ---- */

  /* 回读校验：确保掉电前写进去的就是这份数据 */
  memcpy(verify, (const void *)PARAM_FLASH_ADDR, APP_PARAM_IMAGE_SIZE);
  if (memcmp(verify, buf, APP_PARAM_IMAGE_SIZE) != 0)
  {
    return 2u;
  }
  (void)Param_Deserialize(verify, &dummy);   /* 顺带验证 CRC 自洽 */
  return 0u;
}

uint8_t Param_SaveValues(float target_c, float kp, float ki, float kd)
{
  app_param_t p;

  p.magic    = APP_PARAM_MAGIC;
  p.version  = APP_PARAM_VERSION;
  p.reserved = 0u;
  p.target_c = target_c;
  p.kp       = kp;
  p.ki       = ki;
  p.kd       = kd;
  p.crc32    = 0u;

  return Param_Save(&p);
}

uint8_t Param_WasDefaultRestored(void)
{
  return s_default_restored;
}
