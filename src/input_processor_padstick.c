/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_input_processor_padstick

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <drivers/input_processor.h>

LOG_MODULE_REGISTER(input_processor_padstick, CONFIG_ZMK_LOG_LEVEL);

#define PADSTICK_COORD_UNSET INT32_MIN
#define PADSTICK_DISTANCE_MAX 32767
#define PADSTICK_INVALID_CODE 0xFFF
#define PADSTICK_SCALE_MAX 4096
#define PADSTICK_SCALE_SHIFT 8
/* Keep signed so negative fixed-point totals divide as negative values. */
#define PADSTICK_SCALE_ONE ((int32_t)1 << PADSTICK_SCALE_SHIFT)

struct padstick_config {
	int32_t x_deadzone;
	int32_t y_deadzone;
	int32_t x_scale;
	int32_t y_scale;
	int32_t x_accel_range;
	int32_t y_accel_range;
	int32_t x_accel_scale;
	int32_t y_accel_scale;
	int32_t max_x;
	int32_t max_y;
	int32_t x_center;
	int32_t y_center;
	bool invert_x;
	bool invert_y;
	bool fixed_center;
	bool suppress_abs;
	bool suppress_btn_touch;
	bool suppress_btn0;
};

struct padstick_data {
	int32_t origin_x;
	int32_t origin_y;
	int32_t x_remainder;
	int32_t y_remainder;
	bool touching;
	bool skip_origin_frame;
};

static void padstick_reset_contact(struct padstick_data *data) {
	data->origin_x = PADSTICK_COORD_UNSET;
	data->origin_y = PADSTICK_COORD_UNSET;
	data->x_remainder = 0;
	data->y_remainder = 0;
	data->skip_origin_frame = true;
}

static int32_t padstick_abs_i32(int32_t value) {
	if (value == INT32_MIN) {
		return INT32_MAX;
	}

	return value < 0 ? -value : value;
}

static int32_t padstick_clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
	return MIN(MAX(value, min_value), max_value);
}

static int32_t padstick_scale_distance(int32_t distance, int32_t accel_range, int32_t scale,
				       int32_t accel_scale) {
	accel_range = MAX(accel_range, 0);
	scale = padstick_clamp_i32(scale, 0, PADSTICK_SCALE_MAX);
	accel_scale = padstick_clamp_i32(accel_scale, 0, PADSTICK_SCALE_MAX);

	if (accel_range == 0 || accel_scale <= scale) {
		return distance * scale;
	}

	int32_t ramp_distance = MIN(distance, accel_range);
	int32_t fast_distance = distance - ramp_distance;
	int32_t scale_span = accel_scale - scale;
	int32_t ramp_scale = accel_scale;

	if (accel_range > 1) {
		int32_t ramp_ratio = ((ramp_distance - 1) << PADSTICK_SCALE_SHIFT) / (accel_range - 1);
		ramp_scale = scale + ((scale_span * ramp_ratio) >> PADSTICK_SCALE_SHIFT);
	}

	int32_t ramp_scaled = (ramp_distance * (scale + ramp_scale)) / 2;

	return ramp_scaled + (fast_distance * accel_scale);
}

