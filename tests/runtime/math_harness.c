/* Copyright (c) 2026 amgskobo
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define ARG_UNUSED(x) (void)(x)
#define LOG_DBG(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define PADSTICK_DISTANCE_MAX 32767
#define PADSTICK_SCALE_MAX 4096
#define PADSTICK_SCALE_SHIFT 8
#define PADSTICK_SCALE_ONE ((int32_t)1 << PADSTICK_SCALE_SHIFT)

/* DRIVER_FUNCTIONS */

int main(void) {
    assert(padstick_abs_i32(INT32_MIN) == INT32_MAX);
    assert(padstick_abs_i32(-3) == 3);
    assert(padstick_abs_i32(4) == 4);
    assert(padstick_clamp_i32(-1, 0, 10) == 0);
    assert(padstick_clamp_i32(11, 0, 10) == 10);
    assert(padstick_clamp_i32(5, 0, 10) == 5);
    assert(padstick_isqrt(0) == 0);
    assert(padstick_isqrt(1) == 1);
    assert(padstick_isqrt(65535) == 255);
    assert(padstick_isqrt(UINT32_MAX) == 65535);
    assert(padstick_scale_distance(5, 0, 256, 512) == 1280);
    assert(padstick_scale_distance(5, 10, 512, 256) == 2560);
    assert(padstick_scale_distance(5, 1, 128, 256) > 0);
    assert(padstick_scale_distance(5, 10, 128, 256) > 0);
    assert(padstick_scale_distance(15, 10, 128, 256) > 0);
    /* The ramp, worked by hand: ratio = ((d - 1) << 8) / (range - 1),
     * ramp scale = scale + span * ratio >> 8, then the trapezium. */
    assert(padstick_scale_distance(5, 10, 128, 256) == 780);
    assert(padstick_scale_distance(15, 10, 128, 256) == 3200);
    assert(padstick_scale_distance(5, 1, 128, 256) == 1216);

    /* Past the deadzone, only the distance beyond it counts: 20 counts out
     * with a deadzone of 10 is 10 counts at scale 1.0. A negative deadzone is
     * none at all. */
    int32_t rem = 0;
    assert(padstick_apply_axis('x', 532, 512, 10, 256, 0, 256, 100, false, &rem, 0) == 10);
    assert(rem == 0);
    assert(padstick_apply_axis('x', 532, 512, -5, 256, 0, 256, 100, false, &rem, 0) == 20);
    /* The other axis counts toward the distance: 30 and 40 are 50 out, 40
     * past the deadzone, and this axis takes 30/50 of it. */
    assert(padstick_apply_axis('x', 542, 512, 10, 256, 0, 256, 100, false, &rem, 40) == 24);
    /* A negative limit is a limit of zero. */
    assert(padstick_apply_axis('x', 532, 512, 0, 256, 0, 256, -3, false, &rem, 0) == 0);
    /* The fraction carries from one step to the next: 600/256 each time. */
    rem = 0;
    assert(padstick_apply_axis('x', 514, 512, 0, 300, 0, 300, 100, false, &rem, 0) == 2);
    assert(rem == 88);
    assert(padstick_apply_axis('x', 514, 512, 0, 300, 0, 300, 100, false, &rem, 0) == 2);
    assert(rem == 176);
    assert(padstick_apply_axis('x', 514, 512, 0, 300, 0, 300, 100, false, &rem, 0) == 3);
    assert(rem == 8);
    /* Negative settings are clamped to zero: no scale, no ramp. */
    assert(padstick_scale_distance(10, 0, -256, 0) == 0);
    assert(padstick_scale_distance(10, 10, 0, 0) == 0);
    assert(padstick_scale_distance(10, -5, 256, 512) == 2560);
    /* An offset saturates at the ends of the range instead of wrapping, and a
     * step from one is still bounded and in the push's direction. */
    assert(padstick_offset(5, 3) == 2 && padstick_offset(3, 5) == -2);
    assert(padstick_offset(INT32_MIN, 1) == INT32_MIN);
    assert(padstick_offset(INT32_MAX, -1) == INT32_MAX);
    rem = 0;
    assert(padstick_apply_axis('x', INT32_MIN, 4000, 0, 256, 0, 256, 16, false, &rem, 0) == -16);
    assert(padstick_apply_axis('x', INT32_MAX, -4000, 0, 256, 0, 256, 16, false, &rem,
                               INT32_MIN) == 16);
    /* No distance is no step, on the ramp as off it. */
    assert(padstick_scale_distance(0, 10, 128, 256) == 0);

    /* On the deadzone edge nothing moves and the remainder is dropped. */
    int32_t edge_remainder = 100;
    assert(padstick_apply_axis('x', 522, 512, 10, 256, 0, 256, 39, false,
                               &edge_remainder, 0) == 0);
    assert(edge_remainder == 0);
    /* Landing exactly on the limit keeps the fraction; only past it is it
     * dropped. 2 counts at scale 300 is 600/256: 2 and 88 over. */
    int32_t fraction = 0;
    assert(padstick_apply_axis('x', 514, 512, 0, 300, 0, 300, 2, false, &fraction, 0) == 2);
    assert(fraction == 88);
    fraction = 0;
    assert(padstick_apply_axis('x', 510, 512, 0, 300, 0, 300, 2, false, &fraction, 0) == -2);
    assert(fraction == -88);

    int32_t remainder = 0;
    assert(padstick_apply_axis('x', 512, 512, 10, 256, 0, 256, 39, false,
                              &remainder, 0) == 0);
    assert(remainder == 0);
    assert(padstick_apply_axis('x', 532, 512, 0, 256, 0, 256, 39, false,
                              &remainder, 0) > 0);
    assert(padstick_apply_axis('x', 492, 512, 0, 256, 0, 256, 39, false,
                              &remainder, 0) < 0);
    assert(padstick_apply_axis('x', 532, 512, 0, 256, 0, 256, 39, true,
                              &remainder, 0) < 0);
    assert(padstick_apply_axis('x', 512, 512, 0, 256, 0, 256, 39, false,
                              &remainder, 20) == 0);
    assert(padstick_apply_axis('x', 900, 512, 0, 4096, 0, 4096, 2, false,
                              &remainder, 0) == 2);
    assert(remainder == 0);
    assert(padstick_apply_axis('x', 100, 512, 0, 4096, 0, 4096, 2, false,
                              &remainder, 0) == -2);
    assert(remainder == 0);
    assert(padstick_apply_axis('x', 40000, 0, 0, 256, 0, 256, 39, false,
                              &remainder, 40000) == 39);
    puts("padstick math: PASS");
    return 0;
}
