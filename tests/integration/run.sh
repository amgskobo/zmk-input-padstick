#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

# Build a two-listener firmware fixture and run the native_sim self-tests
# against upstream ZMK or the DYA fork.

set -euo pipefail

variant="${1:-upstream}"
case "$variant" in
upstream | dya) ;;
*)
    echo "unknown integration variant '$variant': expected upstream or dya" >&2
    exit 2
    ;;
esac

tests_dir=/src/tests/integration

if [ -n "${ZMK_TEST_WORKSPACE:-}" ]; then
    work_dir="$ZMK_TEST_WORKSPACE/$variant"
    mkdir -p "$work_dir"
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-padstick-integration.XXXXXX")"
    cleanup() {
        rm -rf "$work_dir"
    }
    trap cleanup EXIT HUP INT TERM
fi

mkdir -p "$work_dir/config"
cp "$tests_dir/config/$variant/west.yml" "$work_dir/config/west.yml"
cd "$work_dir"

if [ ! -d .west ]; then
    west init -l config
fi
west update --narrow --fetch-opt=--depth=1
west zephyr-export

export ZEPHYR_BASE="$work_dir/zephyr"

mkdir -p "$work_dir/build"

firmware_dir="$work_dir/build/firmware"
rm -rf "$firmware_dir"

if ! west build -s "$work_dir/zmk/app" -d "$firmware_dir" -b xiao_ble/nrf52840/zmk -- \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/firmware" \
    -DSHIELD=padstick_test >"$firmware_dir.log" 2>&1; then
    echo "FAILED: $variant firmware fixture did not build"
    tail -n 80 "$firmware_dir.log"
    exit 1
fi

test -f "$firmware_dir/zephyr/zmk.uf2"
grep -q '^CONFIG_ZMK_INPUT_PROCESSOR_PADSTICK=y' "$firmware_dir/zephyr/.config"
grep -q '^CONFIG_ZMK_LOW_PRIORITY_WORK_QUEUE=y' "$firmware_dir/zephyr/.config"
if grep -n 'input_processor_padstick.c:[0-9]*:[0-9]*: warning' "$firmware_dir.log"; then
    echo "FAILED: $variant firmware fixture warned in the processor"
    exit 1
fi
echo "PASS: $variant firmware fixture"

runtime_dir="$work_dir/build/runtime"
rm -rf "$runtime_dir"

if ! west build -s "$work_dir/zmk/app" -d "$runtime_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/module" >"$runtime_dir.log" 2>&1; then
    echo "FAILED: $variant runtime build"
    tail -n 80 "$runtime_dir.log"
    exit 1
fi

runtime_log="$work_dir/build/runtime.run.log"
if ! timeout 30 "$runtime_dir/zephyr/zmk.exe" >"$runtime_log" 2>&1; then
    echo "FAILED: $variant runtime execution"
    tail -n 80 "$runtime_log"
    exit 1
fi
if ! grep -Fq "padstick runtime tests: PASS" "$runtime_log"; then
    echo "FAILED: $variant runtime tests did not report success"
    tail -n 80 "$runtime_log"
    exit 1
fi
grep -F 'PASS: ' "$runtime_log" | sed -e 's/.*PASS: /PASS: /'
echo "PASS: $variant runtime execution"

echo "padstick $variant integration: PASS"
