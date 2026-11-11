#!/usr/bin/env python3
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Check the actual native global IDs and guard the original sparse topology."""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument("workspace", type=Path)
parser.add_argument("build", type=Path)
parser.add_argument("--expected-sources", required=True)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
if not args.output.is_absolute() or args.output.exists() or args.output.is_symlink():
    raise SystemExit("source-bound check requires fresh absolute output")
generated = args.build / "zephyr/include/generated"
header = (generated / "zephyr/devicetree_generated.h").read_text()
sources = []
for name in ("a", "b"):
    match = re.search(r"^#define DT_N_INST_(\d+)_zmk_input_listener\s+DT_N_S_idle_listener_" + name + r"$",
                      header, re.M)
    if not match:
        raise RuntimeError("actual native source index is missing for " + name)
    sources.append(int(match.group(1)))
expected = [int(value) for value in args.expected_sources.split(",")]
if sources != expected:
    raise RuntimeError(f"actual native source IDs {sources} differ from {expected}")
args.output.mkdir(parents=True, exist_ok=False)
probe = args.output / "probe.c"
probe.write_text('#include <zephyr/devicetree.h>\n#include "padstick_sources.h"\n')
command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsyntax-only",
           "-DCONFIG_ARCH_POSIX=1", "-DCONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES=2",
           "-I" + str(generated), "-I" + str(args.workspace / "zephyr/include"),
           "-I" + str(ROOT / "src"), str(probe)]
result = subprocess.run(command, capture_output=True, text=True)
(args.output / "compiler.log").write_text(result.stdout + result.stderr)
rejection = max(sources) >= 2
diagnostic = "PadStick source limit excludes a referenced listener index"
if rejection:
    if result.returncode == 0 or diagnostic not in result.stderr:
        raise RuntimeError("actual original native topology must reject bound two by its named guard")
elif result.returncode:
    raise RuntimeError("actual limited native topology must admit bound two\n" + result.stderr)
(args.output / "result.json").write_text(json.dumps(dict(
    sources=sources, bound=2, command=command, exit=result.returncode,
    status="EXPECTED REJECTION" if rejection else "PASS",
    required_diagnostic=diagnostic if rejection else None), indent=2) + "\n")
print(f"PASS: actual native source IDs {sources}; bound two " +
      ("named rejection" if rejection else "admitted"))
