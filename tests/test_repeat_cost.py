# SPDX-License-Identifier: MIT
"""Count actual square-root entries in the complete repeat worker."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RepeatCostTests(unittest.TestCase):
    def test_one_radial_square_root_per_pair(self):
        with tempfile.TemporaryDirectory(prefix="padstick-repeat-cost-") as folder:
            for optimization in ("-O0", "-O2"):
                binary = Path(folder) / optimization[1:]
                subprocess.run([
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", optimization,
                    "-finstrument-functions", "-I" + str(ROOT / "tests/support"),
                    "-I" + str(ROOT / "include"),
                    str(ROOT / "tests/runtime/repeat_cost_harness.c"), "-o", str(binary),
                ], check=True)
                subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
