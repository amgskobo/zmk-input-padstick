# PadStick owner checks

PadStick owns radial conversion, source-local contact/origin/remainders and
20 ms repeat admission/cancellation. Every original button payload and sync
passes unchanged. Source-button disposition and filtered-frame completion belong
to an independent final filter configured by the application.

```sh
bash tests/run-docker.sh
ZMK_TEST_MODE=full bash tests/run-docker.sh
ZMK_TEST_OUTPUT=/fresh/native bash tests/run-native.sh /resolved/workspace
ZMK_TEST_OFFLINE_WORKSPACE=/resolved/workspace ZMK_TEST_OUTPUT=/fresh/integration \
  bash /src/tests/integration/run.sh upstream # or dya
```

| Boundary | Canonical fixture and retained assertions |
| --- | --- |
| Pure math | runtime/math_harness.c includes actual private padstick_math.h: radial distance/ramp, integer extremes, inversion, signed fractions and saturation |
| Complete production driver | runtime/driver_harness.c includes actual driver/config/state/API/instance: independent streams, settle/touch/fixed origins, remainder, inferred contact, repeat admission/error/recovery and layer/touch cancellation |
| Real kernel/API | integration/module/src/runtime_test.c: actual device/layer dispatcher; invalid stream, source transparency, independent contacts, one actual input pair in flight with a controlled raw-Y dequeue, overdue touch/layer cancellation; same binary3 times per core |
| Firmware | integration/run.sh: two listeners share one node; ARM registration on unchanged stock/DYA, required low-priority work queue and no module warnings |
| Test admission | test_runtime_coverage.py: actual full entry plus direct controls reject empty/unrelated/partial/zero-function/missed line/branch reports |
| Offline/output safety | integration/test_offline.py and test_output_guard.py: exact clean pins, safe project/output/symlink layout, fresh output and no west calls on rejection; pinned Custom Settings archive/digest admission on DYA |

Quick runs every host case once under ASan/UBSan. Full adds optimized and complete
function/line/branch coverage, independently for math and whole driver. No lifted
production functions or copied structures remain. X/Y share one reducer; only
converted synchronized Y confirms an inferred contact frame. The source-local
contact, input device, work/mutex and pending-pair state remain necessary motion
state. Logging remains active for initialization/inference and report errors.

Read AGENTS and resolved dependency instructions first. The offline entry checks
stock5b51501fead672c41b5cfb396f3dafe0894bf4e9 or DYA
e5c9b6915b56801193e359dd9bad4a167ce0d1b8, Zephyr
10ba6d0cb38bc3d258775d27982f707599320085 and required library pins. It makes an
unchanged local core clone and dependency links, with no fetch/repair. The DYA fixture now
selects pinned Custom Settings and the adapter; stock does not. Supply
`ZMK_TEST_CUSTOM_SETTINGS_ARCHIVE` for offline DYA. Identity/layout/fresh-output guards remain; deliberate
ZMK_TEST_OFFLINE_NEGATIVE_CONTROL=skip-identity must break pin rejection tests.
Use a fresh output and retain commands, identities, ordered raw logs and failures.

## Transferred and retired boundaries

Former source-button pairing and final-sync assertions remain in the independent
zmk-input-suppress host and actual listener/HID fixtures. PadStick tests preserve
transparent KEY values/sync across stream/layer changes. Old filter profiles,
settings/window/layer-watcher interfaces are retired, not reimplemented here.
Prior unique assertions, maps and first failures are externally archived by the
consumer before removal; no failed snapshot was accepted.

Retired split-hold simulation retains state/math/error assertions above. Its
physical ordered split/HID/LED/battery trace and unresolved failures remain gaps.
Host queue errors are injected; native uses a real input sink with controlled
acknowledgements and synthetic direct-call source indices. Physical HID congestion,
arbitrary thread interleavings, sensor/USB/BLE/split delivery, timing and power
remain separate; native does not prove physical HID acceptance. See contract.json
and maintenance.json for current commands and ownership.

## Runtime settings

