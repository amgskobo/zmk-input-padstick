/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct { int64_t ms; } k_timeout_t;
struct k_mutex { int depth; };
struct k_spinlock { int depth; };
typedef int k_spinlock_key_t;
static __attribute__((unused)) k_spinlock_key_t k_spin_lock(struct k_spinlock *lock) { assert(lock->depth++ == 0); return 0; }
static __attribute__((unused)) void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key) { (void)key; assert(--lock->depth == 0); }
struct k_work { void (*handler)(struct k_work *work); };
struct k_work_delayable { struct k_work work; bool scheduled; int64_t delay_ms; };
struct k_work_q { int unused; };
struct device { const void *config; void *data; const char *name; const void *api; };
struct input_event { const struct device *dev; uint8_t sync; uint8_t type; uint16_t code;
                     int32_t value; };
struct zmk_input_processor_state { uint8_t input_device_index; };

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
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
#define INPUT_MSC_SCAN 4
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

#ifndef TEST_LISTENER_COUNT
#define TEST_LISTENER_COUNT 2
#endif
#define DT_NUM_INST_STATUS_OKAY(c) TEST_LISTENER_COUNT
/* Direct-call fixture; real generated-DTS route guards run separately. */
#define DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(...)
#define ZMK_EV_EVENT_BUBBLE 0
#define DT_INST_FOREACH_STATUS_OKAY(fn) fn(0)
#define DT_INST_PROP_OR(n,key,fallback) fallback
#define BUILD_ASSERT(condition,message) _Static_assert(condition,message)
#define DEVICE_DT_INST_GET(n) (&processor)
#define DEVICE_DT_INST_DEFINE(n,init,pm,storage,settings,level,priority,driver) \
    static struct device processor={.data=storage,.config=settings,.api=driver};
#define LOG_MODULE_REGISTER(...)
#define ZMK_LISTENER(name,callback) static int (*name)(const zmk_event_t *) __attribute__((used))=callback
#define ZMK_SUBSCRIPTION(...)
#define INPUT_CALLBACK_DEFINE_NAMED(dev, callback, user, name) \
    static void (*raw_repeat_ack)(struct input_event *, void *) = callback
struct zmk_input_processor_driver_api {
 int (*handle_event)(const struct device *,struct input_event *,uint32_t,uint32_t,
                     struct zmk_input_processor_state *);
};
static struct device processor;
static bool device_is_ready(const struct device *dev) { return dev->data != NULL; }
typedef struct { int kind; } zmk_event_t;

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
static void (*during_repeat_report)(uint16_t code);
/* Bounded input API model: accepted events remain ordered until explicitly drained. */
static bool fifo_enabled;
static struct input_event fifo[8];
static unsigned fifo_count, accepted_x, accepted_y;
static int32_t refused_y;

static void (*raw_repeat_ack)(struct input_event *, void *);
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
    if (fifo_enabled) {
        if (fifo_count == ARRAY_SIZE(fifo)) {
            if(code==INPUT_REL_Y) { refused_y=value; }
            return -11;
        }
        fifo[fifo_count++] = (struct input_event){.dev=dev,.type=INPUT_EV_REL,
            .code=code,.value=value,.sync=sync};
        if (code == INPUT_REL_X) { accepted_x++; } else { accepted_y++; }
        return 0;
    }
    reported[code] = value;
    reported_sync[code] = sync;
    reports++;
    if (report_result == 0 && listener_takes_pair) {
        struct input_event event = {.dev = dev, .sync = sync, .type = INPUT_EV_REL, .code = code,
                                    .value = value};
        if (raw_repeat_ack) { raw_repeat_ack(&event, NULL); }
        assert(padstick_handle_event(&processor, &event, 0, 0, NULL) == 0);
        if(during_repeat_report) { during_repeat_report(code); }
    }
    return report_result;
}

#ifndef PADSTICK_DRIVER_SOURCE
#define PADSTICK_DRIVER_SOURCE "../../src/input_processor_padstick.c"
#endif
#include PADSTICK_DRIVER_SOURCE

