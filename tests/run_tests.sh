#!/usr/bin/env bash
# 宿主机测试（gcc/clang，Linux/macOS/WSL/MSYS2 均可）：PID + 参数编解码 + 控制状态机
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

# 日志已 gitignore：MSVC 记录留在 tests/*.log（本地），gcc 结果写到临时目录；快照统一由 snapshot_evidence.py 归档
run test_pid     "$OUT/pid_test.log"     -I ../App/Inc
run test_param   "$OUT/param_test.log"   -I ../App/Inc
run test_control control_test.log        -I stubs -I ../App/Inc -I ../Hardware

if command -v python3 >/dev/null 2>&1; then
  python3 -I ../host/tune_helper.py --selftest || fail=1
fi

if [ "$fail" = "1" ]; then echo "HOST TESTS: SOME FAILED"; exit 1; fi

if command -v python3 >/dev/null 2>&1; then
  python3 snapshot_evidence.py || true   # 证据快照 -> docs/evidence/YYYY-MM-DD
fi
echo "HOST TESTS: ALL PASS"
