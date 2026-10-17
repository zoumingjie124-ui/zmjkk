/**
  ******************************************************************************
  * @file    test_param.c
  * @brief   参数序列化 / CRC32 离线单元测试（宿主机 MSVC 运行）
  *
  * 被测对象：App/Src/param_codec.c（与固件同一份源码，纯 C 无 HAL 依赖）
  * 覆盖：CRC 已知向量、序列化字节布局、往返一致性、损坏检测、
  *       空白 Flash（全 0xFF）检测、边界值
  ******************************************************************************
  */
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "../App/Src/param_codec.c"

static int g_failed = 0;

#define CHECK(cond, msg, ...)                                       \
  do {                                                              \
    if (cond) {                                                     \
      printf("  [PASS] " msg "\n", __VA_ARGS__);                    \
    } else {                                                        \
      printf("  [FAIL] " msg "\n", __VA_ARGS__);                    \
      g_failed++;                                                   \
    }                                                               \
  } while (0)

/* ==================== T1 CRC32 已知向量 ==================== */
static void t1_crc_vector(void)
{
  const uint8_t v[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  uint32_t crc = Param_Crc32(v, sizeof(v));

  printf("T1 CRC32 known vector:\n");
  /* CRC-32/MPEG-2（poly 0x04C11DB7, init 0xFFFFFFFF, 不反射）标准校验值 */
  CHECK(crc == 0x0376E6E7u, "crc32(\"123456789\")=0x%08X == 0x0376E6E7", crc);

  crc = Param_Crc32(v, 0u);
  CHECK(crc == 0xFFFFFFFFu, "crc32(empty)=0x%08X == 0xFFFFFFFF", crc);
}

/* ==================== T2 字节布局 ==================== */
static void t2_layout(void)
{
  app_param_t p;
  uint8_t     buf[APP_PARAM_IMAGE_SIZE];
  uint32_t    n;

  printf("T2 image layout:\n");
  Param_SetDefaults(&p);
  p.target_c = 45.0f;
  p.kp = 8.0f;
  p.ki = 0.10f;
  p.kd = 1.5f;
  n = Param_Serialize(&p, buf);

  CHECK(n == 28u, "image size=%u == 28", (unsigned)n);
  CHECK(buf[0] == 0x31 && buf[1] == 0x4D && buf[2] == 0x43 && buf[3] == 0x54,
        "magic bytes=%02X %02X %02X %02X (TCM1 LE)", buf[0], buf[1], buf[2], buf[3]);
  CHECK(buf[4] == 1u && buf[5] == 0u, "version=%u", buf[4]);
  CHECK(buf[8] == 0x00 && buf[9] == 0x00 && buf[10] == 0x34 && buf[11] == 0x42,
        "target 45.0f bytes=%02X %02X %02X %02X", buf[8], buf[9], buf[10], buf[11]);

  {
    uint32_t crc_calc = Param_Crc32(buf, 24u);
    uint32_t crc_stored = ((uint32_t)buf[24]) | ((uint32_t)buf[25] << 8)
                        | ((uint32_t)buf[26] << 16) | ((uint32_t)buf[27] << 24);
    CHECK(crc_calc == crc_stored, "stored crc=0x%08X matches", crc_stored);
  }
}

/* ==================== T3 往返一致性 ==================== */
static void t3_roundtrip(void)
{
  app_param_t a, b;
  uint8_t     buf[APP_PARAM_IMAGE_SIZE];
  uint8_t     ret;

  printf("T3 round trip:\n");
  Param_SetDefaults(&a);
  a.target_c = 47.5f;
  a.kp = 12.25f;
  a.ki = 0.35f;
  a.kd = 3.75f;

  Param_Serialize(&a, buf);
  ret = Param_Deserialize(buf, &b);

  CHECK(ret == 0u, "deserialize ret=%u == 0", ret);
  CHECK(b.magic == APP_PARAM_MAGIC, "magic=0x%08X", (unsigned)b.magic);
  CHECK(b.version == APP_PARAM_VERSION, "version=%u", (unsigned)b.version);
  CHECK(b.target_c == a.target_c, "target=%.2f", (double)b.target_c);
  CHECK(b.kp == a.kp, "kp=%.2f", (double)b.kp);
  CHECK(b.ki == a.ki, "ki=%.2f", (double)b.ki);
  CHECK(b.kd == a.kd, "kd=%.2f", (double)b.kd);
}

/* ==================== T4 损坏检测 ==================== */
static void t4_corruption(void)
{
  app_param_t p, out;
  uint8_t     buf[APP_PARAM_IMAGE_SIZE], bad[APP_PARAM_IMAGE_SIZE];
  uint8_t     ret;

  printf("T4 corruption detection:\n");
  Param_SetDefaults(&p);
  Param_Serialize(&p, buf);

  /* 4a. 载荷翻转 1 bit -> CRC 不符 */
  memcpy(bad, buf, sizeof(bad));
  bad[9] ^= 0x01u;
  ret = Param_Deserialize(bad, &out);
  CHECK(ret == 2u, "payload bitflip ret=%u == 2 (crc)", ret);

  /* 4b. magic 破坏 -> magic 不符 */
  memcpy(bad, buf, sizeof(bad));
  bad[0] = 0x00u;
  ret = Param_Deserialize(bad, &out);
  CHECK(ret == 1u, "bad magic ret=%u == 1", ret);

  /* 4c. 版本号提高 -> 版本不符 */
  memcpy(bad, buf, sizeof(bad));
  bad[4] = 0x02u;
  ret = Param_Deserialize(bad, &out);
  CHECK(ret == 1u, "bad version ret=%u == 1", ret);

  /* 4d. 空白 Flash（全 0xFF）-> magic 不符 */
  memset(bad, 0xFF, sizeof(bad));
  ret = Param_Deserialize(bad, &out);
  CHECK(ret == 1u, "blank flash ret=%u == 1", ret);
}

/* ==================== T5 边界值 ==================== */
static void t5_boundary(void)
{
  app_param_t a, b;
  uint8_t     buf[APP_PARAM_IMAGE_SIZE];

  printf("T5 boundary values:\n");
  Param_SetDefaults(&a);
  a.target_c = 0.0f;   a.kp = 0.0f;   a.ki = 0.0f;   a.kd = 0.0f;
  Param_Serialize(&a, buf);
  CHECK(Param_Deserialize(buf, &b) == 0u, "min bounds round trip ok");

  a.target_c = 70.0f;  a.kp = 50.0f;  a.ki = 5.0f;   a.kd = 20.0f;
  Param_Serialize(&a, buf);
  CHECK(Param_Deserialize(buf, &b) == 0u, "max bounds round trip ok");
  CHECK(b.target_c == 70.0f && b.kp == 50.0f && b.ki == 5.0f && b.kd == 20.0f,
        "max values preserved: %.1f %.1f %.2f %.1f",
        (double)b.target_c, (double)b.kp, (double)b.ki, (double)b.kd);
}

int main(void)
{
  printf("=== TempCtrl param codec tests (host build) ===\n");
  t1_crc_vector();
  t2_layout();
  t3_roundtrip();
  t4_corruption();
  t5_boundary();
  printf("=== %s: %d check(s) failed ===\n",
         g_failed == 0 ? "ALL PASS" : "FAILED", g_failed);
  return g_failed == 0 ? 0 : 1;
}
