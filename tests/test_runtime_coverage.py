# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
"""Drive the real runtime entry with deliberately incomplete tool output."""
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


class RuntimeCoverageAdmission(unittest.TestCase):
    def test_unmeasured_runtime_cannot_pass(self):
        root = Path(__file__).resolve().parents[1]
        for kind in ('empty', 'outside', 'missing-range'):
            with self.subTest(report=kind), tempfile.TemporaryDirectory() as folder:
                work = Path(folder)
                tools = work / 'bin'
                tools.mkdir()
                compiler = tools / 'cc'
                compiler.write_text(textwrap.dedent('''\
                    #!/usr/bin/env python3
                    import json, os, pathlib, sys
                    args = sys.argv[1:]
                    with open(os.environ["RECORDING_COMPILER_LOG"], "a") as log:
                        log.write(json.dumps(args) + "\\n")
                    output = pathlib.Path(args[args.index("-o") + 1])
                    output.write_text("#!/bin/sh\\nexit 0\\n")
                    output.chmod(0o755)
                    '''), encoding='utf-8')
                compiler.chmod(0o755)
                gcov = tools / 'gcov'
                gcov.write_text(textwrap.dedent('''\
                    #!/usr/bin/env python3
                    import os, pathlib
                    kind = os.environ["COVERAGE_REPORT_KIND"]
                    text = "" if kind == "empty" else "1:0:unrelated metadata\\n"
                    if kind == "missing-range":
                        text="function padstick_abs_i32 called 1\\n1:16:one function only\\n"
                    for name in ("padstick_math.h", "input_processor_padstick.c"):
                        pathlib.Path(name+".gcov").write_text(text)
                    '''), encoding='utf-8')
                gcov.chmod(0o755)
                env = dict(os.environ, ZMK_TEST_MODE='full', PYTHONDONTWRITEBYTECODE='1',
                           RECORDING_COMPILER_LOG=str(work / 'compiler.jsonl'),
                           COVERAGE_REPORT_KIND=kind)
                env['PATH'] = str(tools) + os.pathsep + env['PATH']
                result = subprocess.run([sys.executable, str(root / 'tests/runtime/run.py')],
                                        env=env, capture_output=True, text=True, timeout=30)
                self.assertNotEqual(result.returncode, 0,
                                    'unmeasured runtime falsely passed: ' + result.stdout)
                self.assertIn('coverage report has no executable lines', result.stderr)
                if kind == 'missing-range':
                    self.assertIn('a gated function', result.stderr)
                calls = (work / 'compiler.jsonl').read_text(encoding='utf-8').splitlines()
                self.assertGreater(len(calls), 0)
                self.assertTrue(all(isinstance(json.loads(row), list) for row in calls))


class CompleteUnitCoverage(unittest.TestCase):
    def setUp(self):
        path = Path(__file__).parent / 'runtime/run.py'
        spec = importlib.util.spec_from_file_location('padstick_runtime', path)
        self.runtime = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.runtime)
        self.source = 'static int first(void) { return 1; }\nstatic int second(void) { return 2; }\n'
        self.report = ('function first called 1\nfunction second called 1\n'
                       '1:1:first\n1:2:second\nbranch 0 taken 1\nbranch 1 taken 1\n')

    def test_complete_report(self):
        self.assertEqual(self.runtime.check_report(self.report, self.source), (2, 2, 2))

    def test_missing_or_zero_measurement_is_rejected(self):
        reports = [
            '', '1:0:unrelated metadata\n',
            self.report.replace('function second called 1\n', ''),
            self.report.replace('function second called 1', 'function second called 0'),
            self.report.replace('1:2:second', '#####:2:second'),
            self.report.replace('1:2:second', '=====:2:second'),
            self.report.replace('branch 1 taken 1', 'branch 1 taken 0'),
            self.report.replace('branch 1 taken 1', 'branch 1 never executed'),
            self.report.replace('branch 0 taken 1\nbranch 1 taken 1\n', ''),
            # Metadata must not borrow a count across a newline.
            'function first called 1\nfunction second called 1\n-:1:first\n-:2:second\n'
        ]
        for report in reports:
            with self.subTest(report=report), self.assertRaises(RuntimeError):
                self.runtime.check_report(report, self.source)


if __name__ == '__main__':
    unittest.main()
