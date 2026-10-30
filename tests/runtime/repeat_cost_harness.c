/* SPDX-License-Identifier: MIT */
#define main padstick_behavior_main
#include "driver_harness.c"
#undef main

static unsigned square_roots;
void __cyg_profile_func_enter(void *function, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *function, void *caller) __attribute__((no_instrument_function));
void __cyg_profile_func_enter(void *function, void *caller) {
    (void)caller;
    if (function == (void *)padstick_isqrt) { square_roots++; }
}
void __cyg_profile_func_exit(void *function, void *caller) {
    (void)function;
    (void)caller;
}

int main(void) {
    struct padstick_params config = {
        .x_deadzone = 10, .y_deadzone = 20, .x_scale = 256, .y_scale = 512,
        .max_x = 100, .max_y = 100, .invert_y = true};
    processor = (struct device){.config = &config, .data = &owner,
        .name = "padstick", .api = &padstick_driver_api};
    /* Radius 50: X=(50-10)*30/50=24, Y=-(50-20)*2*40/50=-48.
     * Include reversed axes, zero/deadzone, one axis and saturated coordinates. */
    static const int32_t cases[][4] = {
        {30, 40, 24, -48}, {-30, -40, -24, 48}, {0, 0, 0, 0},
        {0, 40, 0, -40}, {INT32_MAX, INT32_MIN + 1, 100, 100},
    };
    for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
        assert(padstick_init(&processor) == 0);
        struct padstick_stream *s = stream(i % PADSTICK_STREAM_COUNT);
        s->touch_active = true;
        s->input_dev = &pad;
        s->origin_x = s->origin_y = 0;
        s->last_x = cases[i][0];
        s->last_y = cases[i][1];
        s->skip_origin_frame = false;
        listener_takes_pair = false;
        reports = 0;
        square_roots = 0;
        run_repeat(i % PADSTICK_STREAM_COUNT);
        assert(reports == ((cases[i][2] || cases[i][3]) ? 2 : 0));
        if (reports) {
            assert(reported[0] == cases[i][2] && reported[1] == cases[i][3]);
            assert(!reported_sync[0] && reported_sync[1]);
        }
        assert(s->x_remainder == 0 && s->y_remainder == 0);
        assert(square_roots == 1 && "one stored repeat pair needs only one radial square root");
    }
    puts("padstick repeat: exact asymmetric axes; one radial square root: PASS");
    return 0;
}