Boot configuration rejects INT32_MIN on either center axis before initializing
the device, matching the live API's reserved-coordinate rule. The complete-driver
host fixture tests X and Y independently: before repair a stationary (512,512)
input produced (16,0) or (0,16). Each regression requires -EINVAL; INT32_MIN+1
and INT32_MAX remain accepted. Focus either regression with
`PADSTICK_TEST_CASE=boot-center-x` or `boot-center-y` in a quick runtime run.
Existing DTS scale/deadzone clamping and valid defaults remain unchanged.

The real-DTS entry `ZMK_TEST_OUTPUT=/fresh/guards bash
tests/integration/guards/run.sh /resolved/workspace` requires named compile
rejections for X/Y sentinel centers and successful compilation with the adjacent
signed limits. Both integration variants dispatch it before the retained native
suite. Existing, linked and dangling result paths fail before west is called.

The complete adapter fixture covers 30 keys across two nodes, defaults/ranges,
initialization/change events, read/type failures, rejected snapshots and duplicate
keys. API cases cover every integer bound, atomic rejection, no-op writes,
isolation, rebase and queued-pair admission. Named controls detect ignored apply
and key/storage overflow. All previous motion/button assertions remain; lock
assertions now follow one node mutex replacing per-stream mutexes.

Both supported-core native entries exercise live parameters and settings canceling
an overdue repeat. DYA tests actual registry rejection/application and NVS
save/reload: three fresh flash files, first boot plus reload of one binary.
Test node labels are preserved with shorter storage names. Native storage/API
results do not establish physical Studio RPC, flash endurance or HID delivery.

For standalone DYA native settings/NVS checks, set
`ZMK_TEST_CUSTOM_SETTINGS=1` and use a resolved fixture workspace that includes
the pinned registry. The complete DYA integration entry selects this automatically.

The readable `amgskobo__padstick` namespace is asserted literally by the whole
adapter fixture. A `padstick_right` fixture node exercises the exact 64-byte
storage limit; overlong-key/storage controls remain. Native node labels are
preserved while the passthrough node name is shortened to `stick_pass`.

## Scheduled-work and settings regressions

The bounded host FIFO and actual native input queue now force X admission / Y
refusal. They verify retry-only-Y, no duplicate X, ordered completion and recovery
across touch/layer/settings cancellation. The complete driver also tests synchronous
callback reentry on either axis. X refusal now attempts no Y, so the existing
failure case expects one warning/API attempt instead of two; all value/order and
recovery assertions remain. The native fixture deliberately uses an eight-slot
queue; this is API/kernel evidence, not physical HID congestion acceptance.

## Selected workspace and module providers

Workspace selection uses tests/integration/workspace-env.sh to export the
selected manifest's ZEPHYR_MODULES and explicit Zephyr package/base paths. A
Zephyr symlink to a different cache must not select that cache's module providers.
The owner test_workspace_env.py exercises actual selection with a poisoned ambient
list, a cache symlink, module paths containing spaces, failed project discovery and
an empty selection. The keep-ambient negative control must fail these assertions.
Full integration retains all supported-core, role, persistence and invalid-DTS
cases. No dependency is patched or required feature disabled.

## 32-bit arithmetic and clock contract

Coordinate offsets use checked 32-bit subtraction. Radial output distribution divides into whole and fractional parts before multiplication, preserving the existing bounds, signed remainder and report rounding without wide arithmetic.

runtime/math_harness.c retains radial direction/ramp, full-coordinate extremes, fractions, inversion and saturation. The whole-driver fixtures remain unchanged.

## Shared repeat distance

The repeat worker calculates the radial distance once for its stored X/Y pair, then applies each axis's own deadzone, gain, limit, inversion and fraction. Live ABS events still use their most recent partner coordinate independently. tests/test_repeat_cost.py compiles the complete driver fixture with compiler function-entry instrumentation at O0/O2: exact asymmetric/reversed/zero/saturated pairs, stream selection, sync and one square-root entry per repeat. The old worker fails the count assertion. The canonical host entry includes it; this is an operation count, not a CPU-cycle or physical timing measurement.

