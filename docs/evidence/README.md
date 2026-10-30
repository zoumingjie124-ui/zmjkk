# 证据快照（EVIDENCE）

这里的日志是**某次实际执行**留下的快照，用于面试或评审时核对结论；日常构建/测试的日志不进版本库（见仓库根 `.gitignore`）。

| 文件 | 产生方式 | 对应结论 |
|------|----------|----------|
| `rebuild_2026-10-30.log` | `UV4.exe -r TempCtrl.uvprojx -t TempCtrl`（全量 Rebuild） | 0E0W，Code=17978B |
| `pid_test_2026-10-30.log` | `tests/run_tests.bat` | PID 28 项通过 |
| `param_test_2026-10-30.log` | 同上 | 参数编解码 30 项通过 |
| `control_test_2026-10-30.log` | 同上 | 控制状态机 55 项通过 |
| `protocol_test_2026-10-30.txt` | `python host/test_protocol.py` | 协议 46 项通过 |
| `monitor_demo_2026-10-30.txt` | `python host/monitor_cli.py --demo` | 无硬件链路演练 |

## 刷新方式

```bash
cd MDK-ARM && "D:/K5/UV4/UV4.exe" -r TempCtrl.uvprojx -t TempCtrl -o rebuild.log
cd tests && run_tests.bat
cd host   && python test_protocol.py
# 然后把新日志复制到本目录，文件名改成当天日期
```

原则：**日志只是证据，不是结论**。真机（上板）项在 `docs/TEST_REPORT.md` §4 仍标"待测试"，直到有人真的测过。
