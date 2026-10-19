#!/usr/bin/env python3
"""Runs scripts/admin/hiltest.py, the hardware-in-the-loop tests, against
the simulated board instead of a real one, so the lab's script is itself
tested: the board's two serial ports are ptys, and this rig restarts the
simulation whenever the chip resets itself (`reboot`, the recovery
console's `reset`, or a V5F that doesn't start). Needs pyserial."""
import os
import pty
import subprocess
import sys
import threading
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_stage1 as t  # noqa: E402


def main():
    t.gen()
    board = t.Board('hil')
    usb_m, usb_s = pty.openpty()
    dbg_m, dbg_s = pty.openpty()
    for fd in (usb_m, usb_s, dbg_m, dbg_s):
        tty.setraw(fd)
    env = dict(os.environ, SIM_UID=t.UID, SIM_SETUP_MS='500')
    state = {'p': None, 'stop': False}

    def start():
        state['p'] = subprocess.Popen([os.path.join(t.WORK, 'sim')], cwd=board.dir, env=env,
                                      stdin=usb_m, stdout=usb_m, stderr=dbg_m)

    def rig():                          # the chip reset itself: start it again
        while not state['stop']:
            if state['p'].poll() is not None:
                start()
            time.sleep(0.02)

    start()
    threading.Thread(target=rig, daemon=True).start()
    os.makedirs(os.path.join(HERE, 'reports'), exist_ok=True)
    report = os.path.join(HERE, 'reports', 'hil-sim.md')
    hil = os.path.join(t.ROOT, 'scripts', 'admin', 'hiltest.py')
    common = ['--debug', os.ttyname(dbg_s), '--usb', os.ttyname(usb_s)]
    r = subprocess.run([sys.executable, hil, 'check', *common], timeout=120)
    ok = r.returncode == 0
    r = subprocess.run([sys.executable, hil, 'run', *common, '--good', t.PACKAGES['v1'],
                        '--hang', t.PACKAGES['v2hang'], '--report', report], timeout=600)
    ok = ok and r.returncode == 0
    state['stop'] = True
    state['p'].kill()
    print('hiltest on the simulated board: ' + ('passed' if ok else f'FAILED (see {report})'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
