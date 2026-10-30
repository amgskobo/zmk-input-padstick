/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct device;

/* Scale is fixed point with 256 == 1.0. Centers exclude the unset sentinel. */
#define PADSTICK_INT_FIELDS(F, n) \
    F(n, x_deadzone, 0, 32767) \
    F(n, y_deadzone, 0, 32767) \
    F(n, x_scale, 0, 4096) \
    F(n, y_scale, 0, 4096) \
    F(n, x_accel_range, 0, 32767) \
    F(n, y_accel_range, 0, 32767) \
    F(n, x_accel_scale, 0, 4096) \
    F(n, y_accel_scale, 0, 4096) \
    F(n, max_x, 0, 32767) \
    F(n, max_y, 0, 32767) \
    F(n, x_center, (INT32_MIN + 1), INT32_MAX) \
    F(n, y_center, (INT32_MIN + 1), INT32_MAX)
#define PADSTICK_BOOL_FIELDS(F, n) \
    F(n, invert_x) \
    F(n, invert_y) \
    F(n, fixed_center)

struct padstick_params {
#define PADSTICK_INT_MEMBER(n, field, lo, hi) int32_t field;
#define PADSTICK_BOOL_MEMBER(n, field) bool field;
    PADSTICK_INT_FIELDS(PADSTICK_INT_MEMBER, 0)
    PADSTICK_BOOL_FIELDS(PADSTICK_BOOL_MEMBER, 0)
#undef PADSTICK_INT_MEMBER
#undef PADSTICK_BOOL_MEMBER
};

int padstick_get_params(const struct device *dev, struct padstick_params *out);
/* Validate and publish all values together. Changed values stop/rebase every
 * stream; a queued repeat keeps its in-flight obligation until acknowledged.
 * Already accepted input reports cannot be retracted. An unchanged set is a no-op. */
int padstick_set_params(const struct device *dev, const struct padstick_params *params);
