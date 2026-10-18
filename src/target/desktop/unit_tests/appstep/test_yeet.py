#!/usr/bin/env python3
"""The app's yeet handler (forth/core.f) after a task fails with its output
redirected and junk on its stack: it must report on the console, and leave
the terminal on empty stacks with the multitasker reduced to the operator,
the app parked. Loads the current core.f (written into a copy of
bin/lfblocks.fb4 with putblocks.py). Usage: test_yeet.py LF"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '../../../../..'))
LF = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, 'bin', 'lf')

LINES = ['0 open-flash 7 load 50 load',
         ": boom ( -- ) t1 activate  ['] drop emit_xt !  1 2 3  -4 yeet ;",
         'close-flash',
         'cold',                                # the demo app: multitask, hi
         'boom',                                # t1 sends output nowhere, then fails
         '.( after: )',
         '1 2 + .',
         'counter @ counter @ = .',             # the app is parked: counter stays
         'bye']


def main():
    tmp = tempfile.mkdtemp()
    try:
        shutil.copy(os.path.join(ROOT, 'bin', 'lfflash.bin'), tmp)
        shutil.copy(os.path.join(ROOT, 'bin', 'lfblocks.fb4'), tmp)
        subprocess.run([sys.executable, os.path.join(ROOT, 'scripts', 'putblocks.py'),
                        os.path.join(ROOT, 'forth', 'core.f'), os.path.join(tmp, 'lfblocks.fb4')],
                       check=True, capture_output=True)
        p = subprocess.Popen([LF, '-o', '0'], cwd=tmp, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for line in LINES:                      # pauses, so the app runs in between
            p.stdin.write((line + '\n').encode())
            p.stdin.flush()
            time.sleep(0.4)
        p.stdin.close()
        text = p.stdout.read().decode('utf-8', 'replace').replace('\r', '')
        p.wait(timeout=10)
    finally:
        shutil.rmtree(tmp)
    after = text[text.find('after:'):]
    problems = []
    if 'App error -4 at ' not in text:
        problems.append('no "App error -4" on the console')
    if 'after: ok>3 ok>-1 ok>' not in after:
        problems.append('the terminal is not on empty stacks, or the app still runs')
    if '\n\n\n' in text:
        problems.append('runs of blank lines')
    if problems:
        print('FAIL: ' + '; '.join(problems))
        print(text[-800:])
        print('yeet handler tests FAILED')
        return 1
    print('yeet handler tests passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
