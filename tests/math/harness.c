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
