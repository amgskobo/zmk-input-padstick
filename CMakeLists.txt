# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_INPUT_PROCESSOR_PADSTICK)
    add_subdirectory(src)
    # drivers/input_processor.h is provided by the ZMK application include tree.
    zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()

zephyr_include_directories(include)
if(CONFIG_ZMK_INPUT_PADSTICK_CUSTOM_SETTINGS)
    target_sources(app PRIVATE src/input_processor_padstick_custom_settings.c)
endif()
