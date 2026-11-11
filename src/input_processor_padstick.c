/*
 * Copyright (c) 2026 amgskobo
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_padstick

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zmk-input-padstick/runtime.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/input_processor.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/workqueue.h>
#include "padstick_math.h"

#ifndef CONFIG_ZMK_INPUT_PADSTICK_LOG_LEVEL
#define CONFIG_ZMK_INPUT_PADSTICK_LOG_LEVEL 3
#endif
LOG_MODULE_REGISTER(zmk_input_padstick_input_processor_padstick, CONFIG_ZMK_INPUT_PADSTICK_LOG_LEVEL);

#define PADSTICK_COORD_UNSET INT32_MIN
#include "padstick_sources.h"
/*
 * A padstick is a velocity control: holding a deflection keeps emitting its
 * REL step. Keep the cadence here, rather than asking an absolute-coordinate
 * source driver to resend identical samples. This is intentionally not a DTS
 * property; it is part of the processor's fixed output semantics.
 */
#define PADSTICK_REPEAT_INTERVAL_MS 20

struct padstick_data;

struct padstick_stream {
	/* The input callback and the repeat work both update the coordinate state. */
	struct k_work_delayable repeat_work;
	struct padstick_data *owner;
	const struct device *input_dev;
	/*
	 * The input callback, repeat worker and layer listener all hold lock while
	 * changing this contact state. X and Y still arrive as separate events, so a
	 * pair may naturally contain one coordinate from the preceding report; the
	 * stored partner is intentionally used for that one event.
	 */
	int32_t origin_x;
	int32_t origin_y;
	int32_t x_remainder;
	int32_t y_remainder;
	/*
	 * Last raw value seen on each axis, so the other one is available when a
	 * single axis event is being turned into a step. They arrive separately,
	 * so the first of a pair reads its partner one sample behind - far below
	 * what a finger covers between reports.
	 */
	int32_t last_x;
	int32_t last_y;
	/*
	 * Set while a repeat pair has been admitted and its raw Y has not drained. The
	 * input queue is small and does not block: while the listener is held up -
	 * a congested host link can take 100 ms over one report - a repeat every
	 * 20 ms fills it, and whatever arrives next is dropped, the pad's own
	 * release or the one ZMK reports for a split peripheral that disconnected
	 * included. The repeat then outlives the contact. One pair at a time keeps
	 * the stick at the pace the listener really goes.
	 */
	bool repeat_in_flight;
	/* X was admitted; retry only the missing synchronized Y before any new pair.
	 * This completion outlives contact/settings resets, like already queued input. */
	bool repeat_y_pending;
	int32_t repeat_y;
	const struct device *repeat_dev;
	bool touch_active;
	/*
	 * A layer can switch this processor in while a contact is already down, so
	 * it may not see that contact's BTN_TOUCH press. Two complete coordinate
	 * frames are enough to distinguish a continuing contact from the one final
	 * coordinate pair that normally follows BTN_TOUCH release.
	 */
	uint8_t unsignaled_contact_frames;
	bool skip_origin_frame;
};

struct padstick_data {
	/* One lock serializes configuration, events and repeats across this node. */
	struct k_mutex lock;
	struct padstick_params params;
	struct padstick_stream streams[PADSTICK_STREAM_COUNT];
};

/* Input callbacks can run synchronously, including from an ISR on unrelated
 * sources. Protect only the bounded admission metadata with a spinlock; never
 * hold it while reporting input or taking a node mutex. */
static struct k_spinlock padstick_repeat_lock;
static bool padstick_admit_repeat(struct padstick_stream *data);
static void padstick_retire_repeat(struct padstick_stream *data);

static void padstick_reset_contact(struct padstick_stream *data) {
	data->origin_x = PADSTICK_COORD_UNSET;
	data->origin_y = PADSTICK_COORD_UNSET;
	data->x_remainder = 0;
	data->y_remainder = 0;
	data->skip_origin_frame = true;
	data->last_x = PADSTICK_COORD_UNSET;
	data->last_y = PADSTICK_COORD_UNSET;
	data->unsignaled_contact_frames = 0;
	/* Queue ownership outlives contact, layer and settings resets. */
}

