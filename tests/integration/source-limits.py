#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Compile the production allocation guard against actual Zephyr-generated DTS.

These are interface/compile controls. Native fixtures separately run the complete
production driver and real kernel with automatic and limited allocation.
"""
import argparse
import json
from pathlib import Path
import pickle
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
REJECTION = "PadStick source limit excludes a referenced listener index"
RANGE_REJECTION = "PadStick source allocation bound must be between 0 and 256"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("workspace", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--negative-control", choices=("skip-routes", "skip-children"))
    args = parser.parse_args()
    workspace = args.workspace.resolve(strict=True)
    output = args.output
    if not output.is_absolute() or output.exists() or output.is_symlink():
        raise SystemExit("source-limit output must be a fresh absolute path")
    zephyr = workspace / "zephyr"
    sys.path.insert(0, str(zephyr / "scripts/dts/python-devicetree/src"))
    from devicetree import edtlib

    output.mkdir(parents=True, exist_ok=False)
    bindings = [str(zephyr / "dts/bindings"), str(workspace / "zmk/app/dts/bindings"),
                str(ROOT / "dts/bindings")]
    cases = []
    # A sparse use of global index two must never be mistaken for one source.
    scenarios = (("base", "base", "okay", "okay"),
                 ("child", "child", "okay", "okay"),
                 ("disabled-child", "disabled-child", "okay", "okay"),
                 ("disabled-device", "base", "disabled", "okay"),
                 ("disabled-listener", "base", "okay", "disabled"),
                 ("no-listeners", "none", "okay", "okay"))
    for name, route, device_status, listener_status in scenarios:
        folder = output / name
        generated = folder / "include/zephyr"
        generated.mkdir(parents=True)
        route_text = "input-processors = <&other>, <&stick>;" if route == "base" else ""
        if route in ("child", "disabled-child"):
            child_status = 'status = "disabled";' if route == "disabled-child" else ""
            route_text = f'override {{ layers = <5>; {child_status} input-processors = <&other>, <&stick>; }};'
        listeners = "" if route == "none" else f'''
    listener_a {{ compatible = "zmk,input-listener"; device = <&source>; }};
    listener_b {{ compatible = "zmk,input-listener"; device = <&source>; }};
    listener_c {{ compatible = "zmk,input-listener"; device = <&target>;
                  status = "{listener_status}"; {route_text} }};'''
        dts = folder / "fixture.dts"
        dts.write_text(f'''/dts-v1/;
/ {{
    source: source {{ status = "okay"; }};
    target: target {{ status = "{device_status}"; }};
    other: other {{ compatible = "zmk,input-processor-code-mapper";
                   #input-processor-cells = <0>; type = <2>; map = <0 0>; }};
    stick: stick {{ compatible = "zmk,input-processor-padstick"; #input-processor-cells = <0>; }};
    {listeners}
}};
''')
        edt = edtlib.EDT(str(dts), bindings)
        nodes = edt.compat2nodes.get("zmk,input-listener", [])
        source_ids = {node.path: index for index, node in enumerate(nodes)}
        if route != "none" and source_ids["/listener_c"] != 2:
            raise RuntimeError("fixture must retain the sparse global source index two")
        edt_file = folder / "edt.pickle"
        edt_file.write_bytes(pickle.dumps(edt))
        generate = [sys.executable, str(zephyr / "scripts/dts/gen_defines.py"),
                    "--edt-pickle", str(edt_file), "--header-out", str(generated / "devicetree_generated.h")]
        subprocess.run(generate, check=True)
        probe = folder / "probe.c"
        fault = ""
        if args.negative_control == "skip-routes":
            fault = ('#undef DT_COMPAT_FOREACH_STATUS_OKAY_VARGS\n'
                     '#define DT_COMPAT_FOREACH_STATUS_OKAY_VARGS(...)\n')
        elif args.negative_control == "skip-children":
            fault = '#undef DT_FOREACH_CHILD_VARGS\n#define DT_FOREACH_CHILD_VARGS(...)\n'
        probe.write_text('#include <zephyr/devicetree.h>\n' + fault + '#include "padstick_sources.h"\n'
                         '_Static_assert(PADSTICK_STREAM_COUNT == EXPECTED_STREAMS, '
                         '"allocation count differs from the selected bound");\nint main(void) { return 0; }\n')
        enabled_count = sum(node.status == "okay" for node in nodes)
        for bound in (0, 1, 2, 3, 256, -1, 257):
            expected = max(enabled_count, 1) if bound == 0 else bound
            command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
                       "-DCONFIG_ARCH_POSIX=1",
                       "-I" + str(folder / "include"), "-I" + str(zephyr / "include"),
                       "-I" + str(ROOT / "src"), f"-DEXPECTED_STREAMS={expected}",
                       f"-DCONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES={bound}", str(probe)]
            result = subprocess.run(command, capture_output=True, text=True)
            (folder / f"bound-{bound}.log").write_text(result.stdout + result.stderr)
            out_of_range = bound not in range(257)
            sparse_rejection = (route in ("base", "child", "disabled-child") and
                                device_status == "okay" and listener_status == "okay" and
                                0 < bound <= 2)
            message = RANGE_REJECTION if out_of_range else REJECTION if sparse_rejection else None
            if message:
                if result.returncode == 0 or message not in result.stderr:
                    raise RuntimeError(f"{name}/{bound}: missing named rejection: {message}\n{result.stderr}")
            elif result.returncode:
                raise RuntimeError(f"{name}/{bound}: unexpected compiler failure\n{result.stderr}")
            cases.append(dict(name=name, bound=bound, source_ids=source_ids, command=command,
                              exit=result.returncode, status="EXPECTED REJECTION" if message else "PASS",
                              required_diagnostic=message))
            print(f"{name} bound={bound}: {cases[-1]['status']}", flush=True)
    (output / "results.json").write_text(json.dumps(cases, indent=2) + "\n")
    print(f"PadStick generated-DTS allocation controls: {len(cases)} cases PASS")


if __name__ == "__main__":
    main()
