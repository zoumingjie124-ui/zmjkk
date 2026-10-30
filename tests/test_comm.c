/**
  ******************************************************************************
  * @file    test_comm.c
  * @brief   通信解析器宿主机测试（直接编译真实的 App/Src/comm.c）
  *
  * 覆盖目标：
  *   T1 正常帧：GET_STATUS 请求 -> 0x81 + 25 字节载荷
  *   T2 分片不大于阈值：帧内字节间隔 40ms 仍能正常组帧（超时不得误伤）
  *   T3 【A 项回归】半帧卡住 + 超时 + 随后一条完整 STOP：STOP 必须被执行，
  *      不能被当成残余载荷吞掉（修复前这里只会回一个 NACK(1)，STOP 丢失）
  *   T4 超时阈值边界：49ms 不复位、51ms 复位
  *   T5 GET_DIAG：0x09 -> 0x89 + 8 字节（溢出计数 / 累计接收字节）
  *   T6 超时后接带载荷的完整帧：载荷被正确解析（不只是零载荷命令）
  *
  * 方法：与 test_control.c 一致——include 被测 .c，测的就是固件里那份代码。
  *   HAL 与 USART 用桩替代，时间由 g_tick 精确控制。
  ******************************************************************************
  */
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "usart.h"
#include "control.h"
#include "flash_param.h"

/* ------------------------------- 断框架 ------------------------------- */
static int g_failed = 0;

#define CHECK(cond, fmt, ...)                                                  \
  do                                                                           \
  {                                                                            \
    if (cond)                                                                  \
    {                                                                          \
      printf("  [PASS] " fmt "\n", __VA_ARGS__);                                \
    }                                                                          \
    else                                                                       \
    {                                                                          \
      printf("  [FAIL] " fmt "\n", __VA_ARGS__);                                \
      g_failed++;                                                              \
    }                                                                          \
  } while (0)

#define CHECK0(cond, msg)                                                      \
  do                                                                           \
  {                                                                            \
    if (cond) { printf("  [PASS] %s\n", msg); }                                 \
    else      { printf("  [FAIL] %s\n", msg); g_failed++; }                     \
  } while (0)

/* ------------------------------- 桩：时间 ------------------------------- */
static uint32_t g_tick = 1000u;

uint32_t HAL_GetTick(void)
{
  return g_tick;
}

/* ------------------------------- 桩：串口 ------------------------------- */
#define RX_CAP 512
#define TX_CAP 512

static uint8_t g_rx[RX_CAP];
static int     g_rx_len;
static int     g_rx_pos;
static uint8_t g_tx[TX_CAP];
static int     g_tx_len;
static uint32_t g_ovf;
static uint32_t g_rx_bytes;

uint8_t Usart_ReadByte(uint8_t *b)
{
  if (g_rx_pos >= g_rx_len)
  {
    return 0u;
  }
  *b = g_rx[g_rx_pos++];
  g_rx_bytes++;
  return 1u;
}

void Usart_Send(const uint8_t *data, uint16_t len)
{
  int i;
  for (i = 0; (i < (int)len) && (g_tx_len < TX_CAP); i++)
  {
    g_tx[g_tx_len++] = data[i];
  }
}

uint32_t Usart_OverflowCount(void) { return g_ovf; }
uint32_t Usart_RxByteCount(void)   { return g_rx_bytes; }

/* ------------------------------- 桩：控制层 ------------------------------- */
static ctrl_snapshot_t g_snap;
static ctrl_params_t   g_params;
static int g_start_calls, g_stop_calls, g_target_calls, g_pid_calls, g_ack_calls, g_save_calls;
static float g_last_target;

void Control_Init(void) {}
void Control_Tick(void) {}
void Control_ApplyParams(const ctrl_params_t *p) { g_params = *p; }
uint8_t Control_Start(void) { g_start_calls++; return 0u; }
uint8_t Control_Stop(void)  { g_stop_calls++;  return 0u; }
uint8_t Control_AckFault(void) { g_ack_calls++; return 0u; }
uint8_t Control_CanSaveParams(void) { return 1u; }

uint8_t Control_SetTarget(float target_c)
{
  g_target_calls++;
  g_last_target = target_c;
  return 0u;
}

uint8_t Control_SetPid(float kp, float ki, float kd)
{
  g_pid_calls++;
  g_params.kp = kp;
  g_params.ki = ki;
  g_params.kd = kd;
  return 0u;
}