/*
 * Seed both origins at once.
 *
 * With a fixed center neither origin depends on the contact, so both are known
 * before any sample arrives. Seeding only the axis whose event is in hand would
 * leave the other unset for the rest of that frame, and a radial reading needs
 * both to measure the distance from the center: the axis that arrives first
 * would fall back to its own displacement alone and come out short on anything
 * but a straight push.
 *
 * Both are unset whenever this runs: a contact reset clears them together and
 * this is the only thing that sets them while the center is fixed.
 */
static void padstick_seed_fixed_origin(struct padstick_stream *data,
				       const struct padstick_params *config) {
	data->origin_x = config->x_center;
	data->origin_y = config->y_center;
}

/* Called with data->lock held. A repeat is valid only after this instance has
 * observed BTN_TOUCH or confirmed a continuing contact from complete frames. */
static bool padstick_repeat_ready(const struct padstick_stream *data) {
	return data->touch_active && data->input_dev != NULL && !data->skip_origin_frame &&
	       data->origin_x != PADSTICK_COORD_UNSET && data->origin_y != PADSTICK_COORD_UNSET &&
	       data->last_x != PADSTICK_COORD_UNSET && data->last_y != PADSTICK_COORD_UNSET;
}

/* Called with data->lock held. */
static void padstick_schedule_repeat(struct padstick_stream *data) {
	if (data->repeat_y_pending || padstick_repeat_ready(data)) {
		(void)k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(),
						 &data->repeat_work,
						 K_MSEC(PADSTICK_REPEAT_INTERVAL_MS));
	}
}

/* Called with data->lock held at the end of a converted ABS report frame. */
static void padstick_note_unsignaled_contact_frame(struct input_event *event,
								     struct padstick_stream *data) {
	if (data->touch_active || !event->sync) {
		return;
	}

	/* Every path that clears touch_active also zeroes this count, and the
	 * second frame sets touch_active, so it never passes two. */
	if (++data->unsignaled_contact_frames < 2U) {
		return;
	}

	data->touch_active = true;
	data->input_dev = event->dev;
	LOG_DBG("inferred continuing touch after layer switch");
	padstick_schedule_repeat(data);
}

/* Caller holds the node lock. Never enqueue another X while this Y is owed. */
static void padstick_complete_repeat(struct padstick_stream *data) {
	data->repeat_y_pending = false;
	int ret = input_report_rel(data->repeat_dev, INPUT_REL_Y, data->repeat_y, true, K_NO_WAIT);
	if (ret != 0) {
		data->repeat_y_pending = true;
		LOG_WRN("repeat REL_Y report failed: %d", ret);
	}
}