## Quiet motion and queued-pair resets

The complete driver fixture keeps a fixed-center deflection one count past the
deadzone without further ABS frames. Fractional X-only and Y-only movement must
reach one count after 31 repeat ticks; deadzone and zero-gain cases must stop.
A fully queued pair retains admission across touch/layer/settings resets, while
an unrelated source remains independent. Its raw synchronized Y retires it even
when the current route bypasses PadStick. A late processor call for that old Y
must leave a newer pair in flight. The former touch-reset assertion that forgot
queued ownership is replaced by preservation followed by an explicit dequeue;
all original motion, partial-admission and transparent-button cases remain.

The native fixture pauses the input callback before acknowledgement, tests the
same fully queued reset with real input/work queues, and requires quiet fractional
movement within the 20ms cadence plus the configured tick rounding. Native
controlled acknowledgements now gate raw dequeue rather than injecting a REL
call into a selected processor. These are kernel/API boundaries, not physical
HID congestion or sensor timing evidence.

```sh
python3 tests/check_repeat_regressions.py --baseline-revision PRIOR_COMMIT \
  --output FRESH_OUTPUT
```

This compiles the same owner assertions with the current and unmodified prior
driver; both prior fractional-hold and queued-reset cases must fail at their
named behavioral assertions. Production callback order is checked in linked
native and firmware symbols: the raw observer precedes ZMK input handlers.

## Source allocation bound

`CONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES=0` preserves the automatic global-listener
allocation (minimum one). Positive `1..256` values preserve global source
indices and must cover every reachable listener and direct API caller.
`test_source_limits.py` compiles the actual complete driver with automatic two
and five sources, bound two, the full 256-index domain and one-slot/no-listener
fallbacks. Quick runs ASan/UBSan; full also runs O2 and 32-bit ABI variants.
Every allocation of at least two slots runs every existing complete-driver
assertion, including fractional motion, source isolation, pending/partial pairs,
raw acknowledgement and touch/layer/settings cancellation. One-slot probes use
the same driver/service stand-ins without compiling two-source scenarios.
NULL source zero, the highest admitted index and invalid-index byte/state
transparency have independent assertions. No invalid uint8 index exists at 256.

Each integration variant calls `integration/source-limits.py` against that
core's original binding and Zephyr generator. Its 42 compile cases cover base,
child, disabled child, disabled device, disabled listener and no-listener
topologies at automatic/1/2/3/256/-1/257 bounds. Sparse global index 2 must be
rejected by bound 1/2 even though only one route refers to PadStick. Disabled
child routes are included because upstream enumerates all children; disabled
listener/device paths follow upstream's status gate. Other processor phandles
precede PadStick to verify traversal beyond the first element.

```sh
python3 tests/integration/source-limits.py /resolved/workspace --output /fresh/dts
python3 tests/integration/source-limits.py /resolved/workspace \
  --output /fresh/control --negative-control skip-routes # must fail: base/1
python3 tests/integration/source-limits.py /resolved/workspace \
  --output /fresh/child-control --negative-control skip-children # must fail: child/1
```

These controls qualify only at their named missing-rejection diagnostics.
The unchanged original driver is separately compiled with
`-DPADSTICK_DRIVER_SOURCE='"/original/src/input_processor_padstick.c"'`,
`TEST_LISTENER_COUNT=5`, bound 2 and `EXPECTED_STREAMS=2`; it must compile and
then fail the named allocation assertion, not a compiler/setup error.
The real native fixture has two PadStick listeners plus three unrelated
listeners. Both supported-core entries run automatic five-slot and limited
two-slot binaries three times each. An additional automatic five-slot profile
preserves the original include order and global source IDs 3/4. It runs every
original assertion at those actual indices, plus real listener-callback checks;
the low-index profiles check actual IDs 0/1. `check-source-bound.py` asserts each
mapping against the generated header and requires a named bound-two rejection
for the original higher-index profile. DYA retains every registry/NVS boot/reload
case in all three configurations. Compiler frames and linked node sizes are static
evidence; they do not measure live stack high-water or physical HID delivery.
