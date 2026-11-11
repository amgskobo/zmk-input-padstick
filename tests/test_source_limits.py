# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Run the complete driver with unchanged automatic and opt-in allocations."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SourceAllocation(unittest.TestCase):
    def test_complete_driver_allocation_matrix(self):
        variants = [("sanitized", ["-O1", "-g", "-fsanitize=address,undefined",
                                   "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"])]
        if os.environ.get("ZMK_TEST_MODE", "quick") == "full":
            variants += [("optimized", ["-O2"]), ("abi32", ["-m32", "-O2"])]
        with tempfile.TemporaryDirectory(prefix="padstick-source-limits-") as folder:
            for label, flags in variants:
                for listeners, bound, expected in ((2, 0, 2), (5, 0, 5), (5, 2, 2),
                                                   (5, 256, 256), (0, 0, 1), (5, 1, 1)):
                    with self.subTest(variant=label, listeners=listeners, bound=bound):
                        binary = Path(folder) / f"{label}-{listeners}-{bound}"
                        command = ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", *flags,
                                   f"-DTEST_LISTENER_COUNT={listeners}",
                                   f"-DCONFIG_ZMK_INPUT_PADSTICK_MAX_SOURCES={bound}",
                                   f"-DEXPECTED_STREAMS={expected}",
                                   "-I" + str(ROOT / "tests/support"),
                                   "-I" + str(ROOT / "include"),
                                   str(ROOT / "tests/runtime/source_limits_harness.c"),
                                   "-o", str(binary)]
                        subprocess.run(command, check=True)
                        result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
                        self.assertIn("padstick source allocation: PASS", result.stdout)
                        if expected >= 2:
                            self.assertIn("padstick driver: PASS", result.stdout)
                        print(f"{label} listeners={listeners} bound={bound}: {result.stdout}", end="")


if __name__ == "__main__":
    unittest.main()
