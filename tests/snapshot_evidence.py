#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把本次测试/编译日志归档成"证据快照"。

为什么需要：`tests/*.log` 与 `MDK-ARM/rebuild.log` 每次运行都会变，已加入 .gitignore；
而面试/评审需要一份可追溯的快照，所以这里把当前日志复制到
`docs/evidence/<YYYY-MM-DD>/`（同一天重复运行会覆盖，保持 git 干净）。

用法（在 tests/ 目录下，run_tests.bat/sh 已自动调用）：
    python snapshot_evidence.py          # 归档测试日志
    python snapshot_evidence.py --all    # 额外归档 Keil 编译日志与协议/链路日志

退出码：0 = 至少归档了一份；1 = 一个源文件都没找到（不算错误，调用方忽略即可）。
"""

from __future__ import annotations

import datetime
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEST_ROOT = os.path.join(ROOT, "docs", "evidence")

BASE_SOURCES = [
    ("tests/pid_test.log", "pid_test.log"),
    ("tests/param_test.log", "param_test.log"),
    ("tests/control_test.log", "control_test.log"),
]

EXTRA_SOURCES = [
    ("MDK-ARM/rebuild.log", "rebuild.log"),
    ("host/test_protocol_log.txt", "protocol_test.txt"),
    ("host/monitor_demo_log.txt", "monitor_demo.txt"),
]


def copy_one(rel_src: str, name: str, dest_dir: str) -> bool:
    src = os.path.join(ROOT, rel_src.replace("/", os.sep))
    if not os.path.isfile(src) or os.path.getsize(src) == 0:
        return False
    shutil.copyfile(src, os.path.join(dest_dir, name))
    return True


def main(argv: list[str]) -> int:
    include_extra = "--all" in argv
    day = datetime.date.today().isoformat()
    dest = os.path.join(DEST_ROOT, day)
    os.makedirs(dest, exist_ok=True)

    copied = 0
    for rel, name in BASE_SOURCES:
        if copy_one(rel, name, dest):
            copied += 1
            print(f"  evidence <- {rel}")
    if include_extra:
        for rel, name in EXTRA_SOURCES:
            if copy_one(rel, name, dest):
                copied += 1
                print(f"  evidence <- {rel}")

    if copied:
        print(f"Evidence snapshot: docs/evidence/{day}/  ({copied} file(s))")
        return 0
    print("Evidence snapshot skipped: no logs found (run the tests first)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
