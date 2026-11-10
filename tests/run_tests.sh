#!/usr/bin/env bash
# 宿主机测试（gcc/clang，Linux/macOS/WSL/MSYS2 均可）：PID + 参数编解码 + 控制状态机 + 通信解析器 + 协议
# 用法：cd tests && ./run_tests.sh
set -euo pipefail
cd "$(dirname "$0")"
CC="${CC:-gcc}"
OUT="${TMPDIR:-/tmp}/tempctrl_tests"
mkdir -p "$OUT"
FLAGS="-std=c99 -Wall -Wextra"
fail=0

run() {  # run <name> <log> <extra include flags...>
  local name="$1" log="$2"; shift 2
  $CC $FLAGS "$@" "$name.c" -o "$OUT/$name" -lm
  "$OUT/$name" 2>&1 | tee "$log"
  [ "${PIPESTATUS[0]}" = "0" ] || fail=1
}

# 日志统一写到当前 tests/ 目录（已 gitignore），供 snapshot_evidence.py 完整归档 4 份 C 测试日志
run test_pid     pid_test.log     -I ../App/Inc
run test_param   param_test.log   -I ../App/Inc
run test_control control_test.log -I stubs -I ../App/Inc -I ../Hardware
# comm.c 经 stubs/usart.h 遮蔽真 usart.h（后者会把 HAL 头拉进来）
run test_comm    comm_test.log    -I stubs -I ../App/Inc -I ../Hardware

if command -v python3 >/dev/null 2>&1; then
  python3 ../host/tune_helper.py --selftest || fail=1
  (cd ../host && python3 test_protocol.py) || fail=1
fi

if [ "$fail" = "1" ]; then echo "HOST TESTS: SOME FAILED"; exit 1; fi

if command -v python3 >/dev/null 2>&1; then
  python3 snapshot_evidence.py || true   # 证据快照 -> docs/evidence/YYYY-MM-DD
fi
echo "HOST TESTS: ALL PASS"
