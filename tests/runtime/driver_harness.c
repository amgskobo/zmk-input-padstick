/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { int64_t ms; } k_timeout_t;
struct k_mutex { int depth; };
struct k_work { void (*handler)(struct k_work *work); };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct k_work_q { int unused; };
struct device { const void *config; void *data; const char *name; };
struct input_event { const struct device *dev; uint8_t sync; uint8_t type; uint16_t code;
                     int32_t value; };
struct zmk_input_processor_state { uint8_t input_device_index; };

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ARG_UNUSED(x) ((void)(x))
#define CONTAINER_OF(ptr, type, field) ((type *)(void *)((char *)(ptr) - offsetof(type, field)))
/* Unevaluated printf() keeps the format strings checked and the arguments used. */
#define LOG_DBG(fmt, ...) ((void)sizeof(printf(fmt, ##__VA_ARGS__)))
#define LOG_WRN(fmt, ...) ((void)warnings++, LOG_DBG(fmt, ##__VA_ARGS__))
#define K_FOREVER ((k_timeout_t){-1})
#define K_NO_WAIT ((k_timeout_t){0})
#define K_MSEC(ms) ((k_timeout_t){(ms)})
#define INPUT_EV_KEY 0x01
#define INPUT_EV_REL 0x02
#define INPUT_EV_ABS 0x03
#define INPUT_EV_MSC 0x04
#define INPUT_REL_X 0x00
#define INPUT_REL_Y 0x01
#define INPUT_ABS_X 0x00
#define INPUT_ABS_Y 0x01
#define INPUT_ABS_PRESSURE 0x18
#define INPUT_BTN_0 0x100
#define INPUT_BTN_TOUCH 0x14a
#define INPUT_KEY_A 30
#define ZMK_INPUT_PROC_CONTINUE 0
#define ZMK_INPUT_PROC_STOP 1

#define PADSTICK_COORD_UNSET INT32_MIN
#define PADSTICK_DISTANCE_MAX 32767
#define PADSTICK_INVALID_CODE 0xFFF
#define PADSTICK_SCALE_MAX 4096
#define PADSTICK_SCALE_SHIFT 8
#define PADSTICK_SCALE_ONE ((int32_t)1 << PADSTICK_SCALE_SHIFT)
#define PADSTICK_STREAM_COUNT 2
#define PADSTICK_REPEAT_INTERVAL_MS 20
#define ZMK_EV_EVENT_BUBBLE 0
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DEVICE_DT_INST_GET(n) (&processor)
static struct device processor;
typedef struct { int kind; } zmk_event_t;

struct padstick_config {
    int32_t x_deadzone, y_deadzone, x_scale, y_scale, x_accel_range, y_accel_range;
    int32_t x_accel_scale, y_accel_scale, max_x, max_y, x_center, y_center;
    bool invert_x, invert_y, fixed_center, suppress_abs, suppress_btn_touch, suppress_btn0;
};
struct padstick_data;
struct padstick_stream {
    struct k_mutex lock;
    struct k_work_delayable repeat_work;
    struct padstick_data *owner;
    const struct device *input_dev;
    bool touch_active;
    uint8_t unsignaled_contact_frames;
    int32_t origin_x, origin_y, x_remainder, y_remainder;
    bool skip_origin_frame;
    int32_t last_x, last_y;
    bool btn0_press_suppressed;
    bool repeat_in_flight;
};
struct padstick_data {
    const struct device *processor;
    struct padstick_stream streams[PADSTICK_STREAM_COUNT];
};

static int warnings;
static struct k_work_q lowprio_queue;
static int report_result;
static int reports;
static int32_t reported[2];
static bool reported_sync[2];

