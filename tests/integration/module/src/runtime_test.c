/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the padstick processor, run inside a native_sim build.
 *
 * The events are handed to the processor directly, with no source device, so
 * no held-direction repeat is ever injected and every result below comes from
 * the event that was just sent.
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <drivers/input_processor.h>
#include <zmk/keymap.h>

static const struct device *const padstick = DEVICE_DT_GET(DT_NODELABEL(padstick_runtime_test));

#define STREAM_A 0U
#define STREAM_B 1U
#define NO_STREAM UINT8_MAX
#define OTHER_LAYER 1

static int process(uint8_t stream, struct input_event *event) {
    struct zmk_input_processor_state state = {
        .input_device_index = stream,
        .remainder = NULL,
    };

    return zmk_input_processor_handle_event(padstick, event, 0, 0, &state);
}

static struct input_event input(uint8_t type, uint16_t code, int32_t value, bool sync) {
    return (struct input_event){
        .type = type,
        .code = code,
        .value = value,
        .sync = sync,
    };
}

static void touch(uint8_t stream, bool down) {
    struct input_event event = input(INPUT_EV_KEY, INPUT_BTN_TOUCH, down, false);

    __ASSERT_NO_MSG(process(stream, &event) == ZMK_INPUT_PROC_STOP);
}

/* Sends a frame the processor is expected to consume: a settle or origin frame. */
static void consumed_frame(uint8_t stream, int32_t x, int32_t y) {
    struct input_event event_x = input(INPUT_EV_ABS, INPUT_ABS_X, x, false);
    struct input_event event_y = input(INPUT_EV_ABS, INPUT_ABS_Y, y, true);

    __ASSERT_NO_MSG(process(stream, &event_x) == ZMK_INPUT_PROC_STOP);
    __ASSERT_NO_MSG(process(stream, &event_y) == ZMK_INPUT_PROC_STOP);
}

/* Sends a frame the processor is expected to turn into this relative step. */
static void moving_frame(uint8_t stream, int32_t x, int32_t y, int32_t rel_x, int32_t rel_y) {
    struct input_event event_x = input(INPUT_EV_ABS, INPUT_ABS_X, x, false);
    struct input_event event_y = input(INPUT_EV_ABS, INPUT_ABS_Y, y, true);

    __ASSERT_NO_MSG(process(stream, &event_x) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(event_x.type == INPUT_EV_REL && event_x.code == INPUT_REL_X);
    __ASSERT_NO_MSG(event_x.value == rel_x);

    __ASSERT_NO_MSG(process(stream, &event_y) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(event_y.type == INPUT_EV_REL && event_y.code == INPUT_REL_Y);
    __ASSERT_NO_MSG(event_y.value == rel_y);
}

/* A layer change reaches the processor through its listener, whatever else answers it. */
static void set_other_layer(bool active) {
    if (active) {
        (void)zmk_keymap_layer_activate(OTHER_LAYER, false);
    } else {
        (void)zmk_keymap_layer_deactivate(OTHER_LAYER, false);
    }

    __ASSERT_NO_MSG(zmk_keymap_layer_active(OTHER_LAYER) == active);
}

static void button(uint8_t stream, bool pressed, int expected) {
    struct input_event event = input(INPUT_EV_KEY, INPUT_BTN_0, pressed, true);

    __ASSERT_NO_MSG(process(stream, &event) == expected);
}

static void test_invalid_stream_passes_through(void) {
    struct input_event event = input(INPUT_EV_KEY, INPUT_BTN_TOUCH, 1, false);

    __ASSERT_NO_MSG(process(NO_STREAM, &event) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(event.type == INPUT_EV_KEY && event.code == INPUT_BTN_TOUCH);
    __ASSERT_NO_MSG(event.value == 1);

    event = input(INPUT_EV_ABS, INPUT_ABS_X, 700, true);
    __ASSERT_NO_MSG(process(NO_STREAM, &event) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(event.type == INPUT_EV_ABS && event.code == INPUT_ABS_X);
    __ASSERT_NO_MSG(event.value == 700 && event.sync);

    event = input(INPUT_EV_KEY, INPUT_BTN_0, 0, true);
    __ASSERT_NO_MSG(process(NO_STREAM, &event) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(event.code == INPUT_BTN_0 && event.value == 0);

    printk("PASS: an index past the listeners passes through untouched\n");
}

static void test_streams_are_independent(void) {
    touch(STREAM_A, true);
    consumed_frame(STREAM_A, 500, 500);
    consumed_frame(STREAM_A, 510, 520);

    /* A contact starting on the other stream must not reset this one's origin. */
    touch(STREAM_B, true);
    consumed_frame(STREAM_B, 100, 100);

    moving_frame(STREAM_A, 530, 520, 20, 0);

    consumed_frame(STREAM_B, 110, 120);
    moving_frame(STREAM_B, 110, 150, 0, 30);

    /* Nor does its release. */
    touch(STREAM_B, false);
    moving_frame(STREAM_A, 510, 500, 0, -20);

    touch(STREAM_A, false);

    printk("PASS: two listeners keep their own contacts on one node\n");
}

static void test_button_suppression_stays_paired(void) {
    /* A release whose press was not suppressed here always passes. */
    button(STREAM_A, false, ZMK_INPUT_PROC_CONTINUE);

    button(STREAM_A, true, ZMK_INPUT_PROC_STOP);
    /* The other stream suppressed no press, so its release passes. */
    button(STREAM_B, false, ZMK_INPUT_PROC_CONTINUE);
    button(STREAM_A, false, ZMK_INPUT_PROC_STOP);

    /* A layer change forgets the press, so its release is no longer dropped. */
    button(STREAM_A, true, ZMK_INPUT_PROC_STOP);
    set_other_layer(true);
    button(STREAM_A, false, ZMK_INPUT_PROC_CONTINUE);
    set_other_layer(false);

    printk("PASS: button suppression stays paired per stream\n");
}

static void test_layer_change_drops_the_origin(void) {
    touch(STREAM_A, true);
    consumed_frame(STREAM_A, 500, 500);
    consumed_frame(STREAM_A, 500, 500);
    moving_frame(STREAM_A, 540, 500, 40, 0);

    /* The contact continues, but its origin is gone and must be taken again. */
    set_other_layer(true);
    consumed_frame(STREAM_A, 540, 500);
    consumed_frame(STREAM_A, 540, 500);
    moving_frame(STREAM_A, 540, 470, 0, -30);
    set_other_layer(false);

    touch(STREAM_A, false);

    printk("PASS: a layer change drops the origin\n");
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    __ASSERT_NO_MSG(device_is_ready(padstick));
    /* The test board brings listeners of its own; two streams are all these tests need. */
    __ASSERT_NO_MSG(DT_NUM_INST_STATUS_OKAY(zmk_input_listener) >= 2);

    test_invalid_stream_passes_through();
    test_streams_are_independent();
    test_button_suppression_stays_paired();
    test_layer_change_drops_the_origin();

    printk("padstick runtime tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(padstick_runtime_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
