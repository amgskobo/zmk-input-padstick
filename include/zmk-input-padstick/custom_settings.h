/* SPDX-License-Identifier: MIT */
#pragma once
#include <zephyr/devicetree.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>
/* Readable group ID; longest field permits a 14-character node at 64 bytes. */
#define ZMK_INPUT_PADSTICK_SUBSYSTEM_TOKEN amgskobo__padstick
#define ZMK_INPUT_PADSTICK_SUBSYSTEM STRINGIFY(ZMK_INPUT_PADSTICK_SUBSYSTEM_TOKEN)
#define PADSTICK_SETTING_KEY(n, field) DT_NODE_FULL_NAME(DT_DRV_INST(n)) "." field
#define PADSTICK_STORAGE_NAME(n, field) \
    "custom_settings/" ZMK_INPUT_PADSTICK_SUBSYSTEM "/" PADSTICK_SETTING_KEY(n, field)
#define PADSTICK_ASSERT_KEY(n) \
    BUILD_ASSERT(sizeof(PADSTICK_SETTING_KEY(n, "y_accel_range")) <= CONFIG_ZMK_CUSTOM_SETTINGS_KEY_MAX_LEN, "PadStick setting key too long"); \
    BUILD_ASSERT(sizeof(PADSTICK_STORAGE_NAME(n, "y_accel_range")) <= SETTINGS_MAX_NAME_LEN, "PadStick storage key too long");
