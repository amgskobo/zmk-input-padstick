/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
#if EXPECTED_STREAMS >= 2
#define main complete_driver_main
#include "driver_harness.c"
#undef main
#else
#include "driver_support.h"
static struct padstick_data owner;
static struct device pad = {.name = "pad"};
static struct padstick_stream *stream(size_t index) { return &owner.streams[index]; }
static int touch(struct zmk_input_processor_state *state, bool down) {
    struct input_event event = {.dev = &pad, .type = INPUT_EV_KEY,
                               .code = INPUT_BTN_TOUCH, .value = down};
    return padstick_handle_event(&processor, &event, 0, 0, state);
}
#endif

#ifndef EXPECTED_STREAMS
#error "declare the expected source allocation independently"
#endif

int main(void) {
    assert(PADSTICK_STREAM_COUNT == EXPECTED_STREAMS &&
           "PadStick must honor the declared source allocation bound");
    struct padstick_params params = {
        .x_deadzone = 0, .y_deadzone = 0, .x_scale = 256, .y_scale = 256,
        .max_x = 127, .max_y = 127, .x_center = 512, .y_center = 512,
    };
    processor = (struct device){.config = &params, .data = &owner,
                               .api = &padstick_driver_api};
    assert(padstick_init(&processor) == 0);
    /* NULL remains source zero; the highest admitted source retains its own
     * origin and fraction even when there are no DT listeners at all. */
    touch(NULL, true);
    assert(stream(0)->touch_active);
    const uint8_t last = EXPECTED_STREAMS - 1;
    struct zmk_input_processor_state highest = {.input_device_index = last};
    touch(&highest, true);
    assert(stream(last)->touch_active);
    if (last > 0) {
        touch(NULL, false);
        assert(!stream(0)->touch_active && stream(last)->touch_active);
    }
    if (EXPECTED_STREAMS < 256) {
        const struct padstick_data before = owner;
        struct zmk_input_processor_state invalid = {.input_device_index = (uint8_t)EXPECTED_STREAMS};
        struct input_event event = {.dev = &pad, .type = INPUT_EV_ABS,
                                   .code = INPUT_ABS_X, .value = 123, .sync = true};
        const struct input_event original = event;
        assert(padstick_handle_event(&processor, &event, 0, 0, &invalid) == 0);
        assert(memcmp(&event, &original, sizeof(event)) == 0);
        assert(memcmp(&owner, &before, sizeof(owner)) == 0);
    }
    puts("padstick source allocation: PASS");
    printf("streams=%d stream-bytes=%zu node-bytes=%zu\n",
           PADSTICK_STREAM_COUNT, sizeof(struct padstick_stream), sizeof(struct padstick_data));
#if EXPECTED_STREAMS >= 2
    return complete_driver_main();
#else
    return 0;
#endif
}
