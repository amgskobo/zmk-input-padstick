#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
set -euo pipefail
root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
python3 "$root/tests/runtime/run.py"
python3 -m unittest discover -s "$root/tests" -p 'test_runtime_coverage.py' -v
python3 -m unittest discover -s "$root/tests/integration" -p 'test_*.py' -v
