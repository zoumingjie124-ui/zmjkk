# Gate 0 台架测试补丁（A/B 阶段故障注入）

> 用途：PA8 假负载验证 + 复位行为 + 传感器故障。测完把第 5 节记录表贴进 `docs/TEST_REPORT.md` §4。
> 器材：万用表 FY106A、LED ×1~2、330Ω、10kΩ、杜邦线、ST-Link。**不接 MOS，不接 PTC。**
> 状态：⏳ 待执行（本文是操作单，不是实测记录）

---

## 0. 前置条件

- 烧 `SENSOR_SIM_ENABLE=0` 的 REAL 固件（`Hardware/sensor.h`，默认值）。
  SIM 模式下 `Heater_EnableOutput()` 被强制为 0，**PA8 恒低**，且传感器恒 valid——A/B 全部无效。
- NTC 必须已接好再上电：REAL 模式下 NTC 开路会让上电直接进 `FAULT`，无法启动。
- 工程未冻结 IWDG（`Core/Src/iwdg.c` 无 `DBGMCU` 配置）：Keil 调试中点"暂停"＝程序卡死，约 1.3s 后自动复位。**这是预期行为，不是 bug**。

---

## 1. 接线

```
PA8 ── 330Ω ── LED 阳极(长脚)，LED 阴极(短脚) ── GND
PA8 ── 10kΩ ── GND            ← 下拉，没有它"复位时为低"测不准
可选：PB0 ── 330Ω ── LED2 ── GND    ← 同时观察"加热关 + 风扇开"
```

10k 下拉必须装：复位期间 PA8 是高阻态，没有下拉时万用表读数会漂。

---

## 2. 台架代码（阶梯占空比 10% → 50% → 90%）

放进 `Core/Src/main.c` 的 `while (1)` **末尾**（`Control_Tick()` 之后，否则会被状态机里的 `Heater_Off()` 覆盖）。
作用域内已有毫秒时间变量 `now`（`HAL_GetTick()`），无需额外声明。

```c
#if GATE0_STAIR
    /* Gate 0 台架专用：10% -> 50% -> 90% 阶梯，每 3s 换档。
       只接 LED + 330R + 10k 下拉，绝不接 PTC。
       用法：Keil -> Options -> C/C++ -> Define 加 GATE0_STAIR=1；测完删掉。 */
    {
      static const uint16_t stair[3] = {100u, 500u, 900u};
      static uint8_t  idx    = 0u;
      static uint32_t t_step = 0u;

      if ((now - t_step) >= 3000u)
      {
        t_step = now;
        idx = (uint8_t)((idx + 1u) % 3u);
      }
      Heater_EnableOutput(1);   /* 强制打开输出许可（绕过状态机，仅台架用） */
      Heater_SetDutyPermille(stair[idx]);
    }
#endif
```

**测完必须做三件事**：删掉 `GATE0_STAIR=1` 宏、确认正式固件不含这段、重新编译并核对 `Code` 尺寸回到 18626B。

> 这段代码**故意绕过状态机**：即使进了 `FAULT` 也照样输出 PWM。所以它只允许在"不接功率负载"的台架上用。

---

## 3. Step 1：PA8 波形

| 档位 | Hz 档 | DC 档（**先拔掉 LED**） |
|------|-------|-----------------------|
| 10% | 1.000 kHz（±15Hz） | 0.33 V ±0.15 |
| 50% | 1.000 kHz | 1.65 V ±0.15 |
| 90% | 1.000 kHz | 2.97 V ±0.15 |

LED 接着时**不要量电压**（高电平被钳到约 2V，读数失真），只看亮度三档变化。
公式：`V_avg = 3.3 × 占空比`。

---

## 4. Step 2 / Step 3

### Step 2：复位行为（用**不带** `GATE0_STAIR` 的固件）

| 测试 | 操作 | 判据 |
|------|------|------|
| 手动复位 | 按住 RST，DC 档量 PA8 | **0 V**；松开后仍为 0（STANDBY 不加热） |
| 看门狗 | Keil 调试运行中点"暂停" | ≤1.3s 自动复位，调试断开，OLED 出现 `WDG` |
| 启动期间 | 上电盯 LED | 复位到 OLED 亮起全程不亮；**闪一下 = 有输出，属缺陷** |

### Step 3：传感器故障

前提：REAL 固件、NTC 接好、按 KEY1 进 RUNNING（此时 PA8 上 LED 应亮——室温远低于 45℃ 目标，PID 输出大）。

| 场景 | 操作 | 预期 | 判据 |
|------|------|------|------|
| 开路 | 拔掉 NTC 与 PA0 间的线 | PA0 被上拉到 3.3V（raw>4090 贴轨），`is_valid=0`，进 FLT | OLED 1s 内 `FLT`+`SEN`；PA8=0V；PB0=3.3V |
| 短路 | 杜邦线短接 NTC 两脚 | PA0=0V（raw<5 贴轨），进 FLT | 同上（3.3V 经 10k 到地仅 0.33mA，安全） |
| 误确认 | 故障未排除时按 KEY1 | 拒绝解除 | OLED `SENSOR STILL ERR`，仍 FLT（串口 ACK 6） |
| 正确恢复 | 插回 NTC，按 KEY1 | 回 `STB`，**不自动恢复加热** | 需再按一次 KEY1 才进 RUNNING |

---

## 5. 记录表

| # | 项 | 实测 | 结论 |
|---|----|------|------|
| 1 | PA8 10% 电压 | ____ V | |
| 2 | PA8 50% 电压 | ____ V | |
| 3 | PA8 90% 电压 | ____ V | |
| 4 | 频率 | ____ kHz | |
| 5 | 复位时 PA8 | ____ V | |
| 6 | IWDG 复位时间 | ____ s | |
| 7 | 开路 → FLT | ____ s | |
| 8 | 短路 → FLT | ____ s | |
| 9 | 误确认被拒 | 是/否 | |

---

## 6. 万用表测不到的

关断延迟、复位瞬间毛刺：留给示波器/逻辑分析仪，**别用"万用表读 0V"当作已验证**。
对应项在 `docs/TEST_REPORT.md` §4 里仍标"待测试"，直到有波形证据。

---

## 7. 做完之后

1. 撤掉 `GATE0_STAIR`，重新编译（`Code` 应回到 18626B，0 Error 0 Warning）
2. 把第 5 节填好的数字并进 `docs/TEST_REPORT.md` §4，标"实测 + 日期 + 器材"
3. 下一步进 Gate 1（过温开关与传感器链路），再进 Gate 2（真实 PTC 与 PID 整定）
