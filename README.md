# zmk-input-padstick

[日本語](README_JA.md)

A ZMK input processor for using an absolute-reporting trackpad as a small joystick-style pointing surface.

`zmk,input-processor-padstick` stores the first `INPUT_ABS_X` and `INPUT_ABS_Y` values after `INPUT_BTN_TOUCH` as the touch origin. Later absolute reports are converted to `INPUT_REL_X` and `INPUT_REL_Y` based on distance from that origin.

## Features

- **Origin-based motion**: Uses the touched position as a temporary joystick center.
- **Deadzone**: Ignores contact jitter around the touch origin.
- **Smooth acceleration**: Ramps from fine movement scale to accelerated scale by distance, using integer math only.
- **Sub-pixel accumulation**: Keeps fractional REL counts per axis so low scale values do not drop small movement.
- **Optional suppression**: Can consume original ABS, `BTN_TOUCH`, and `BTN_0` events after processing.
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
    x-deadzone = <24>;
    y-deadzone = <24>;
    x-scale = <256>;
    y-scale = <256>;
    x-accel-range = <96>;
    y-accel-range = <96>;
    x-accel-scale = <512>;
    y-accel-scale = <512>;
    max-x = <80>;
    max-y = <80>;
    suppress-abs;
    suppress-btn-touch;
};

/* Add padstick to your trackpad pipeline */
&trackpad_listener {
    input-processors = <&padstick>;
};
```

### 3. Motion Semantics

- `BTN_TOUCH` press resets the touch origin and sub-pixel remainders.
- The first `ABS_X` and `ABS_Y` values after touch become the origin coordinates.
- Movement inside `x-deadzone` / `y-deadzone` emits zero and clears the axis remainder.
- Movement outside the deadzone is scaled as fixed point where `256` is `1.0x`.
- Acceleration ramps smoothly from `x-scale` / `y-scale` to `x-accel-scale` / `y-accel-scale` across `x-accel-range` / `y-accel-range`.
- Fractional output is accumulated per axis. For example, with `x-scale = <128>`, repeated 1-count movement outside the deadzone emits `REL_X = 1` every second event.
- Output is clamped by `max-x` / `max-y`; saturation clears the axis remainder.

## Debug Logging

Enable Zephyr logging and set ZMK's log level to debug to enable `LOG_DBG` output from this processor. For example, set `CONFIG_LOG=y` and `CONFIG_ZMK_LOG_LEVEL_DBG=y` in your ZMK config. Your build still needs a ZMK log backend, such as USB logging, RTT, or UART, to view the logs. `CONFIG_ZMK_USB_LOGGING=y` can be used when you want USB CDC ACM logging.

Debug logs include touch resets, stored origin coordinates, raw ABS coordinates, origin deltas, post-deadzone magnitude, fixed-point scaled value, incoming and outgoing sub-pixel remainder, generated REL output, saturation, and suppressed input events.

## Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `x-deadzone` | int | 20 | Absolute X counts around the touch origin that emit zero. |
| `y-deadzone` | int | 20 | Absolute Y counts around the touch origin that emit zero. |
| `x-scale` | int | 256 | X fine movement scale after the deadzone. `256` is `1.0x`. |
| `y-scale` | int | 256 | Y fine movement scale after the deadzone. `256` is `1.0x`. |
| `x-accel-range` | int | 96 | X counts after the deadzone used to ramp smoothly from `x-scale` to `x-accel-scale`. |
| `y-accel-range` | int | 96 | Y counts after the deadzone used to ramp smoothly from `y-scale` to `y-accel-scale`. |
| `x-accel-scale` | int | 512 | X scale reached at the end of `x-accel-range`. `512` is `2.0x`. |
| `y-accel-scale` | int | 512 | Y scale reached at the end of `y-accel-range`. `512` is `2.0x`. |
| `max-x` | int | 127 | Maximum absolute `REL_X` value emitted by this processor. |
| `max-y` | int | 127 | Maximum absolute `REL_Y` value emitted by this processor. |
| `invert-x` | bool | false | Invert generated `REL_X` direction. |
| `invert-y` | bool | false | Invert generated `REL_Y` direction. |
| `suppress-abs` | bool | false | Suppress unconverted absolute events so ABS reports do not leak downstream. |
| `suppress-btn-touch` | bool | false | Suppress `BTN_TOUCH` events after using them to track contact state. |
| `suppress-btn0` | bool | false | Suppress `BTN_0` events when the trackpad reports a physical click. |

Scale and acceleration scale values are clamped to `0..4096` at runtime. Deadzone and max values are clamped to non-negative values.

## License

MIT License. See [LICENSE](LICENSE) for details.