const ctrl_snapshot_t *Control_Get(void)       { return &g_snap; }
const ctrl_params_t   *Control_GetParams(void) { return &g_params; }
const char            *Control_StateName(void) { return "STB"; }

uint8_t Param_SaveValues(float target_c, float kp, float ki, float kd)
{
  g_save_calls++;
  (void)target_c; (void)kp; (void)ki; (void)kd;
  return 0u;
}

/* --------------------- 被测代码（真实 comm.c，同 TU） --------------------- */
#include "../App/Src/comm.c"

/* ------------------------------- 辅助 ------------------------------- */
static void t_reset(void)
{
  Comm_Init();
  g_rx_len = 0;
  g_rx_pos = 0;
  g_tx_len = 0;
  g_ovf = 0u;
  g_rx_bytes = 0u;
  g_start_calls = g_stop_calls = g_target_calls = g_pid_calls = g_ack_calls = g_save_calls = 0;
  g_last_target = 0.0f;
  g_snap.state = CTRL_STANDBY;
  g_snap.fault = CTRL_FAULT_NONE;
  g_snap.temp_c = 30.0f;
  g_snap.temp_valid = 1u;
  g_snap.duty = 0u;
  g_snap.fan_on = 0u;
  g_snap.is_simulated = 0u;
  g_params.target_c = 45.0f;
  g_params.kp = 8.0f;
  g_params.ki = 0.1f;
  g_params.kd = 1.5f;
}

static void inject(const uint8_t *d, int n)
{
  int i;
  for (i = 0; i < n && g_rx_len < RX_CAP; i++)
  {
    g_rx[g_rx_len++] = d[i];
  }
}

/* 构造一帧：AA 55 | cmd | len | payload | crc16 */
static int mkframe(uint8_t *out, uint8_t cmd, const uint8_t *pl, uint8_t len)
{
  uint8_t  buf[2u + 32u];
  uint16_t crc;
  int      n = 0;
  int      i;

  out[n++] = 0xAAu;
  out[n++] = 0x55u;
  out[n++] = cmd;
  out[n++] = len;
  buf[0] = cmd;
  buf[1] = len;
  for (i = 0; i < (int)len; i++)
  {
    out[n++] = pl[i];
    buf[2 + i] = pl[i];
  }
  crc = crc16_ccitt(buf, (uint16_t)(len + 2u));
  out[n++] = (uint8_t)(crc & 0xFFu);
  out[n++] = (uint8_t)(crc >> 8);
  return n;
}

/* 在发送缓冲里找第一个响应帧的命令字（找不到返回 -1） */
static int find_resp(uint8_t cmd)
{
  int i;
  for (i = 0; i + 3 < g_tx_len; i++)
  {
    if (g_tx[i] == 0xAAu && g_tx[i + 1] == 0x55u && g_tx[i + 2] == cmd)
    {
      return i;
    }
  }
  return -1;
}

/* ============================ T1 正常帧 ============================ */
static void t1_status_frame(void)
{
  uint8_t f[16];
  int n;

  printf("T1 normal GET_STATUS frame:\n");
  t_reset();
  n = mkframe(f, 0x01u, NULL, 0u);
  inject(f, n);
  Comm_Tick();
  CHECK(find_resp(0x81u) >= 0, "got 0x81 response (at %d)", find_resp(0x81u));
  CHECK(g_tx[3] == 25u, "status payload len=%u", (unsigned)g_tx[3]);
}

/* ============================ T2 分片不误伤 ============================ */
static void t2_fragmented_within_gap(void)
{
  uint8_t f[16];
  uint8_t pl[4];
  int n;

  printf("T2 fragment gap 40ms (< timeout) must still assemble:\n");
  t_reset();
  put_f32(pl, 45.0f);
  n = mkframe(f, 0x02u, pl, 4u);

  inject(f, 3);                 /* 前 3 字节 */
  Comm_Tick();
  g_tick += 40u;                /* 间隔 40ms，未到 50ms */
  inject(f + 3, n - 3);
  Comm_Tick();

  CHECK(g_target_calls == 1, "SET_TARGET executed once (%d)", g_target_calls);
  CHECK(g_last_target > 44.9f && g_last_target < 45.1f, "target=%.2f", (double)g_last_target);
  CHECK0(find_resp(0x82u) >= 0, "got ACK 0x82");
}

