#!/usr/bin/env python3
"""A full port, with the app running: lf waits, and the app is stepped
only while no VM code runs (vmRunning in vm.h).

lf runs on a pty (-t) that this script stops reading, so its output backs
up: serial_putc must wait (the port is non-blocking), and lf_putc finds the
port busy, as the CH32H417's does when the host reads slowly. With the demo app running (`cold`) and a second task (as `try`
makes for the clock), a word the terminal
runs that prints a lot must neither run the app from inside the word nor
leave the terminal on the app's stack. Usage: test_appstep.py LF"""
import os
import pty
import select
import shutil
import subprocess
import sys
import tempfile
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '../../../../..'))
LF = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, 'bin', 'lf')


def main():
    if not hasattr(os, 'openpty'):
        print('appstep tests skipped (no ptys)')
        return 0
    tmp = tempfile.mkdtemp()
    m, s = pty.openpty()
    tty.setraw(s)
    name = os.ttyname(s)
    out = bytearray()

    def read(seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([m], [], [], 0.05)
            if r:
                try:
                    out.extend(os.read(m, 65536))
                except OSError:
                    return

    def quiet(limit):
        """Reads until lf has been quiet for a second."""
        end = time.time() + limit
        while time.time() < end:
            n = len(out)
            read(1.0)
            if len(out) == n:
                return

    def send(text):
        os.write(m, text.encode())

    try:
        shutil.copy(os.path.join(ROOT, 'bin', 'lfflash.bin'), tmp)
        shutil.copy(os.path.join(ROOT, 'bin', 'lfblocks.fb4'), tmp)
        p = subprocess.Popen([LF, '-o', '0', '-t', name], cwd=tmp,
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        read(1.0)
        send('0 open-flash 7 load 50 load\n')
        send(': spin ( -- ) begin pause again ;\n')
        send(': go ( -- ) t1 activate spin ;\n')
        send(': lots ( -- ) 20 for words next ;\n')
        send('close-flash\n')
        read(3.0)
        send('cold\n')                          # the demo app runs from now on
        send('go\n')                            # and a second task, as `try` makes
        read(1.0)
        out.clear()
        for _ in range(2):                      # far more than the pty holds
            send('words ' * 20 + '\n')         # C words the terminal runs,
        send('lots\n')                          # and a colon word that prints
        time.sleep(2.0)                         # not reading: lf_putc finds it busy
        quiet(60)
        send('1 2 + .\n')
        quiet(10)
        send('bye\n')
        read(1.0)
        p.wait(timeout=10)
    finally:
        os.close(m)
        os.close(s)
        shutil.rmtree(tmp)
    text = out.decode('utf-8', 'replace').replace('\r', '')
    tail = text[text.rfind('1 2 + .'):] if '1 2 + .' in text else text[-300:]
    problems = []
    if 'App error' in text:
        problems.append('the app failed')
    if '3 ok>' not in tail.replace('\n', ' ') and '3  ok>' not in tail.replace('\n', ' '):
        problems.append("`1 2 + .` didn't print 3")
    if '( ' in tail:                            # the prompt shows a stack: there should be none
        problems.append('items were left on the stack')
    lists = text.count('invert only forth words bye')
    if lists != 60:
        problems.append(f'{lists} word lists printed, not 60')
    if text.count('\n\n\n\n\n') > 0:
        problems.append('runs of blank lines')
    if problems:
        print('FAIL: ' + '; '.join(problems))
        print('--- the end of the output ---')
        print(text[-1500:])
        print('appstep tests FAILED')
        return 1
    print('appstep tests passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
