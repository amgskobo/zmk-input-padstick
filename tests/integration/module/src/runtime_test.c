/*
 * Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 *
 * Self-tests for the padstick processor, run inside a native_sim build.
 *
 * Arithmetic cases use direct events without a source. Repeat boundary cases
 * use a real input sink and explicitly acknowledge the returned pair, so queue
 * backpressure and cancellation are tested without claiming HID delivery.
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <drivers/input_processor.h>
#include <zmk/keymap.h>
#include <zmk/workqueue.h>

static const struct device *const padstick = DEVICE_DT_GET(DT_NODELABEL(padstick_runtime_test));
static const struct device *const repeat_sink = DEVICE_DT_GET(DT_NODELABEL(repeat_sink));
static const struct device *event_source;

#define STREAM_A 0U
#define STREAM_B 1U
#define NO_STREAM UINT8_MAX
#define OTHER_LAYER 1

static int process(uint8_t stream, struct input_event *event) {
    event->dev = event_source;
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

    struct input_event release = input(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);
    __ASSERT_NO_MSG(process(STREAM_A, &release) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(release.code == INPUT_BTN_TOUCH && release.sync);

    printk("PASS: a layer change drops the origin\n");
}

struct repeat_sample {
    uint16_t code;
    int32_t value;
    bool sync;
    int64_t ms;
};
static struct repeat_sample samples[16];
static atomic_t sample_count;
K_SEM_DEFINE(repeat_blocked, 0, 1);
K_SEM_DEFINE(repeat_resume, 0, 1);
K_SEM_DEFINE(repeat_drained, 0, 1);

static void capture_repeat(struct input_event *event, void *unused) {
    ARG_UNUSED(unused);
    if (event->type != INPUT_EV_REL) {
        return;
    }
    size_t n = atomic_get(&sample_count);
    __ASSERT_NO_MSG(n < ARRAY_SIZE(samples));
    samples[n] = (struct repeat_sample){event->code, event->value, event->sync, k_uptime_get()};
    atomic_inc(&sample_count);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(repeat_sink)), capture_repeat, NULL);

static void block_repeat(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&repeat_blocked);
    __ASSERT_NO_MSG(k_sem_take(&repeat_resume, K_SECONDS(1)) == 0);
}
static void repeat_fence(struct k_work *work) {
    ARG_UNUSED(work);
    k_sem_give(&repeat_drained);
}
K_WORK_DEFINE(repeat_blocker, block_repeat);
K_WORK_DEFINE(repeat_barrier, repeat_fence);

static void begin_repeat(void) {
    atomic_set(&sample_count, 0);
    event_source = repeat_sink;
    touch(STREAM_A, true);
    consumed_frame(STREAM_A, 500, 500);
    consumed_frame(STREAM_A, 510, 520);
    moving_frame(STREAM_A, 530, 520, 20, 0);
}
static void check_pair(size_t n) {
    __ASSERT_NO_MSG(samples[n].code == INPUT_REL_X && samples[n].value == 20 && !samples[n].sync);
    __ASSERT_NO_MSG(samples[n + 1].code == INPUT_REL_Y && samples[n + 1].value == 0 &&
                    samples[n + 1].sync);
    __ASSERT_NO_MSG(samples[n + 1].ms >= samples[n].ms);
}
static void print_samples(void) {
    for (size_t i = 0; i < (size_t)atomic_get(&sample_count); i++) {
        printk("repeat %lld %u %d %u\n", samples[i].ms, samples[i].code, samples[i].value,
               samples[i].sync);
    }
}
static void test_real_repeat_backpressure_and_release(void) {
    int64_t start = k_uptime_get();
    begin_repeat();
    k_msleep(65);
    __ASSERT_NO_MSG(atomic_get(&sample_count) == 2);
    check_pair(0);
    __ASSERT_NO_MSG(samples[0].ms >= start + 20);
    struct input_event x = input(INPUT_EV_REL, INPUT_REL_X, 20, false);
    __ASSERT_NO_MSG(process(STREAM_A, &x) == ZMK_INPUT_PROC_CONTINUE);
    k_msleep(40);
    __ASSERT_NO_MSG(atomic_get(&sample_count) == 2);
    struct input_event y = input(INPUT_EV_REL, INPUT_REL_Y, 0, true);
    __ASSERT_NO_MSG(process(STREAM_A, &y) == ZMK_INPUT_PROC_CONTINUE);
    k_msleep(25);
    __ASSERT_NO_MSG(atomic_get(&sample_count) == 4);
    check_pair(2);
    touch(STREAM_A, false);
    k_msleep(65);
    __ASSERT_NO_MSG(atomic_get(&sample_count) == 4);
    print_samples();
    event_source = NULL;
    printk("PASS: one actual input pair stays in flight until sync; release stops later repeats\n");
}
static void test_overdue_repeat_cancel(bool layer) {
    struct k_work_q *q = zmk_workqueue_lowprio_work_q();
    __ASSERT_NO_MSG(k_work_submit_to_queue(q, &repeat_blocker) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&repeat_blocked, K_SECONDS(1)) == 0);
    begin_repeat();
    k_msleep(30);
    if (layer) {
        set_other_layer(true);
    } else {
        touch(STREAM_A, false);
    }
    k_sem_give(&repeat_resume);
    __ASSERT_NO_MSG(k_work_submit_to_queue(q, &repeat_barrier) >= 0);
    __ASSERT_NO_MSG(k_sem_take(&repeat_drained, K_SECONDS(1)) == 0);
    k_msleep(45);
    __ASSERT_NO_MSG(atomic_get(&sample_count) == 0);
    if (layer) {
        set_other_layer(false);
    }
    event_source = NULL;
    printk("PASS: %s cancels overdue repeat before its input pair escapes\n",
           layer ? "layer event" : "touch release");
}

static void test_touch_release_boundaries(void) {
    const struct device *transparent = DEVICE_DT_GET(DT_NODELABEL(padstick_touch_passthrough));
    __ASSERT_NO_MSG(device_is_ready(transparent));
    for (uint8_t stream = STREAM_A; stream <= STREAM_B; stream++) {
        struct zmk_input_processor_state state = {.input_device_index = stream};
        for (int value = 1; value >= 0; value--) {
            struct input_event edge = input(INPUT_EV_KEY, INPUT_BTN_TOUCH, value, false);
            __ASSERT_NO_MSG(zmk_input_processor_handle_event(transparent, &edge, 0, 0, &state) ==
                            ZMK_INPUT_PROC_CONTINUE);
            __ASSERT_NO_MSG(edge.code == INPUT_BTN_TOUCH && edge.sync);
        }
    }
    touch(STREAM_A, true);
    struct input_event unmatched = input(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);
    __ASSERT_NO_MSG(process(STREAM_B, &unmatched) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(unmatched.code == INPUT_BTN_TOUCH && unmatched.sync);
    touch(STREAM_A, false);
    touch(STREAM_A, true);
    set_other_layer(true);
    struct input_event changed_route = input(INPUT_EV_KEY, INPUT_BTN_TOUCH, 0, false);
    __ASSERT_NO_MSG(process(STREAM_A, &changed_route) == ZMK_INPUT_PROC_CONTINUE);
    __ASSERT_NO_MSG(changed_route.code == INPUT_BTN_TOUCH && changed_route.sync);
    set_other_layer(false);
    printk("PASS: touch sync and release ownership survive stream and layer boundaries\n");
}

static void run_tests(void *p1, void *p2, void *p3) {
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    __ASSERT_NO_MSG(device_is_ready(padstick));
    /* The test board brings listeners of its own; two streams are all these tests need. */
    __ASSERT_NO_MSG(DT_NUM_INST_STATUS_OKAY(zmk_input_listener) >= 2);

    test_invalid_stream_passes_through();
    test_touch_release_boundaries();
    test_streams_are_independent();
    test_button_suppression_stays_paired();
    test_layer_change_drops_the_origin();
    __ASSERT_NO_MSG(device_is_ready(repeat_sink));
    test_real_repeat_backpressure_and_release();
    test_overdue_repeat_cancel(false);
    test_overdue_repeat_cancel(true);

    printk("padstick runtime tests: PASS\n");
    exit(0);
}

K_THREAD_DEFINE(padstick_runtime_tests, 4096, run_tests, NULL, NULL, NULL,
                K_LOWEST_APPLICATION_THREAD_PRIO, 0, 100);
