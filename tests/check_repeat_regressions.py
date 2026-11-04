# SPDX-License-Identifier: MIT
"""Compile identical owner assertions against current and unmodified prior driver."""
import argparse, hashlib, json, os
from pathlib import Path
import shutil, subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-revision', required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=False)
rows=[]
for label in ('current', 'baseline'):
    work=args.output/label
    (work/'src').mkdir(parents=True)
    (work/'tests/runtime').mkdir(parents=True)
    relative='src/input_processor_padstick.c'
    source=(root/relative).read_bytes() if label=='current' else subprocess.check_output(['git','show',args.baseline_revision+':'+relative],cwd=root)
    (work/relative).write_bytes(source)
    shutil.copyfile(root/'src/padstick_math.h',work/'src/padstick_math.h')
    shutil.copyfile(root/'tests/runtime/driver_harness.c',work/'tests/runtime/driver_harness.c')
    binary=work/'driver'
    command=['cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(root/'tests/support'),'-I'+str(root/'include')]
    if label=='baseline': command+=['-DPADSTICK_REVIEW_PRE_FIX']
    command += [str(work/'tests/runtime/driver_harness.c'),'-o',str(binary)]
    result=subprocess.run(command,capture_output=True,text=True)
    (work/'compile.log').write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError(result.stderr)
    for case,message in [('fractional-hold','fractional held motion lost its timer'),('queued-reset','reset forgot a fully queued pair')]:
        result=subprocess.run([str(binary)],env=dict(os.environ,PADSTICK_TEST_CASE=case),capture_output=True,text=True)
        (work/(case+'.log')).write_text(result.stdout+result.stderr)
        if label=='current': assert result.returncode==0, result.stderr
        else: assert result.returncode!=0 and message in result.stderr, result.stderr
        rows.append(dict(source=label,case=case,exit_code=result.returncode,driver_sha256=hashlib.sha256(source).hexdigest()))
(args.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
print('PASS: current repeat regressions; both unmodified prior-driver failures qualified')