static struct k_work_q *zmk_workqueue_lowprio_work_q(void) { return &lowprio_queue; }
static void k_mutex_init(struct k_mutex *mutex) { mutex->depth = 0; }
static int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout) {
    ARG_UNUSED(timeout);
    mutex->depth++;
    return 0;
}
static int k_mutex_unlock(struct k_mutex *mutex) {
    assert(mutex->depth > 0);
    mutex->depth--;
    return 0;
}
static void k_work_init_delayable(struct k_work_delayable *work,
                                  void (*handler)(struct k_work *work)) {
    *work = (struct k_work_delayable){.work = {.handler = handler}};
}
static struct k_work_delayable *k_work_delayable_from_work(struct k_work *work) {
    return CONTAINER_OF(work, struct k_work_delayable, work);
}
static int k_work_reschedule_for_queue(struct k_work_q *queue, struct k_work_delayable *work,
                                       k_timeout_t delay) {
    assert(queue == &lowprio_queue);
    work->scheduled = true;
    work->delay_ms = delay.ms;
    return 1;
}
static int k_work_cancel_delayable(struct k_work_delayable *work) {
    work->scheduled = false;
    return 0;
}
static bool listener_takes_pair = true;
static int padstick_handle_event(const struct device *dev, struct input_event *event,
                                 uint32_t param1, uint32_t param2,
                                 struct zmk_input_processor_state *state);

/*
 * The repeat hands its pair back to the source device's listener. Unless a test
 * holds it back, the listener takes the pair at once and runs it through the
 * chain, this processor included, as a synchronous listener would.
 */
static int input_report_rel(const struct device *dev, uint16_t code, int32_t value, bool sync,
                            k_timeout_t timeout) {
    assert(dev != NULL && code <= INPUT_REL_Y && timeout.ms == 0);
    reported[code] = value;
    reported_sync[code] = sync;
    reports++;
    if (report_result == 0 && listener_takes_pair) {
        struct input_event event = {.dev = dev, .sync = sync, .type = INPUT_EV_REL, .code = code,
                                    .value = value};
        assert(padstick_handle_event(&processor, &event, 0, 0, NULL) == 0);
    }
    return report_result;
}

/* DRIVER_FUNCTIONS */

static struct padstick_data owner;
static struct device pad = {.name = "pad"};
static struct zmk_input_processor_state second = {.input_device_index = 1};

static int send(struct zmk_input_processor_state *state, uint8_t type, uint16_t code,
                int32_t value, bool sync) {
    struct input_event event = {.dev = &pad, .sync = sync, .type = type, .code = code,
                                .value = value};
    const int ret = padstick_handle_event(&processor, &event, 0, 0, state);
    if (ret == ZMK_INPUT_PROC_STOP) {
        assert(event.code == PADSTICK_INVALID_CODE && !event.sync);
    }
    return ret;
}

/* Converted events come back as REL; return the step, or a sentinel when stopped. */
#define STOPPED INT32_MIN
static int32_t axis(struct zmk_input_processor_state *state, uint16_t code, int32_t value,
                    bool sync) {
    struct input_event event = {.dev = &pad, .sync = sync, .type = INPUT_EV_ABS, .code = code,
                                .value = value};
    if (padstick_handle_event(&processor, &event, 0, 0, state) == ZMK_INPUT_PROC_STOP) {
        return STOPPED;
    }
    assert(event.type == INPUT_EV_REL && event.code == code);
    return event.value;
}

static int touch(struct zmk_input_processor_state *state, bool down) {
    return send(state, INPUT_EV_KEY, INPUT_BTN_TOUCH, down, false);
}

static struct padstick_stream *stream(size_t index) { return &owner.streams[index]; }

static void run_repeat(size_t index) {
    stream(index)->repeat_work.scheduled = false;
    stream(index)->repeat_work.work.handler(&stream(index)->repeat_work.work);
}

