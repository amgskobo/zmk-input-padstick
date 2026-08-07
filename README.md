# zmk-input-padstick

[日本語](README_JA.md)

A ZMK input processor for using an absolute-reporting trackpad as a small joystick-style pointing surface.

`zmk,input-processor-padstick` ignores the first complete absolute report frame after `INPUT_BTN_TOUCH`, then converts later absolute reports to `INPUT_REL_X` and `INPUT_REL_Y`. By default the next `INPUT_ABS_X` and `INPUT_ABS_Y` values become the temporary touch origin. When `fixed-center` is enabled, the configured `x-center` / `y-center` coordinates are used instead.

## Features

- **Origin-based motion**: Uses the touched position as a temporary joystick center.
- **Fixed-center mode**: Can use a configured trackpad center instead of the touched position.
- **Deadzone**: Ignores contact jitter around the touch origin.
- **Smooth acceleration**: Ramps from fine movement scale to accelerated scale by distance, using integer math only.
- **Radial response**: Optional. Makes speed depend only on how far the finger is, not on which way it is pushed, and turns the deadzone into a circle.
- **Sub-pixel accumulation**: Keeps fractional REL counts per axis so low scale values do not drop small movement.
- **Optional suppression**: Can consume original ABS, `BTN_TOUCH`, and `BTN_0` events after processing.
- **Layer-change safe**: Drops the origin when the layer changes, so a contact split across two chains is not measured against an unrelated one.
- **Split-ready**: Enabled only on the central side for split builds.

## Installation

Add this module to your project's `config/west.yml` file.

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-padstick
      remote: amgskobo
      revision: main
```

## Quick Start

### 1. DTS Include

Include the standard helper in your shield's `.overlay` or `.zmk.dts`:

```dts
#include <zmk-input-padstick/input_processor_padstick.dtsi>
```

### 2. Configuration Example

**Note**: Enabling the compatible in DeviceTree automatically enables `CONFIG_ZMK_INPUT_PROCESSOR_PADSTICK` via Kconfig defaults when `CONFIG_ZMK_POINTING` is enabled.

```dts
/* Configure the padstick processor */
&padstick {
    x-deadzone = <48>;
    y-deadzone = <48>;
    x-scale = <8>;
    y-scale = <8>;
    x-accel-range = <464>;
    y-accel-range = <464>;
    x-accel-scale = <12>;
    y-accel-scale = <12>;
    max-x = <16>;
    max-y = <16>;
    fixed-center;
    x-center = <512>;
    y-center = <512>;
    suppress-abs;
    suppress-btn-touch;
};