/* ==================== T3 A 项回归：半帧超时不得吞掉 STOP ==================== */
static void t3_truncated_then_stop(void)
{
  uint8_t half[6];
  uint8_t stop[8];
  int n;

  printf("T3 [A-fix] half frame + timeout + then STOP must execute STOP:\n");
  t_reset();

  /* 半帧：SET_TARGET 声明 4 字节载荷，只给 2 字节 */
  half[0] = 0xAAu; half[1] = 0x55u; half[2] = 0x02u; half[3] = 0x04u;
  half[4] = 0x00u; half[5] = 0x00u;
  inject(half, 6);
  Comm_Tick();
  CHECK(s_ps == PS_PAYLOAD, "stuck in PS_PAYLOAD (state=%d)", (int)s_ps);

  g_tick += 60u;                /* 超过 50ms */
  Comm_Tick();                  /* 空转一拍，触发超时复位 */
  CHECK(s_ps == PS_H1, "parser reset after gap timeout (state=%d)", (int)s_ps);

  /* 紧随其后的完整 STOP 帧必须被执行 */
  g_tx_len = 0;
  n = mkframe(stop, 0x04u, NULL, 0u);
  inject(stop, n);
  Comm_Tick();

  CHECK(g_stop_calls == 1, "STOP executed (%d)", g_stop_calls);
  CHECK0(find_resp(0x84u) >= 0, "got ACK 0x84");
  CHECK0(find_resp(0x7Fu) < 0, "no NACK (frame was not swallowed)");
}

/* ============================ T4 阈值边界 ============================ */
static void t4_gap_boundary(void)
{
  uint8_t half[6];

  printf("T4 gap boundary: 49ms keeps state, 51ms resets:\n");
  t_reset();
  half[0] = 0xAAu; half[1] = 0x55u; half[2] = 0x02u; half[3] = 0x04u;
  half[4] = 0x00u; half[5] = 0x00u;
  inject(half, 6);
  Comm_Tick();

  g_tick += 49u;
  Comm_Tick();
  CHECK(s_ps == PS_PAYLOAD, "49ms: still waiting (state=%d)", (int)s_ps);

  g_tick += 2u;                 /* 累计 51ms */
  Comm_Tick();
  CHECK(s_ps == PS_H1, "51ms: reset (state=%d)", (int)s_ps);
}

/* ============================ T5 GET_DIAG ============================ */
static void t5_diag(void)
{
  uint8_t f[8];
  int n, at;

  printf("T5 GET_DIAG returns overflow/rx counters:\n");
  t_reset();
  g_ovf = 7u;
  g_rx_bytes = 1234u;
  n = mkframe(f, 0x09u, NULL, 0u);
  inject(f, n);
  Comm_Tick();

  at = find_resp(0x89u);
  CHECK(at >= 0, "got 0x89 diag response (at %d)", at);
  if (at >= 0)
  {
    uint32_t ovf, rx;
    CHECK(g_tx[at + 3] == 8u, "diag payload len=%u", (unsigned)g_tx[at + 3]);
    memcpy(&ovf, &g_tx[at + 4], 4u);
    memcpy(&rx,  &g_tx[at + 8], 4u);
    CHECK(ovf == 7u, "overflow=%u", (unsigned)ovf);
    CHECK(rx >= 1234u, "rx_bytes=%u", (unsigned)rx);
  }
}

/* ==================== T6 超时后接带载荷帧 ==================== */
static void t6_payload_after_timeout(void)
{
  uint8_t half[6];
  uint8_t f[16];
  uint8_t pl[4];
  int n;

  printf("T6 payload frame after timeout parses payload correctly:\n");
  t_reset();
  half[0] = 0xAAu; half[1] = 0x55u; half[2] = 0x02u; half[3] = 0x04u;
  half[4] = 0x11u; half[5] = 0x22u;
  inject(half, 6);
  Comm_Tick();
  g_tick += 80u;
  Comm_Tick();

  put_f32(pl, 50.0f);
  n = mkframe(f, 0x02u, pl, 4u);
  inject(f, n);
  Comm_Tick();

  CHECK(g_target_calls == 1, "SET_TARGET executed once (%d)", g_target_calls);
  CHECK(g_last_target > 49.9f && g_last_target < 50.1f,
        "target=%.2f (not corrupted by the leftover bytes)", (double)g_last_target);
}

int main(void)
{
  printf("=== TempCtrl comm parser tests (host build, real comm.c) ===\n");
  t1_status_frame();
  t2_fragmented_within_gap();
  t3_truncated_then_stop();
  t4_gap_boundary();
  t5_diag();
  t6_payload_after_timeout();
  printf("=== %s: %d check(s) failed ===\n",
         g_failed == 0 ? "ALL PASS" : "FAILED", g_failed);
  return g_failed == 0 ? 0 : 1;
}