static int32_t padstick_apply_axis(char axis, int32_t value, int32_t origin, int32_t deadzone,
				   int32_t scale, int32_t accel_range,
				   int32_t accel_scale, int32_t max_value, bool invert,
				   int32_t *remainder) {
	int32_t delta = value - origin;
	int32_t magnitude = MIN(padstick_abs_i32(delta), PADSTICK_DISTANCE_MAX);
	int32_t remainder_in = *remainder;

	ARG_UNUSED(remainder_in);

	deadzone = MAX(deadzone, 0);
	max_value = MAX(max_value, 0);

	if (magnitude <= deadzone) {
		*remainder = 0;
		LOG_DBG("%c abs=%d origin=%d delta=%d mag=%d deadzone=%d -> rel=0 rem=0",
			axis, value, origin, delta, magnitude, deadzone);
		return 0;
	}

	magnitude -= deadzone;

	int32_t scaled = padstick_scale_distance(magnitude, accel_range, scale, accel_scale);
	if ((delta < 0) != invert) {
		scaled = -scaled;
	}

	int32_t total = scaled + *remainder;
	int32_t output = total / PADSTICK_SCALE_ONE;

	if (output > max_value) {
		*remainder = 0;
		LOG_DBG("%c abs=%d origin=%d delta=%d mag=%d scaled=%d rem_in=%d total=%d -> "
			"rel=%d saturated rem=0",
			axis, value, origin, delta, magnitude, scaled, remainder_in, total,
			max_value);
		return max_value;
	}

	if (output < -max_value) {
		*remainder = 0;
		LOG_DBG("%c abs=%d origin=%d delta=%d mag=%d scaled=%d rem_in=%d total=%d -> "
			"rel=%d saturated rem=0",
			axis, value, origin, delta, magnitude, scaled, remainder_in, total,
			-max_value);
		return -max_value;
	}

	*remainder = total - (output * PADSTICK_SCALE_ONE);
	LOG_DBG("%c abs=%d origin=%d delta=%d mag=%d scaled=%d rem_in=%d total=%d -> "
		"rel=%d rem=%d",
		axis, value, origin, delta, magnitude, scaled, remainder_in, total, output,
		*remainder);

	return output;
}

static int padstick_suppress_event(struct input_event *event) {
	uint16_t type = event->type;
	uint16_t code = event->code;
	int32_t value = event->value;

	ARG_UNUSED(type);
	ARG_UNUSED(code);
	ARG_UNUSED(value);

	LOG_DBG("suppress type=%u code=%u value=%d", type, code, value);

	event->code = PADSTICK_INVALID_CODE;
	event->sync = false;
	return ZMK_INPUT_PROC_STOP;
}

static int padstick_handle_touch(struct input_event *event, struct padstick_data *data,
				 const struct padstick_config *config) {
	bool touching = event->value != 0;

	if (touching != data->touching) {
		data->touching = touching;
		padstick_reset_contact(data);
		LOG_DBG("touch=%d reset origin/rem", data->touching);
	} else {
		LOG_DBG("touch=%d unchanged", data->touching);
	}

	if (config->suppress_btn_touch) {
		return padstick_suppress_event(event);
	}

	return ZMK_INPUT_PROC_CONTINUE;
}

static int padstick_handle_abs_axis(struct input_event *event, struct padstick_data *data,
				    const struct padstick_config *config) {
	if (!data->touching) {
		return config->suppress_abs ? padstick_suppress_event(event) : ZMK_INPUT_PROC_CONTINUE;
	}

	if (event->code == INPUT_ABS_X) {
		if (data->skip_origin_frame) {
			LOG_DBG("x skip origin-settle abs=%d sync=%d", event->value, event->sync);
			if (event->sync) {
				data->skip_origin_frame = false;
			}
			return padstick_suppress_event(event);
		}

		if (data->origin_x == PADSTICK_COORD_UNSET) {
			if (config->fixed_center) {
				data->origin_x = config->x_center;
				LOG_DBG("x fixed origin=%d", data->origin_x);
			} else {
				data->origin_x = event->value;
				LOG_DBG("x origin=%d", data->origin_x);
				return padstick_suppress_event(event);
			}
		}

		event->type = INPUT_EV_REL;
		event->code = INPUT_REL_X;
		event->value = padstick_apply_axis('x', event->value, data->origin_x,
						   config->x_deadzone,
						   config->x_scale, config->x_accel_range,
						   config->x_accel_scale, config->max_x,
						   config->invert_x, &data->x_remainder);
		return ZMK_INPUT_PROC_CONTINUE;
	}

	if (event->code == INPUT_ABS_Y) {
		if (data->skip_origin_frame) {
			LOG_DBG("y skip origin-settle abs=%d sync=%d", event->value, event->sync);
			if (event->sync) {
				data->skip_origin_frame = false;
			}
			return padstick_suppress_event(event);
		}

		if (data->origin_y == PADSTICK_COORD_UNSET) {
			if (config->fixed_center) {
				data->origin_y = config->y_center;
				LOG_DBG("y fixed origin=%d", data->origin_y);
			} else {
				data->origin_y = event->value;
				LOG_DBG("y origin=%d", data->origin_y);
				return padstick_suppress_event(event);
			}
		}

		event->type = INPUT_EV_REL;
		event->code = INPUT_REL_Y;
		event->value = padstick_apply_axis('y', event->value, data->origin_y,
						   config->y_deadzone,
						   config->y_scale, config->y_accel_range,
						   config->y_accel_scale, config->max_y,
						   config->invert_y, &data->y_remainder);
		return ZMK_INPUT_PROC_CONTINUE;
	}

	return config->suppress_abs ? padstick_suppress_event(event) : ZMK_INPUT_PROC_CONTINUE;
}

