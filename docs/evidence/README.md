# 证据快照（EVIDENCE）

这里的日志是**某次实际执行**留下的快照，用于面试或评审时核对结论；日常构建/测试的日志不进版本库（见仓库根 `.gitignore`）。

按日期分目录：`docs/evidence/<YYYY-MM-DD>/`。同一天重复运行会覆盖同名文件（保持 git 干净）。

| 文件（以 2026-10-30 为例） | 产生方式 | 对应结论 |
|------|----------|----------|
| `2026-10-30/rebuild.log` | `UV4.exe -r TempCtrl.uvprojx -t TempCtrl`（全量 Rebuild） | 0E0W，Code=17978B |
| `2026-10-30/pid_test.log` | `tests/run_tests.bat`（自动快照） | PID 28 项通过 |
| `2026-10-30/param_test.log` | 同上 | 参数编解码 30 项通过 |
| `2026-10-30/control_test.log` | 同上 | 控制状态机 55 项通过 |
| `2026-10-30/protocol_test.txt` | `python host/test_protocol.py`（手动 `--all` 归档） | 协议 46 项通过 |
| `2026-10-30/monitor_demo.txt` | `python host/monitor_cli.py --demo`（同上） | 无硬件链路演练 |

## 刷新方式

`run_tests.bat` / `run_tests.sh` 会**自动**调用 `tests/snapshot_evidence.py`，把三份测试日志归档到当天目录；
编译日志与协议/链路日志用 `--all` 手动补一次：

```bash
cd MDK-ARM && "D:/K5/UV4/UV4.exe" -r TempCtrl.uvprojx -t TempCtrl -o rebuild.log
cd tests  && run_tests.bat                 # 自动归档 pid/param/control
cd tests  && python snapshot_evidence.py --all   # 再补 rebuild / protocol / demo
```

原则：**日志只是证据，不是结论**。真机（上板）项在 `docs/TEST_REPORT.md` §4 仍标"待测试"，直到有人真的测过。