static void padstick_repeat_work_handler(struct k_work *work) {
	struct k_work_delayable *d_work = k_work_delayable_from_work(work);
	struct padstick_stream *data = CONTAINER_OF(d_work, struct padstick_stream, repeat_work);
	const struct padstick_params *config = &data->owner->params;

	k_mutex_lock(&data->owner->lock, K_FOREVER);

	if (data->repeat_y_pending) {
		padstick_complete_repeat(data);
		padstick_schedule_repeat(data);
		k_mutex_unlock(&data->owner->lock);
		return;
	}

	if (!padstick_repeat_ready(data)) {
		k_mutex_unlock(&data->owner->lock);
		return;
	}

	if (!padstick_admit_repeat(data)) {
		/* The last pair is still queued: wait for it rather than add to it. */
		padstick_schedule_repeat(data);
		k_mutex_unlock(&data->owner->lock);
		return;
	}

	const int32_t old_x_remainder = data->x_remainder;
	const int32_t old_y_remainder = data->y_remainder;
	int32_t delta_x = padstick_offset(data->last_x, data->origin_x);
	int32_t delta_y = padstick_offset(data->last_y, data->origin_y);
	int32_t reach = padstick_radial_distance(delta_x, delta_y);
	int32_t rel_x = padstick_apply_delta(delta_x, reach,
					     config->x_deadzone, config->x_scale,
					     config->x_accel_range, config->x_accel_scale,
					     config->max_x, config->invert_x,
					     &data->x_remainder);
	int32_t rel_y = padstick_apply_delta(delta_y, reach,
					     config->y_deadzone, config->y_scale,
					     config->y_accel_range, config->y_accel_scale,
					     config->max_y, config->invert_y,
					     &data->y_remainder);

	/*
	 * Return the synthesized REL pair to the original input device. Its input
	 * listener therefore applies the same remaining processors (for example
	 * inertia) as it does to a live trackpad event. Holding the lock makes a
	 * touch release or layer change serialize with admission. If only X fits,
	 * its missing Y completes even after reset, before any later repeat.
	 */
	if (rel_x != 0 || rel_y != 0) {
		const struct device *source = data->input_dev;
		int ret = input_report_rel(source, INPUT_REL_X, rel_x, false, K_NO_WAIT);
		if (ret != 0) {
			padstick_retire_repeat(data);
			LOG_WRN("repeat REL_X report failed: %d", ret);
		}

		if (ret == 0) {
			data->repeat_y = rel_y;
			data->repeat_y_pending = true;
			padstick_complete_repeat(data);
		}
	} else {
		padstick_retire_repeat(data);
	}
	/* A nonzero fractional step needs more ticks before it reaches one count.
	 * Stop at deadzone/zero gain even if an old fraction remains. */
	if (rel_x != 0 || rel_y != 0 || old_x_remainder != data->x_remainder ||
	    old_y_remainder != data->y_remainder) {
		padstick_schedule_repeat(data);
	}
	k_mutex_unlock(&data->owner->lock);
}

static void padstick_reference_only(struct input_event *event) {
	event->type = INPUT_EV_MSC;
	event->code = INPUT_MSC_SCAN;
	event->value = 0;
}

/*
 * Both edges of BTN_TOUCH start the contact over.
 *
 * A press begins a contact that bears no relation to the last one, and a
 * release ends one - and the trackpad emits a final absolute pair just after
 * the release, which the settle frame then absorbs instead of turning it into a
 * step from wherever the finger happened to leave the pad.
 *
 * Neither edge is treated as redundant. Which processors run is decided per
 * event from the layer active at that moment, so this instance may simply have
 * missed the release that ended the previous contact; skipping the reset then
 * would carry that contact's origin and remainders into the new one.
 */
static void padstick_handle_touch(struct input_event *event, struct padstick_stream *data) {
	k_work_cancel_delayable(&data->repeat_work);
	padstick_reset_contact(data);
	data->touch_active = event->value > 0;
	data->input_dev = data->touch_active ? event->dev : NULL;
	if (data->repeat_y_pending) { padstick_schedule_repeat(data); }
}

/* X and Y use the same reducer. Preserve event order: each axis sees the
 * most recent partner sample; only a converted synchronized Y confirms a frame. */
static void padstick_handle_abs_axis(struct input_event *event, struct padstick_stream *data,
									 const struct padstick_params *config) {
	if (event->code != INPUT_ABS_X && event->code != INPUT_ABS_Y) {
		return;
	}
	const bool x = event->code == INPUT_ABS_X;
	int32_t *last = x ? &data->last_x : &data->last_y;
	int32_t *origin = x ? &data->origin_x : &data->origin_y;
	int32_t *remainder = x ? &data->x_remainder : &data->y_remainder;
	*last = event->value;
	if (data->skip_origin_frame) {
		if (event->sync) {
			data->skip_origin_frame = false;
		}
		padstick_reference_only(event);
		return;
	}
	if (*origin == PADSTICK_COORD_UNSET) {
		if (config->fixed_center) {
			padstick_seed_fixed_origin(data, config);
		} else {
			*origin = event->value;
			padstick_reference_only(event);
			return;
		}
	}
	const int32_t other_last = x ? data->last_y : data->last_x;
	const int32_t other_origin = x ? data->origin_y : data->origin_x;
	const int32_t other = other_last != PADSTICK_COORD_UNSET && other_origin != PADSTICK_COORD_UNSET
							  ? padstick_offset(other_last, other_origin) : 0;
	event->type = INPUT_EV_REL;
	event->code = x ? INPUT_REL_X : INPUT_REL_Y;
	event->value = padstick_apply_axis(event->value, *origin,
		x ? config->x_deadzone : config->y_deadzone,
		x ? config->x_scale : config->y_scale,
		x ? config->x_accel_range : config->y_accel_range,
		x ? config->x_accel_scale : config->y_accel_scale,
		x ? config->max_x : config->max_y,
		x ? config->invert_x : config->invert_y, remainder, other);
	padstick_schedule_repeat(data);
	if (!x) {
		padstick_note_unsignaled_contact_frame(event, data);
	}
}

