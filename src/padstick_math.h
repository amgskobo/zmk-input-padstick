/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#define PADSTICK_MIN(a, b) ((a) < (b) ? (a) : (b))
#define PADSTICK_MAX(a, b) ((a) > (b) ? (a) : (b))
#define PADSTICK_DISTANCE_MAX 32767
#define PADSTICK_SCALE_MAX 4096
#define PADSTICK_SCALE_SHIFT 8
/* Signed fixed-point division preserves negative remainders. */
#define PADSTICK_SCALE_ONE ((int32_t)1 << PADSTICK_SCALE_SHIFT)

static int32_t padstick_abs_i32(int32_t value) {
	if (value == INT32_MIN) {
		return INT32_MAX;
	}

	return value < 0 ? -value : value;
}

static int32_t padstick_clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
	return PADSTICK_MIN(PADSTICK_MAX(value, min_value), max_value);
}

/*
 * How far a coordinate is from its origin. Both are whatever the input device
 * reported, so the difference is taken wide and saturated rather than left to
 * overflow when a source sends a value near the ends of the range.
 */
static int32_t padstick_offset(int32_t value, int32_t origin) {
	const int64_t offset = (int64_t)value - origin;

	if (offset > INT32_MAX) {
		return INT32_MAX;
	}
	if (offset < INT32_MIN) {
		return INT32_MIN;
	}
	return (int32_t)offset;
}

/*
 * Integer square root, bit by bit. Sixteen rounds of shifts and compares, with
 * no divide or multiply, which is what keeps a radial reading affordable at
 * report rate. Exact for every input a pad of this size can produce.
 */
static uint32_t padstick_isqrt(uint32_t v) {
	uint32_t rem = 0;
	uint32_t root = 0;

	for (int i = 0; i < 16; i++) {
		root <<= 1;
		rem = (rem << 2) | (v >> 30);
		v <<= 2;
		if (root < rem) {
			root++;
			rem -= root;
			root++;
		}
	}

	return root >> 1;
}

static int32_t padstick_scale_distance(int32_t distance, int32_t accel_range, int32_t scale,
				       int32_t accel_scale) {
	accel_range = PADSTICK_MAX(accel_range, 0);
	scale = padstick_clamp_i32(scale, 0, PADSTICK_SCALE_MAX);
	accel_scale = padstick_clamp_i32(accel_scale, 0, PADSTICK_SCALE_MAX);

	if (accel_range == 0 || accel_scale <= scale) {
		return distance * scale;
	}

	int32_t ramp_distance = PADSTICK_MIN(distance, accel_range);
	int32_t fast_distance = distance - ramp_distance;
	int32_t scale_span = accel_scale - scale;
	int32_t ramp_scale = accel_scale;

	if (accel_range > 1) {
		/* Multiplied rather than shifted: a distance of zero makes the
		 * factor -1, and shifting a negative value is undefined. */
		int32_t ramp_ratio = ((ramp_distance - 1) * PADSTICK_SCALE_ONE) / (accel_range - 1);
		ramp_scale = scale + ((scale_span * ramp_ratio) >> PADSTICK_SCALE_SHIFT);
	}

	int32_t ramp_scaled = (ramp_distance * (scale + ramp_scale)) / 2;

	return ramp_scaled + (fast_distance * accel_scale);
}

/*
 * Turn one axis of a contact into a step.
 *
 * The distance from the origin is taken first and the ramp applied to that,
 * then the step is shared out along the two axes. Speed therefore depends only
 * on how far the finger has moved and not on which way it was pushed, and the
 * deadzone is a circle.
 *
 * Reading each axis on its own would make the deadzone a square, and a ramp
 * that rises with distance yields less when that distance is split between two
 * axes than when it all lands on one - so a diagonal push would come out slower
 * than a straight one, by a margin that grows with deflection.
 *
 * The distance costs one integer square root and the share one divide, per axis
 * event. Against the interval between reports that is not a meaningful fraction.
 */
static int32_t padstick_apply_axis(int32_t value, int32_t origin, int32_t deadzone,
				   int32_t scale, int32_t accel_range,
				   int32_t accel_scale, int32_t max_value, bool invert,
				   int32_t *remainder, int32_t other_delta) {
	int32_t delta = padstick_offset(value, origin);
	int32_t axis_distance = PADSTICK_MIN(padstick_abs_i32(delta), PADSTICK_DISTANCE_MAX);
	uint32_t a = (uint32_t)axis_distance;
	uint32_t b = (uint32_t)PADSTICK_MIN(padstick_abs_i32(other_delta), PADSTICK_DISTANCE_MAX);
	int32_t magnitude = (int32_t)PADSTICK_MIN(padstick_isqrt(a * a + b * b),
					 (uint32_t)PADSTICK_DISTANCE_MAX);
	int32_t reach = magnitude;

	deadzone = PADSTICK_MAX(deadzone, 0);
	max_value = PADSTICK_MAX(max_value, 0);

	if (magnitude <= deadzone) {
		*remainder = 0;
		return 0;
	}

	magnitude -= deadzone;

	int32_t scaled = padstick_scale_distance(magnitude, accel_range, scale, accel_scale);

	/* The nonpositive case returned above: deadzone >= 0 and reach > deadzone,
	 * so this divisor cannot be zero. Share the step across both axes. */
	scaled = (int32_t)(((int64_t)scaled * (int64_t)axis_distance) / (int64_t)reach);

	if ((delta < 0) != invert) {
		scaled = -scaled;
	}

	int32_t total = scaled + *remainder;
	int32_t output = total / PADSTICK_SCALE_ONE;

	if (output > max_value) {
		*remainder = 0;
		return max_value;
	}

	if (output < -max_value) {
		*remainder = 0;
		return -max_value;
	}

	*remainder = total - (output * PADSTICK_SCALE_ONE);
	return output;
}
