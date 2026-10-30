#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo; SPDX-License-Identifier: MIT
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "pass exactly one resolved west workspace" >&2; exit 2; fi
workspace="$(cd "$1" && pwd)"
repo="$(cd "$(dirname "$0")/../../.." && pwd)"
output="${ZMK_TEST_OUTPUT:?set ZMK_TEST_OUTPUT to fresh absolute evidence}"
if [ -e "$output" ] || [ -L "$output" ] || [ -e "$output.build.log" ]; then
    echo "output already exists: $output" >&2; exit 2
fi
case "$output" in /*) ;; *) echo "output must be absolute" >&2; exit 2;; esac
source "$repo/tests/integration/workspace-env.sh"
zmk_test_select_workspace "$workspace"
mkdir -p "$output"
cd "$workspace"
failed=0
for case_name in center-x center-y center-limits; do
    status=0
    (set -x; west build -p=always -o=-j2 -s "$workspace/zmk/app" -d "$output/$case_name" \
        -b native_sim//zmk_test_mock -- -DCONFIG_ASSERT=y \
        -DZMK_CONFIG="$repo/tests/integration/runtime" \
        -DZMK_EXTRA_MODULES="$repo;$repo/tests/integration/module" \
        -DEXTRA_DTC_OVERLAY_FILE="$repo/tests/integration/guards/$case_name.overlay") \
        >"$output/$case_name.log" 2>&1 || status=$?
    echo "$status" >"$output/$case_name.exit"
    if [ "$case_name" = center-limits ]; then
        if [ "$status" -eq 0 ]; then echo "PASS: valid signed center limits compile";
        else echo "FAIL: valid center limits (exit $status)"; failed=1; fi
    else
        axis="${case_name#center-}"
        if [ "$status" -ne 0 ] && grep -Fq \
            "padstick $axis-center must not be the unset coordinate" "$output/$case_name.log"; then
            echo "PASS: $case_name named rejection"
        else echo "FAIL: $case_name missing named rejection (exit $status)"; failed=1; fi
    fi
done
exit "$failed"