static int padstick_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
				 uint32_t param2, struct zmk_input_processor_state *state) {
	ARG_UNUSED(param1);
	ARG_UNUSED(param2);

	struct padstick_data *owner = dev->data;
	const struct padstick_params *config = &owner->params;
	struct padstick_stream *data;

	if (state == NULL) {
		data = &owner->streams[0];
/* Every uint8 source index is admitted by the explicit 256-slot bound. */
#if CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES != 256
	} else if (state->input_device_index >= PADSTICK_STREAM_COUNT) {
		/* No stream of its own: pass through rather than take stream zero's, and
		 * without a log line per event from a source that keeps sending. */
		return ZMK_INPUT_PROC_CONTINUE;
#endif
	} else {
		data = &owner->streams[state->input_device_index];
	}

	if ((event->type != INPUT_EV_KEY && event->type != INPUT_EV_ABS) ||
	    (event->type == INPUT_EV_KEY && event->code != INPUT_BTN_TOUCH)) {
		return ZMK_INPUT_PROC_CONTINUE;
	}

	k_mutex_lock(&data->owner->lock, K_FOREVER);

	if (event->type == INPUT_EV_ABS) {
		padstick_handle_abs_axis(event, data, config);
	} else {
		padstick_handle_touch(event, data);
	}

	k_mutex_unlock(&data->owner->lock);
	return ZMK_INPUT_PROC_CONTINUE;
}

static int padstick_init(const struct device *dev) {
	struct padstick_data *data = dev->data;
	const struct padstick_params *config = dev->config;

	/* The origin/last-coordinate state reserves this value as unset. The live
	 * setter already rejects it; boot configuration must obey the same rule. */
	if (config->x_center == PADSTICK_COORD_UNSET ||
	    config->y_center == PADSTICK_COORD_UNSET) {
		return -EINVAL;
	}

	k_mutex_init(&data->lock);
	data->params = *config;
	for (size_t i = 0U; i < PADSTICK_STREAM_COUNT; i++) {
		struct padstick_stream *stream = &data->streams[i];

		k_work_init_delayable(&stream->repeat_work, padstick_repeat_work_handler);
		stream->owner = data;
		stream->input_dev = NULL;
		stream->touch_active = false;
		stream->repeat_y_pending = false;
		stream->repeat_in_flight = false;
		stream->repeat_dev = NULL;
		padstick_reset_contact(stream);
	}
	LOG_DBG("init x: dz=%d scale=%d accel_range=%d accel_scale=%d max=%d invert=%d",
		config->x_deadzone, config->x_scale, config->x_accel_range,
		config->x_accel_scale, config->max_x, config->invert_x);
	LOG_DBG("init y: dz=%d scale=%d accel_range=%d accel_scale=%d max=%d invert=%d",
		config->y_deadzone, config->y_scale, config->y_accel_range,
		config->y_accel_scale, config->max_y, config->invert_y);
	LOG_DBG("init center: fixed=%d x=%d y=%d", config->fixed_center, config->x_center,
		config->y_center);

	return 0;
}

static const struct zmk_input_processor_driver_api padstick_driver_api = {
	.handle_event = padstick_handle_event,
};

static bool padstick_device_valid(const struct device *dev) {
    return dev != NULL && dev->api == &padstick_driver_api && device_is_ready(dev);
}

