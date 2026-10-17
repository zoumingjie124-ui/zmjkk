/**
  ******************************************************************************
  * @file    flash_param.h
  * @brief   参数掉电保存：STM32F103C8T6 末页 Flash（第 63 页）
  *
  * 容量核算：C8T6 = 64KB Flash，页大小 1KB，共 64 页（页 0~63）
  *   页地址 = 0x08000000 + page * 0x400
  *   第 63 页起始 = 0x08000000 + 63 * 0x400 = 0x0800FC00，占 1KB
  *   镜像仅 28 字节，剩余空间留作将来扩展（版本号已预留）
  *
  * 擦写保护：F1 单 bank，擦写期间取指会停顿，必须关中断；
  *           进入前保存 PRIMASK，结束后恢复原值（不用无脑 __enable_irq）。
  ******************************************************************************
  */
#ifndef __FLASH_PARAM_H
#define __FLASH_PARAM_H

#include "main.h"
#include "param_codec.h"

#define PARAM_FLASH_PAGE       63u
#define PARAM_FLASH_ADDR       0x0800FC00u

/* 返回值：1 = 从 Flash 成功加载；0 = 校验失败/首次上电，已回默认值 */
uint8_t Param_Load(app_param_t *p);

/* 返回值：0 = 保存并回读校验成功；1 = 擦除/写入失败；2 = 回读校验不符 */
uint8_t Param_Save(const app_param_t *p);

/* 便捷封装：把当前控制参数打包保存（供菜单/通信层调用） */
uint8_t Param_SaveValues(float target_c, float kp, float ki, float kd);

/* 1 = 本次上电曾因校验失败恢复默认（OLED/上位机可提示） */
uint8_t Param_WasDefaultRestored(void);

#endif /* __FLASH_PARAM_H */
