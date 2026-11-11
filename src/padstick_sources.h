/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
#pragma once

#define PADSTICK_LISTENER_COUNT DT_NUM_INST_STATUS_OKAY(zmk_input_listener)
#ifndef CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES
#define CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES 0
#endif
BUILD_ASSERT(CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES >= 0 &&
             CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES <= 256,
             "PadStick source allocation bound must be between 0 and 256");
#if CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES > 0
#define PADSTICK_STREAM_COUNT CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES
/* ZMK uses global DT_INST indices, not the number of routes referencing this
 * processor. Match its device-status gate and all children (even disabled
 * children), because input_listener.c uses DT_INST_FOREACH_CHILD. */
#define PADSTICK_CHECK_PROCESSOR(node, prop, index, source)                                      \
    COND_CODE_1(DT_NODE_HAS_COMPAT(DT_PHANDLE_BY_IDX(node, prop, index),                        \
                                   zmk_input_processor_padstick),                              \
                (BUILD_ASSERT((source) < PADSTICK_STREAM_COUNT,                                 \
                              "PadStick source limit excludes a referenced listener index");), ())
#define PADSTICK_CHECK_ROUTE(node, source)                                                       \
    COND_CODE_1(DT_NODE_HAS_PROP(node, input_processors),                                      \
                (DT_FOREACH_PROP_ELEM_VARGS(node, input_processors,                           \
                                             PADSTICK_CHECK_PROCESSOR, source)), ())
#define PADSTICK_CHECK_LISTENER(source, compat, unused)                                          \
    COND_CODE_1(DT_NODE_HAS_STATUS(DT_PHANDLE(DT_INST(source, compat), device), okay),           \
                (PADSTICK_CHECK_ROUTE(DT_INST(source, compat), source)                         \
                 DT_FOREACH_CHILD_VARGS(DT_INST(source, compat), PADSTICK_CHECK_ROUTE, source)), ())
DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(zmk_input_listener, PADSTICK_CHECK_LISTENER, unused)
#else
#define PADSTICK_STREAM_COUNT MAX(PADSTICK_LISTENER_COUNT, 1)
#endif