int padstick_get_params(const struct device *dev, struct padstick_params *out) {
    if (out == NULL) { return -EINVAL; }
    if (!padstick_device_valid(dev)) { return -ENODEV; }
    struct padstick_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    *out = data->params;
    k_mutex_unlock(&data->lock);
    return 0;
}

int padstick_set_params(const struct device *dev, const struct padstick_params *params) {
    if (params == NULL) { return -EINVAL; }
    if (!padstick_device_valid(dev)) { return -ENODEV; }
#define PADSTICK_VALIDATE(n, field, lo, hi) \
    if (params->field < (lo) || params->field > (hi)) { return -EINVAL; }
    PADSTICK_INT_FIELDS(PADSTICK_VALIDATE, 0)
#undef PADSTICK_VALIDATE
    struct padstick_data *data = dev->data;
    k_mutex_lock(&data->lock, K_FOREVER);
    bool same = true;
#define PADSTICK_SAME_INT(n, field, lo, hi) same &= data->params.field == params->field;
#define PADSTICK_SAME_BOOL(n, field) same &= data->params.field == params->field;
    PADSTICK_INT_FIELDS(PADSTICK_SAME_INT, 0)
    PADSTICK_BOOL_FIELDS(PADSTICK_SAME_BOOL, 0)
#undef PADSTICK_SAME_INT
#undef PADSTICK_SAME_BOOL
    if (!same) {
        data->params = *params;
        for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
            struct padstick_stream *stream = &data->streams[i];
            k_work_cancel_delayable(&stream->repeat_work);
            padstick_reset_contact(stream);
            if (stream->repeat_y_pending) { padstick_schedule_repeat(stream); }
        }
    }
    k_mutex_unlock(&data->lock);
    return 0;
}

#define PADSTICK_INST(n)                                                                         \
	BUILD_ASSERT((int32_t)DT_INST_PROP_OR(n, x_center, 512) != PADSTICK_COORD_UNSET,            \
		     "padstick x-center must not be the unset coordinate");                            \
	BUILD_ASSERT((int32_t)DT_INST_PROP_OR(n, y_center, 512) != PADSTICK_COORD_UNSET,            \
		     "padstick y-center must not be the unset coordinate");                            \
	static struct padstick_data padstick_data_##n;                                           \
	static const struct padstick_params padstick_config_##n = {                              \
		.x_deadzone = DT_INST_PROP_OR(n, x_deadzone, 48),                                \
		.y_deadzone = DT_INST_PROP_OR(n, y_deadzone, 48),                                \
		.x_scale = DT_INST_PROP_OR(n, x_scale, 8),                                       \
		.y_scale = DT_INST_PROP_OR(n, y_scale, 8),                                       \
		.x_accel_range = DT_INST_PROP_OR(n, x_accel_range, 464),                         \
		.y_accel_range = DT_INST_PROP_OR(n, y_accel_range, 464),                         \
		.x_accel_scale = DT_INST_PROP_OR(n, x_accel_scale, 12),                          \
		.y_accel_scale = DT_INST_PROP_OR(n, y_accel_scale, 12),                          \
		.max_x = DT_INST_PROP_OR(n, max_x, 16),                                          \
		.max_y = DT_INST_PROP_OR(n, max_y, 16),                                          \
		.x_center = DT_INST_PROP_OR(n, x_center, 512),                                   \
		.y_center = DT_INST_PROP_OR(n, y_center, 512),                                   \
		.invert_x = DT_INST_PROP_OR(n, invert_x, false),                                 \
		.invert_y = DT_INST_PROP_OR(n, invert_y, false),                                 \
		.fixed_center = DT_INST_PROP_OR(n, fixed_center, false),                         \
	};                                                                                        \
	DEVICE_DT_INST_DEFINE(n, padstick_init, NULL, &padstick_data_##n, &padstick_config_##n,  \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                     \
			      &padstick_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PADSTICK_INST)

#define PADSTICK_DEVICE(n) DEVICE_DT_INST_GET(n),
static const struct device *const padstick_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(PADSTICK_DEVICE)
};