/* Press, settle frame, origin frame: the contact is ready to step. */
static void start_contact(int32_t x, int32_t y) {
    assert(touch(NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(axis(NULL, INPUT_ABS_X, x, false) == STOPPED);
    assert(send(NULL, INPUT_EV_ABS, INPUT_ABS_Y, y, true) == ZMK_INPUT_PROC_STOP);
    assert(axis(NULL, INPUT_ABS_X, x, false) == STOPPED);
    assert(axis(NULL, INPUT_ABS_Y, y, true) == STOPPED);
    assert(stream(0)->origin_x == x && stream(0)->origin_y == y);
}

static void test_init_and_routing(void) {
    /* Init clears whatever a stream held. */
    for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
        owner.streams[i].touch_active = true;
        owner.streams[i].btn0_press_suppressed = true;
        owner.streams[i].lock.depth = 3;
        owner.streams[i].input_dev = &pad;
        owner.streams[i].x_remainder = owner.streams[i].y_remainder = 7;
        owner.streams[i].unsignaled_contact_frames = 1;
    }
    assert(padstick_init(&processor) == 0);
    for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
        assert(!stream(i)->touch_active && !stream(i)->btn0_press_suppressed);
        assert(stream(i)->lock.depth == 0 && stream(i)->input_dev == NULL);
        assert(stream(i)->x_remainder == 0 && stream(i)->y_remainder == 0);
        assert(stream(i)->unsignaled_contact_frames == 0);
    }
    assert(owner.processor == &processor);
    for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
        assert(stream(i)->owner == &owner && stream(i)->skip_origin_frame);
        assert(stream(i)->origin_x == PADSTICK_COORD_UNSET);
    }

    /* Past the listeners: untouched and unlocked. */
    struct zmk_input_processor_state stranger = {.input_device_index = PADSTICK_STREAM_COUNT};
    assert(send(&stranger, INPUT_EV_ABS, INPUT_ABS_X, 5, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(stream(0)->last_x == PADSTICK_COORD_UNSET);
    /* Neither a key, a relative step nor an absolute axis. */
    assert(send(NULL, INPUT_EV_MSC, 0, 3, true) == ZMK_INPUT_PROC_CONTINUE);
    /* A key that is neither the touch nor BTN_0. */
    assert(send(NULL, INPUT_EV_KEY, INPUT_KEY_A, 1, false) == ZMK_INPUT_PROC_CONTINUE);
    /* An absolute axis other than X and Y passes unless told otherwise. */
    assert(send(NULL, INPUT_EV_ABS, INPUT_ABS_PRESSURE, 40, false) == ZMK_INPUT_PROC_CONTINUE);
}

static void test_contact_steps(void) {
    start_contact(500, 500);
    assert(stream(0)->touch_active && stream(0)->input_dev == &pad);

    /* Inside the deadzone nothing moves, and a repeat that finds nothing to
     * send does not arm another... */
    assert(axis(NULL, INPUT_ABS_X, 510, false) == 0);
    assert(axis(NULL, INPUT_ABS_Y, 505, true) == 0);
    run_repeat(0);
    assert(reports == 0 && !stream(0)->repeat_work.scheduled);

    /* ...outside it, both axes step and the repeat keeps them going. */
    assert(axis(NULL, INPUT_ABS_X, 700, false) > 0);
    assert(axis(NULL, INPUT_ABS_Y, 300, true) < 0);
    assert(stream(0)->repeat_work.scheduled && stream(0)->repeat_work.delay_ms == 20);
    run_repeat(0);
    assert(reports == 2 && reported[INPUT_REL_X] > 0 && reported[INPUT_REL_Y] < 0);
    assert(!reported_sync[INPUT_REL_X] && reported_sync[INPUT_REL_Y]);
    assert(stream(0)->repeat_work.scheduled);

    /* A report the listener refuses is logged and does not stop the repeat. */
    assert(warnings == 0); /* delivered reports warn about nothing */
    report_result = -11;
    run_repeat(0);
    assert(warnings == 2 && stream(0)->repeat_work.scheduled);
    /* A pair that never made it leaves nothing in flight. */
    assert(!stream(0)->repeat_in_flight);
    report_result = 0;

    /* Straight down: X steps nothing and the pair is still sent. */
    assert(axis(NULL, INPUT_ABS_X, 500, false) == 0);
    run_repeat(0);
    assert(reports == 6 && reported[INPUT_REL_X] == 0 && reported[INPUT_REL_Y] < 0);
    assert(!stream(0)->repeat_in_flight); /* the listener took the pair */

    /* A listener held up: one pair waits in the queue, and the repeat adds
     * nothing to it until the pair's synchronized step comes back. */
    listener_takes_pair = false;
    run_repeat(0);
    assert(reports == 8 && stream(0)->repeat_in_flight);
    run_repeat(0);
    assert(reports == 8 && stream(0)->repeat_work.scheduled);
    assert(send(NULL, INPUT_EV_REL, INPUT_REL_X, 1, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(stream(0)->repeat_in_flight); /* not the step that closes it */
    assert(send(NULL, INPUT_EV_REL, INPUT_REL_Y, 1, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(!stream(0)->repeat_in_flight);
    run_repeat(0);
    assert(reports == 10 && stream(0)->repeat_in_flight);
    listener_takes_pair = true;

    /* Diagonal: each axis is given the other's offset from the origin. The
     * math itself is checked in the math harness; here it is the oracle. */
    assert(touch(NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    /* A touch edge starts the contact over with nothing in flight. */
    assert(!stream(0)->repeat_in_flight);
    start_contact(500, 500);
    const struct padstick_config *cfg = processor.config;
    int32_t rx = stream(0)->x_remainder;
    const int32_t want_x = padstick_apply_axis('x', 800, 500, cfg->x_deadzone, cfg->x_scale,
                                               cfg->x_accel_range, cfg->x_accel_scale,
                                               cfg->max_x, cfg->invert_x, &rx, 0);
    assert(axis(NULL, INPUT_ABS_X, 800, false) == want_x);
    int32_t ry = stream(0)->y_remainder;
    const int32_t want_y = padstick_apply_axis('y', 800, 500, cfg->y_deadzone, cfg->y_scale,
                                               cfg->y_accel_range, cfg->y_accel_scale,
                                               cfg->max_y, cfg->invert_y, &ry, 300);
    assert(axis(NULL, INPUT_ABS_Y, 800, true) == want_y && want_y != 0);
    rx = stream(0)->x_remainder;
    ry = stream(0)->y_remainder;
    const int32_t repeat_x = padstick_apply_axis('x', 800, 500, cfg->x_deadzone, cfg->x_scale,
                                                 cfg->x_accel_range, cfg->x_accel_scale,
                                                 cfg->max_x, cfg->invert_x, &rx, 300);
    const int32_t repeat_y = padstick_apply_axis('y', 800, 500, cfg->y_deadzone, cfg->y_scale,
                                                 cfg->y_accel_range, cfg->y_accel_scale,
                                                 cfg->max_y, cfg->invert_y, &ry, 300);
    reports = 0;
    run_repeat(0);
    assert(reports == 2 && reported[INPUT_REL_X] == repeat_x &&
           reported[INPUT_REL_Y] == repeat_y);
    assert(touch(NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    start_contact(500, 500);
    assert(axis(NULL, INPUT_ABS_Y, 700, true) > 0);
    assert(stream(0)->repeat_work.scheduled);

    /* The release cancels the repeat and resets the contact, remainders and
     * inferred-contact count included. */
    stream(0)->x_remainder = stream(0)->y_remainder = 5;
    stream(0)->unsignaled_contact_frames = 1;
    assert(touch(NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(!stream(0)->touch_active && stream(0)->input_dev == NULL);
    assert(!stream(0)->repeat_work.scheduled && stream(0)->skip_origin_frame);
    assert(stream(0)->x_remainder == 0 && stream(0)->y_remainder == 0);
    assert(stream(0)->unsignaled_contact_frames == 0);
    reports = 0;
    run_repeat(0);
    assert(reports == 0);
}

static void test_partial_frames(void) {
    /* The first X of a contact whose Y has not been seen shares nothing. */
    assert(touch(NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    run_repeat(0); /* still settling */
    assert(axis(NULL, INPUT_ABS_X, 500, true) == STOPPED); /* settle, closes on this sync */
    run_repeat(0);                                          /* origins unset */
    assert(axis(NULL, INPUT_ABS_X, 500, false) == STOPPED); /* x origin */
    run_repeat(0);                                          /* y origin unset */
    assert(axis(NULL, INPUT_ABS_X, 800, false) > 0);        /* y never seen */
    assert(!stream(0)->repeat_work.scheduled);              /* last_y unset */
    assert(axis(NULL, INPUT_ABS_Y, 500, true) == STOPPED);  /* y origin */
    assert(axis(NULL, INPUT_ABS_X, 800, false) > 0);        /* shares with y */
    assert(stream(0)->repeat_work.scheduled);

    /* The same, Y first: a seen X without an origin shares nothing. */
    assert(touch(NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(axis(NULL, INPUT_ABS_Y, 500, false) == STOPPED); /* settle stays open */
    assert(axis(NULL, INPUT_ABS_X, 500, true) == STOPPED);  /* settle closes */
    assert(axis(NULL, INPUT_ABS_Y, 500, false) == STOPPED); /* y origin */
    assert(axis(NULL, INPUT_ABS_Y, 200, true) < 0);         /* x seen, no origin */
    assert(axis(NULL, INPUT_ABS_X, 500, true) == STOPPED);  /* x origin */
    assert(axis(NULL, INPUT_ABS_Y, 200, true) < 0);

    /* A seen Y without an origin shares nothing either. */
    assert(touch(NULL, true) == ZMK_INPUT_PROC_CONTINUE);
    assert(axis(NULL, INPUT_ABS_Y, 500, true) == STOPPED); /* settle */
    assert(axis(NULL, INPUT_ABS_X, 500, false) == STOPPED); /* x origin */
    assert(axis(NULL, INPUT_ABS_X, 800, false) > 0);
    assert(touch(NULL, false) == ZMK_INPUT_PROC_CONTINUE);

    /* A touch reported by a source with no device never repeats. */
    struct input_event press = {.dev = NULL, .type = INPUT_EV_KEY, .code = INPUT_BTN_TOUCH,
                                .value = 1};
    assert(padstick_handle_event(&processor, &press, 0, 0, NULL) == ZMK_INPUT_PROC_CONTINUE);
    stream(0)->skip_origin_frame = false;
    stream(0)->origin_x = stream(0)->origin_y = 0;
    stream(0)->last_x = stream(0)->last_y = 100;
    run_repeat(0);
    assert(reports == 0);
    assert(touch(NULL, false) == ZMK_INPUT_PROC_CONTINUE);
    /* An axis whose origin is not taken yet shares nothing, even though its
     * last coordinate is known: exactly the step of this axis alone. */
    const struct padstick_config *cfg = processor.config;
    for (int axis_y = 0; axis_y <= 1; axis_y++) {
        assert(touch(NULL, false) == ZMK_INPUT_PROC_CONTINUE);
        assert(touch(NULL, true) == ZMK_INPUT_PROC_CONTINUE);
        assert(axis(NULL, INPUT_ABS_X, 500, false) == STOPPED); /* settle frame, */
        assert(axis(NULL, INPUT_ABS_Y, 500, true) == STOPPED);  /* both axes seen */
        const uint16_t code = axis_y ? INPUT_ABS_Y : INPUT_ABS_X;
        assert(axis(NULL, code, 500, false) == STOPPED);        /* this origin only */
        int32_t rem = axis_y ? stream(0)->y_remainder : stream(0)->x_remainder;
        const int32_t want = padstick_apply_axis('x', 800, 500, cfg->x_deadzone, cfg->x_scale,
                                                 cfg->x_accel_range, cfg->x_accel_scale,
                                                 cfg->max_x, false, &rem, 0);
        assert(want != 0 && axis(NULL, code, 800, false) == want);
    }
    assert(touch(NULL, false) == ZMK_INPUT_PROC_CONTINUE);
}

static void test_inferred_contact(void) {
    /* A layer switch dropped the press: two complete converted frames prove a
     * contact is still down, and the repeat resumes on its own. */
    assert(touch(&second, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(axis(&second, INPUT_ABS_X, 500, false) == STOPPED);
    assert(axis(&second, INPUT_ABS_Y, 500, true) == STOPPED);
    assert(axis(&second, INPUT_ABS_X, 500, false) == STOPPED);
    assert(axis(&second, INPUT_ABS_Y, 500, true) == STOPPED);
    assert(axis(&second, INPUT_ABS_X, 800, false) > 0);
    assert(axis(&second, INPUT_ABS_Y, 800, false) > 0); /* no sync: not a frame */
    assert(stream(1)->unsignaled_contact_frames == 0);
    assert(axis(&second, INPUT_ABS_Y, 800, true) > 0);
    assert(stream(1)->unsignaled_contact_frames == 1 && !stream(1)->touch_active);
    assert(axis(&second, INPUT_ABS_X, 800, false) > 0);
    assert(axis(&second, INPUT_ABS_Y, 800, true) > 0);
    assert(stream(1)->touch_active && stream(1)->input_dev == &pad);
    assert(stream(1)->repeat_work.scheduled);
    /* Known to be touching now: further frames are not counted. */
    assert(axis(&second, INPUT_ABS_Y, 800, true) > 0);
    assert(stream(1)->unsignaled_contact_frames == 2);
    assert(touch(&second, false) == ZMK_INPUT_PROC_CONTINUE);
}

static void test_buttons(void) {
    /* Suppression is paired: only the release of a dropped press is dropped. */
    assert(send(NULL, INPUT_EV_KEY, INPUT_BTN_0, 1, false) == ZMK_INPUT_PROC_STOP);
    assert(send(NULL, INPUT_EV_KEY, INPUT_BTN_0, 0, false) == ZMK_INPUT_PROC_STOP);
    assert(send(NULL, INPUT_EV_KEY, INPUT_BTN_0, 0, false) == ZMK_INPUT_PROC_CONTINUE);
}

static void test_fixed_center_config(struct padstick_config *config) {
    /* Both origins come from the center, so the first frame after the settle
     * frame already steps, and the first axis is shared radially. */
    config->fixed_center = true;
    config->suppress_btn_touch = true;
    config->suppress_btn0 = false;
    config->suppress_abs = true;

    assert(touch(NULL, true) == ZMK_INPUT_PROC_STOP);
    assert(axis(NULL, INPUT_ABS_Y, 500, true) == STOPPED); /* settle */
    assert(axis(NULL, INPUT_ABS_Y, 900, true) > 0);         /* seeds both */
    assert(stream(0)->origin_x == 512 && stream(0)->origin_y == 512);
    run_repeat(0); /* last_x unset */
    assert(reports == 0);
    assert(axis(NULL, INPUT_ABS_X, 100, true) < 0);
    run_repeat(0);
    assert(reports == 2);

    assert(touch(NULL, true) == ZMK_INPUT_PROC_STOP);
    assert(axis(NULL, INPUT_ABS_X, 500, true) == STOPPED); /* settle */
    /* Seeds both origins; Y has no coordinate yet, so X steps alone - by
     * exactly its own step, below the limit so a wrong share would show. */
    int32_t rem = stream(0)->x_remainder;
    const int32_t alone = padstick_apply_axis('x', 600, 512, config->x_deadzone, config->x_scale,
                                              config->x_accel_range, config->x_accel_scale,
                                              config->max_x, config->invert_x, &rem, 0);
    assert(alone > 0 && alone < config->max_x);
    assert(axis(NULL, INPUT_ABS_X, 600, true) == alone);
    run_repeat(0); /* last_y unset */
    assert(reports == 2);

    /* BTN_0 left alone, and a stale suppression forgotten. */
    stream(0)->btn0_press_suppressed = true;
    assert(send(NULL, INPUT_EV_KEY, INPUT_BTN_0, 0, false) == ZMK_INPUT_PROC_CONTINUE);
    assert(!stream(0)->btn0_press_suppressed);
    /* Other absolute axes suppressed. */
    assert(send(NULL, INPUT_EV_ABS, INPUT_ABS_PRESSURE, 40, false) == ZMK_INPUT_PROC_STOP);
    assert(touch(NULL, false) == ZMK_INPUT_PROC_STOP);
}

int main(void) {
    struct padstick_config config = {
        .x_deadzone = 48, .y_deadzone = 48, .x_scale = 8, .y_scale = 8,
        .x_accel_range = 464, .y_accel_range = 464, .x_accel_scale = 12, .y_accel_scale = 12,
        .max_x = 16, .max_y = 16, .x_center = 512, .y_center = 512, .suppress_btn0 = true};
    processor = (struct device){.config = &config, .data = &owner, .name = "padstick"};

    test_init_and_routing();
    test_contact_steps();
    test_partial_frames();
    test_inferred_contact();
    test_buttons();
    test_fixed_center_config(&config);

    /* A layer change drops every stream's contact, origin and pairing. */
    for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
        stream(i)->touch_active = true;
        stream(i)->input_dev = &pad;
        stream(i)->origin_x = stream(i)->origin_y = 100;
        stream(i)->btn0_press_suppressed = true;
        stream(i)->repeat_work.scheduled = true;
    }
    const zmk_event_t layer_changed = {1};
    assert(padstick_layer_listener(&layer_changed) == ZMK_EV_EVENT_BUBBLE);
    for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
        assert(!stream(i)->touch_active && stream(i)->input_dev == NULL);
        assert(stream(i)->origin_x == PADSTICK_COORD_UNSET && stream(i)->skip_origin_frame);
        assert(!stream(i)->btn0_press_suppressed && !stream(i)->repeat_work.scheduled);
    }

    for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
        assert(stream(i)->lock.depth == 0);
    }
    puts("padstick driver: PASS");
    return 0;
}
