# App 层（应用层）— 当前实际内容

> 本文件描述**已实现**的模块与依赖关系。最后更新：2026-10-24。

依赖方向：`App → Hardware → HAL`；`pid.c` / `param_codec.c` 例外——它们零依赖，可在宿主机直接编译测试。

| 文件 | 职责 | 依赖 | 状态 |
|------|------|------|------|
| `pid.c/h` | 位置式 PID：输出限幅 0~100、双重积分抗饱和、微分作用于测量值 + 一阶低通、NaN/超界输入拦截；固定 `Ts=0.5s` | 无（纯 C） | ✅ 已实现 · 28 项单测通过 |
| `param_codec.c/h` | 参数镜像序列化（28B）/ 反序列化 + CRC-32 + 数值范围校验 | 无（纯 C） | ✅ 已实现 · 30 项单测通过（含范围校验） |
| `control.c/h` | 四状态机 INIT/STANDBY/RUNNING/FAULT；安全联锁（传感器无效 / >75℃ / PID 输入异常 / 热失控）；风扇迟滞（RUNNING 与 STANDBY 共用，停机做余热管理）；运行时参数读写；START/ACK 新鲜读数校验 | pid + sensor/heater/fan | ✅ 已实现 · 70 项宿主机测试通过，上板待测 |
| `menu.c/h` | 按键菜单（启停/故障确认/编辑 TGT·KP·KI·KD/步进/保存）+ OLED 四行渲染（正常页/编辑页/消息） | key + oled + control + flash_param | ✅ 已实现 · 上板待测 |
| `comm.c/h` | AA55 帧解析状态机 + **50ms 帧内字节超时** + CRC16 校验 + 9 条命令分发（含 `GET_DIAG`）+ 200ms 状态上报 + NACK 错误码 | usart + control + flash_param | ✅ 已实现 · 解析器 18 项 + 协议 57 项测试通过，链路待上板 |
| `flash_param.c/h` | 末页 Flash（0x0800FC00）整页擦写 + 关中断临界区 + 回读校验；加载失败回默认并置标志 | param_codec + HAL FLASH | ✅ 已实现 · 参数编解码已测，擦写待上板 |

## 约束

- App 层只调 Hardware 层接口，不直接操作寄存器。
- `pid.c`、`param_codec.c` 禁止 include 任何 HAL 头文件（否则宿主机单测失效）。
- 安全联锁必须在 PID 之前执行；FAULT 只能由人工确认解除，且确认时复核传感器。

## 历史说明

本文件早期版本写的是"第一阶段为空，仅作结构占位"，那是阶段 1 的状态。
当前 6 组模块均已完成实现与宿主机测试，**上板验证仍未执行**（见 `docs/TEST_REPORT.md`）。