/* Add padstick to your trackpad pipeline */
&trackpad_listener {
    input-processors = <&padstick>;
};
```

The defaults are tuned for a 1024 x 1024 absolute trackpad. A 48-count deadzone gives the touch origin a small play area, and the remaining roughly 464 counts from center to edge are used for the smooth acceleration ramp.

### 3. Motion Semantics

- `BTN_TOUCH` press resets the touch origin and sub-pixel remainders.
- The first complete `ABS_X` / `ABS_Y` frame after touch is suppressed as an origin-settle frame.
- With the default touch-origin mode, the next `ABS_X` and `ABS_Y` values become the origin coordinates.
- With `fixed-center`, `x-center` and `y-center` become the origin coordinates for every touch. Both are seeded together, since neither depends on the contact and a radial reading needs both from the first sample.
- Movement inside `x-deadzone` / `y-deadzone` emits zero and clears the axis remainder.
- Movement outside the deadzone is scaled as fixed point where `256` is `1.0x`.
- Acceleration ramps smoothly from `x-scale` / `y-scale` to `x-accel-scale` / `y-accel-scale` across `x-accel-range` / `y-accel-range`.
- Fractional output is accumulated per axis. For example, with `x-scale = <8>`, repeated 1-count movement outside the deadzone emits `REL_X = 1` every 32 events.
- Output is clamped by `max-x` / `max-y`; saturation clears the axis remainder.

### 4. Radial Response

Without `radial` the axes are independent, and how fast the stick runs depends on which way it is pushed. The deadzone is a square, and a ramp that rises with distance yields less when that distance is split between two axes than when it all lands on one, so a diagonal push comes out slower than a straight one.

`radial` takes the distance from the origin first, applies the ramp to that, then shares the resulting step out along the two axes in proportion to their displacement. Speed then depends only on how far the finger is, and the deadzone becomes a circle.

```dts
&padstick {
    radial;
};
```

It costs one integer square root and one divide per axis event. The square root is bit-by-bit, sixteen rounds of shifts and compares with no divide or multiply.

Because the axes arrive as separate events, the one that comes first reads its partner one sample behind - far below what a finger covers between reports.

### 5. Sizing `max-x` / `max-y`

The step for a given travel is the area under the ramp, so the output at the outer edge of `x-accel-range` is

```text
max = range * (scale + accel-scale) / 2 / 256
```

Set `max-x` / `max-y` to that value and the response saturates exactly at the rim of the usable area. Set it lower and the outer part of the pad is flat; set it higher and the rim never reaches full speed.

With `radial` the clamp still applies per axis, so it acts as a safety limit rather than the operating point - a diagonal push at full deflection puts about 0.71 of the step on each axis.

### 6. Layer Changes

Which processors run is decided per event, from the layer active at that moment, so one contact can be split across two chains. Without this, the instance a contact moves to would still hold an origin taken from an earlier touch and turn its first sample into the distance between two unrelated contacts.

`zmk_layer_state_changed` is therefore subscribed directly, and a layer change drops the origin and the sub-pixel remainders. The next frame is treated as an origin-settle frame, exactly as at touch-down.

The contact flag is deliberately left alone. Clearing it would silence the pointer until the finger lifted, which on a board that keeps one instance across every layer would stop motion the moment a layer key was pressed.

`suppress-btn0` never drops a `BTN_0` release whose press was not suppressed here. Passing a release through is always safe - the press it belongs to already reached the host - while dropping one would leave the button held down with nothing left to release it. That record is cleared on a layer change too.

## Debug Logging

Enable Zephyr logging and set ZMK's log level to debug to enable `LOG_DBG` output from this processor. For example, set `CONFIG_LOG=y` and `CONFIG_ZMK_LOG_LEVEL_DBG=y` in your ZMK config. Your build still needs a ZMK log backend, such as USB logging, RTT, or UART, to view the logs. `CONFIG_ZMK_USB_LOGGING=y` can be used when you want USB CDC ACM logging.

Debug logs include touch resets, stored origin coordinates, raw ABS coordinates, origin deltas, post-deadzone magnitude, fixed-point scaled value, incoming and outgoing sub-pixel remainder, generated REL output, saturation, and suppressed input events.

## Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `x-deadzone` | int | 48 | Absolute X counts around the touch origin that emit zero. |
| `y-deadzone` | int | 48 | Absolute Y counts around the touch origin that emit zero. |
| `x-scale` | int | 8 | X fine movement scale after the deadzone. `256` is `1.0x`. |
| `y-scale` | int | 8 | Y fine movement scale after the deadzone. `256` is `1.0x`. |
| `x-accel-range` | int | 464 | X counts after the deadzone used to ramp smoothly from `x-scale` to `x-accel-scale`. |
| `y-accel-range` | int | 464 | Y counts after the deadzone used to ramp smoothly from `y-scale` to `y-accel-scale`. |
| `x-accel-scale` | int | 12 | X scale reached at the end of `x-accel-range`. `256` is `1.0x`. |
| `y-accel-scale` | int | 12 | Y scale reached at the end of `y-accel-range`. `256` is `1.0x`. |
| `max-x` | int | 16 | Maximum absolute `REL_X` value emitted by this processor. |
| `max-y` | int | 16 | Maximum absolute `REL_Y` value emitted by this processor. |
| `x-center` | int | 512 | Fixed X center coordinate used when `fixed-center` is enabled. |
| `y-center` | int | 512 | Fixed Y center coordinate used when `fixed-center` is enabled. |
| `invert-x` | bool | false | Invert generated `REL_X` direction. |
| `invert-y` | bool | false | Invert generated `REL_Y` direction. |
| `fixed-center` | bool | false | Use `x-center` and `y-center` as the joystick origin instead of the touched position. |
| `radial` | bool | false | Take the distance from the origin before applying the ramp, then share the step out along the two axes. Makes speed independent of direction and the deadzone circular. |
| `suppress-abs` | bool | false | Suppress unconverted absolute events so ABS reports do not leak downstream. |
| `suppress-btn-touch` | bool | false | Suppress `BTN_TOUCH` events after using them to track contact state. |
| `suppress-btn0` | bool | false | Suppress `BTN_0` events when the trackpad reports a physical click. |

Scale and acceleration scale values are clamped to `0..4096` at runtime. Deadzone and max values are clamped to non-negative values.

## License

MIT License. See [LICENSE](LICENSE) for details.