static bool padstick_admit_repeat(struct padstick_stream *data) {
    const k_spinlock_key_t key = k_spin_lock(&padstick_repeat_lock);
    /* Several instances/listeners can select the same absolute source across
     * a route change. Its old pair must drain before any of them sends X. */
    for (size_t n = 0; n < ARRAY_SIZE(padstick_devices); n++) {
        struct padstick_data *owner = padstick_devices[n]->data;
        for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
            const struct padstick_stream *other = &owner->streams[i];
            if (other->repeat_in_flight && other->repeat_dev == data->input_dev) {
                k_spin_unlock(&padstick_repeat_lock, key);
                return false;
            }
        }
    }
    data->repeat_dev = data->input_dev;
    data->repeat_in_flight = true;
    k_spin_unlock(&padstick_repeat_lock, key);
    return true;
}

static void padstick_retire_repeat(struct padstick_stream *data) {
    const k_spinlock_key_t key = k_spin_lock(&padstick_repeat_lock);
    data->repeat_in_flight = false;
    k_spin_unlock(&padstick_repeat_lock, key);
}

static void padstick_repeat_ack(struct input_event *event, void *unused) {
    ARG_UNUSED(unused);
    if (event->type != INPUT_EV_REL || event->code != INPUT_REL_Y || !event->sync) {
        return;
    }
    const k_spinlock_key_t key = k_spin_lock(&padstick_repeat_lock);
    for (size_t n = 0; n < ARRAY_SIZE(padstick_devices); n++) {
        struct padstick_data *owner = padstick_devices[n]->data;
        for (size_t i = 0; i < PADSTICK_STREAM_COUNT; i++) {
            struct padstick_stream *data = &owner->streams[i];
            if (data->repeat_in_flight && data->repeat_dev == event->dev) {
                data->repeat_in_flight = false;
            }
        }
    }
    k_spin_unlock(&padstick_repeat_lock, key);
}
/* Zephyr orders input callback sections by name. Observe the original Y before
 * ZMK's input_handler_* callbacks transform it, even when PadStick is bypassed.
 * Only this observer retires queued pairs; a later route cannot acknowledge a
 * newer pair a second time. */
INPUT_CALLBACK_DEFINE_NAMED(NULL, padstick_repeat_ack, NULL, a_padstick_repeat_ack);

/**
 * Drop the origin and stop repeat output when the layer changes.
 *
 * Which processors run is decided per event from the layer active at that
 * moment, so one contact can be split across two instances of this processor.
 * Without fixed-center the instance the contact moves to still holds an origin
 * taken from an earlier touch, and the first sample it sees is turned into the
 * distance between two unrelated contacts.
 *
 * Dropping the origin costs the settle frame spent re-establishing it, the same
 * frame every contact already spends when it starts. This reset clears motion
 * state only; the common final filter owns button disposition. A route that
 * missed BTN_TOUCH can infer a continuing contact from later complete frames.
 */
#define PADSTICK_RESYNC(n)                                                           \
	{                                                                            \
		struct padstick_data *owner = DEVICE_DT_INST_GET(n)->data;           \
		for (size_t i = 0U; i < PADSTICK_STREAM_COUNT; i++) {                \
			struct padstick_stream *data = &owner->streams[i];              \
			k_mutex_lock(&data->owner->lock, K_FOREVER);                         \
			k_work_cancel_delayable(&data->repeat_work);                  \
			data->touch_active = false;                                   \
			data->input_dev = NULL;                                       \
			padstick_reset_contact(data);                                 \
			if (data->repeat_y_pending) { padstick_schedule_repeat(data); } \
			k_mutex_unlock(&data->owner->lock);                                  \
		}                                                                    \
	}

static int padstick_layer_listener(const zmk_event_t *eh) {
	ARG_UNUSED(eh);

	DT_INST_FOREACH_STATUS_OKAY(PADSTICK_RESYNC)

	return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(padstick_layer, padstick_layer_listener);
ZMK_SUBSCRIPTION(padstick_layer, zmk_layer_state_changed);
