#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
set -euo pipefail
if [ "$#" -ne 1 ]; then echo "pass exactly one resolved west workspace" >&2; exit 2; fi
workspace="$(cd "${1:?pass an already resolved west workspace}" && pwd)"
repo="$(cd "$(dirname "$0")/.." && pwd)"
output="${ZMK_TEST_OUTPUT:-$workspace/build/padstick-native}"
if [ -e "$output" ] || [ -L "$output" ] || [ -e "$output.build.log" ]; then
  echo "output already exists: $output" >&2
  exit 2
fi
cd "$workspace"
mkdir -p "$(dirname "$output")"
west zephyr-export
export ZEPHYR_BASE="$workspace/zephyr"
source "$repo/tests/integration/workspace-env.sh"
zmk_test_select_workspace "$workspace"

cmake_args=()
if [ "${ZMK_TEST_CUSTOM_SETTINGS:-0}" = 1 ]; then
    cmake_args+=("-DEXTRA_CONF_FILE=$repo/tests/integration/custom-settings.conf")
fi
if [ -n "${ZMK_TEST_PADSTICK_MAX_SOURCES:-}" ]; then
    cmake_args+=("-DCONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES=$ZMK_TEST_PADSTICK_MAX_SOURCES")
fi
source_indices=0,1
if [ "${ZMK_TEST_PADSTICK_ORIGINAL_ORDER:-0}" = 1 ]; then
    cmake_args+=("-DDTS_EXTRA_CPPFLAGS=-DPADSTICK_TEST_ORIGINAL_ORDER=1")
    source_indices=3,4
fi
west build -p=always -o=-j2 -s "$workspace/zmk/app" -d "$output" \
  -b native_sim//zmk_test_mock -- -DCONFIG_ASSERT=y \
  -DZMK_CONFIG="$repo/tests/integration/runtime" \
  -DZMK_EXTRA_MODULES="$repo;$repo/tests/integration/module" "${cmake_args[@]}" >"$output.build.log" 2>&1
nm_tool="$(sed -n 's/^CMAKE_NM:FILEPATH=//p' "$output/CMakeCache.txt")"
"$nm_tool" --defined-only "$output/zephyr/zmk.exe" >"$output/repeat-symbols.txt"
python3 "$repo/tests/integration/check-repeat-callback.py" "$output/repeat-symbols.txt"
python3 "$repo/tests/integration/check-source-bound.py" "$workspace" "$output" \
    --expected-sources "$source_indices" --output "$output/source-bound-check"
for run in 1 2 3; do
  flash=()
  if [ "${ZMK_TEST_CUSTOM_SETTINGS:-0}" = 1 ]; then flash=("--flash=$output.flash-$run.bin"); fi
  timeout 30 "$output/zephyr/zmk.exe" "${flash[@]}" >"$output.run-$run.log" 2>&1
  grep -Fq 'padstick runtime tests: PASS' "$output.run-$run.log"
  cat "$output.run-$run.log"
  if [ "${ZMK_TEST_CUSTOM_SETTINGS:-0}" = 1 ]; then
    timeout 30 "$output/zephyr/zmk.exe" "${flash[@]}" >"$output.reload-$run.log" 2>&1
    grep -Fq 'persisted PadStick value reloads' "$output.reload-$run.log"
    cat "$output.reload-$run.log"
  fi
done

if [ "${ZMK_TEST_CUSTOM_SETTINGS:-0}" = 1 ]; then
  python3 "$repo/tests/integration/check-settings-logs.py" "$output.run-{i}.log" "$output.reload-{i}.log"
fi
