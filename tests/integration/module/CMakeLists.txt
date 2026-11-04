# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_INPUT_PADSTICK_TEST)
    target_sources(app PRIVATE src/runtime_test.c)
    target_link_options(zephyr_interface INTERFACE -Wl,--wrap=input_report)
endif()