static int padstick_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
				 uint32_t param2, struct zmk_input_processor_state *state) {
	ARG_UNUSED(param1);
	ARG_UNUSED(param2);
	ARG_UNUSED(state);

	const struct padstick_config *config = dev->config;
	struct padstick_data *data = dev->data;

	if (event->type == INPUT_EV_KEY) {
		if (event->code == INPUT_BTN_TOUCH) {
			return padstick_handle_touch(event, data, config);
		}

		if (event->code == INPUT_BTN_0 && config->suppress_btn0) {
			return padstick_suppress_event(event);
		}

		return ZMK_INPUT_PROC_CONTINUE;
	}

	if (event->type == INPUT_EV_ABS) {
		return padstick_handle_abs_axis(event, data, config);
	}

	return ZMK_INPUT_PROC_CONTINUE;
}

static int padstick_init(const struct device *dev) {
	struct padstick_data *data = dev->data;
	const struct padstick_config *config = dev->config;

	data->touching = false;
	padstick_reset_contact(data);
	LOG_DBG("init x: dz=%d scale=%d accel_range=%d accel_scale=%d max=%d invert=%d",
		config->x_deadzone, config->x_scale, config->x_accel_range,
		config->x_accel_scale, config->max_x, config->invert_x);
	LOG_DBG("init y: dz=%d scale=%d accel_range=%d accel_scale=%d max=%d invert=%d",
		config->y_deadzone, config->y_scale, config->y_accel_range,
		config->y_accel_scale, config->max_y, config->invert_y);
	LOG_DBG("init center: fixed=%d x=%d y=%d", config->fixed_center, config->x_center,
		config->y_center);
	LOG_DBG("init suppress: abs=%d btn_touch=%d btn0=%d", config->suppress_abs,
		config->suppress_btn_touch, config->suppress_btn0);

	return 0;
}

static const struct zmk_input_processor_driver_api padstick_driver_api = {
	.handle_event = padstick_handle_event,
};

#define PADSTICK_INST(n)                                                                         \
	static struct padstick_data padstick_data_##n;                                           \
	static const struct padstick_config padstick_config_##n = {                              \
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
		.suppress_abs = DT_INST_PROP_OR(n, suppress_abs, false),                         \
		.suppress_btn_touch = DT_INST_PROP_OR(n, suppress_btn_touch, false),             \
		.suppress_btn0 = DT_INST_PROP_OR(n, suppress_btn0, false),                       \
	};                                                                                        \
	DEVICE_DT_INST_DEFINE(n, padstick_init, NULL, &padstick_data_##n, &padstick_config_##n,  \
			      POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                     \
			      &padstick_driver_api);

DT_INST_FOREACH_STATUS_OKAY(PADSTICK_INST)
