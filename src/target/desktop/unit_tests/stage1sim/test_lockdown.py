#!/usr/bin/env python3
"""Runs scripts/admin/lockdown.py against the simulated board (a level 2
build: sim_strict), with a fake wlink, so the script that locks boards
down is itself tested. The board's serial ports are ptys, and this rig
restarts the simulation whenever the chip resets. The fake wlink (written
here) logs every call, refuses anything lockdown.py mustn't run, writes
`flash` images into the board's flash.bin, models the probe's halting (status, dump and flash stop the simulation; a
reset leaves it stopped until `resume`), and answers `status` and the option bytes from ob.bin, as the
chip loads them. Needs pyserial."""
import os
import pty
import signal
import subprocess
import sys
import threading
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_stage1 as t  # noqa: E402

LOCKDOWN = os.path.join(t.ROOT, 'scripts', 'admin', 'lockdown.py')

FAKE_WLINK = r'''#!PYTHON
import os, signal, sys
board = os.environ["FAKE_BOARD"]
args = sys.argv[1:]
with open(os.path.join(board, "wlink.log"), "a") as log:
    log.write(" ".join(args) + "\n")

def ob():
    p = os.path.join(board, "ob.bin")
    if not os.path.exists(p):
        return 0xC1 << 2, 0xFFFFFFFF            # fresh from WCH, as sim.c
    b = open(p, "rb").read()
    h = [int.from_bytes(b[2 * i:2 * i + 2], "little") for i in range(8)]
    obr = (2 if (h[0] & 0xFF) != 0xA5 else 0) | (h[1] & 0xFF) << 2
    return obr, sum((h[4 + i] & 0xFF) << (8 * i) for i in range(4))

HALTED = os.path.join(board, "halted")

def sim(sig):
    try:
        os.kill(int(open(os.path.join(board, "sim.pid")).read()), sig)
    except (OSError, ValueError):
        pass

def halt():                     # as the real probe: attaching halts the core,
    open(HALTED, "w").close()   # and the halt request stays set
    sim(signal.SIGSTOP)

def reset():                    # a reset with the request set: halted again
    sim(signal.SIGKILL)         # (the rig doesn't restart a halted board)

def num(v):
    return int(v, 0)

cmd = args[0] if args else ""
if cmd in ("status", "dump", "flash"):
    halt()
if cmd == "status":
    print("Connected to WCH-Link v2.20(v40) (WCH-LinkE-CH32V305) (fake)")
    print("Chip ESIG: FlashSize(960KB) UID(01-02-03-04-05-06-07-08)")
    print("Flash protected: " + ("true" if ob()[0] & 2 else "false"))
elif cmd == "dump" and os.path.exists(os.path.join(board, "flaky")):
    os.remove(os.path.join(board, "flaky"))     # the probe doesn't attach, once
    print("Error: WCH-Link underlying protocol error: 0x55")
    sys.exit(1)
elif cmd == "dump":
    a, n = num(args[1]), num(args[2])
    if a == 0x4002201C:
        obr, wpr = ob()
        data = (obr.to_bytes(4, "little") + wpr.to_bytes(4, "little"))[:n]
    else:
        with open(os.path.join(board, "flash.bin"), "rb") as f:
            f.seek(a - 0x08000000)
            data = f.read(n)
    if "-o" in args:
        open(args[args.index("-o") + 1], "wb").write(data)
    else:
        print(data.hex())
elif cmd == "flash" and os.path.exists(os.path.join(board, "flakyflash")):
    os.remove(os.path.join(board, "flakyflash"))  # no attach, before writing anything
    print("Error: WCH-Link underlying protocol error: 0x55")
    sys.exit(1)
elif cmd == "flash" and "-e" not in args and "--erase" not in args:
    if ob()[0] & 2 or ob()[1] != 0xFFFFFFFF:
        print("fake wlink: flash on a protected chip: this would have unprotected it")
        sys.exit(9)
    a = num(args[args.index("--address") + 1]) if "--address" in args else 0x08000000
    data = open(args[-1], "rb").read()
    with open(os.path.join(board, "flash.bin"), "r+b") as f:
        f.seek(a - 0x08000000)
        f.write(data)
    print(f"Flashing {len(data)} bytes to 0x{a:08x}")
    if "-R" not in args:
        reset()
elif cmd == "reset":
    reset()
elif cmd == "resume":
    if os.path.exists(HALTED):
        os.remove(HALTED)
    sim(signal.SIGCONT)
else:
    print("fake wlink: " + " ".join(args) + " isn't something lockdown.py should run")
    sys.exit(9)
'''


def check(cond, what, got):
    if not t.check(cond, what):
        print('   got:', str(got)[-3000:])


class Rig:
    """A level 2 board on ptys, restarted at every reset."""

    def __init__(self, name):
        self.board = t.Board(name)
        self.usb_m, self.usb_s = pty.openpty()
        self.dbg_m, self.dbg_s = pty.openpty()
        for fd in (self.usb_m, self.usb_s, self.dbg_m, self.dbg_s):
            tty.setraw(fd)
        self.env = dict(os.environ, SIM_UID=t.UID, SIM_SETUP_MS='300')
        self.p = None
        self.stop = False
        self.start()
        threading.Thread(target=self.watch, daemon=True).start()

    def start(self):
        self.p = subprocess.Popen([os.path.join(t.WORK, 'sim_strict')], cwd=self.board.dir,
                                  env=self.env, stdin=self.usb_m, stdout=self.usb_m,
                                  stderr=self.dbg_m)
        with open(os.path.join(self.board.dir, 'sim.pid'), 'w') as f:
            f.write(str(self.p.pid))

    def watch(self):
        while not self.stop:
            halted = os.path.exists(os.path.join(self.board.dir, 'halted'))
            if self.p.poll() is not None and not halted:
                self.start()
            time.sleep(0.02)

    def lockdown(self, wlink, *args, answer='LOCK\n'):
        env = dict(os.environ, FAKE_BOARD=self.board.dir)
        report = os.path.join(self.board.dir, 'report.md')
        cmd = [sys.executable, LOCKDOWN, *args, '--wlink', wlink]
        if args[0] != 'check':
            cmd += ['--usb', os.ttyname(self.usb_s), '--debug', os.ttyname(self.dbg_s),
                    '--report', report]
        try:
            r = subprocess.run(cmd, input=answer, capture_output=True, text=True, env=env,
                               timeout=150)
            code, text = r.returncode, r.stdout + r.stderr
        except subprocess.TimeoutExpired as e:  # say what it got to
            out = [x.decode(errors='replace') if isinstance(x, bytes) else (x or '')
                   for x in (e.stdout, e.stderr)]
            code, text = 124, f'lockdown.py {args[0]} timed out after 150 s\n' + ''.join(out)
            print(text[-3000:], flush=True)
            p = os.path.join(self.board.dir, 'wlink.log')
            if os.path.exists(p):
                with open(p) as f:
                    print('wlink calls:', f.read().splitlines(), flush=True)
        if os.path.exists(report):
            text += open(report).read()
        return code, text

    def log(self):
        p = os.path.join(self.board.dir, 'wlink.log')
        if not os.path.exists(p):
            return []
        lines = open(p).read().splitlines()
        os.remove(p)
        return lines

    def ob(self):
        return os.path.exists(os.path.join(self.board.dir, 'ob.bin'))

    def close(self):
        self.stop = True
        self.p.kill()


def main():
    t.gen()
    wlink = os.path.join(t.WORK, 'fakewlink')
    with open(wlink, 'w') as f:
        f.write(FAKE_WLINK.replace('PYTHON', sys.executable, 1))
    os.chmod(wlink, 0o755)
    v3f = os.path.join(t.WORK, 'v3f-image.bin')
    with open(v3f, 'wb') as f:
        f.write(bytes(range(256)) * 16)
    v5f = os.path.join(t.WORK, 'v1.bin')
    run = ['run', '--v3f', v3f, '--v5f', v5f]

    rig = Rig('lockdown-script-no')     # the answer isn't LOCK: nothing locked
    code, text = rig.lockdown(wlink, *run, answer='no\n')
    check(code == 1 and 'Not locked' in text and not rig.ob(),
            'lockdown.py: not LOCK, not locked', text)
    log = rig.log()
    check(any(l.startswith('flash -R --address 0x8000000') for l in log)
            and any(l.startswith('flash -R --address 0x8010000') for l in log)
            and not any(w in l for l in log for w in ('erase', 'protect', '-e ')),
            'lockdown.py: both images flashed, no erase or protect', log)
    rig.close()

    rig = Rig('lockdown-script')
    code, text = rig.lockdown(wlink, 'check')
    check(code == 1 and 'NOT locked down' in text, 'lockdown.py check: an open board', text)
    rig.log()
    open(os.path.join(rig.board.dir, 'flaky'), 'w').close()    # the first dump fails
    open(os.path.join(rig.board.dir, 'flakyflash'), 'w').close()  # and the first flash
    code, text = rig.lockdown(wlink, *run)
    check(code == 0 and 'Locked down.' in text and rig.ob(), 'lockdown.py run: locked', text)
    check('(wlink flash failed, try 1:' in text,
          'lockdown.py run: a flash that didn\'t attach tried again', text)
    check('(wlink dump failed, try 1:' in text and 'trying again' in text, 'lockdown.py run: a failed attach tried again', text)
    check('lockdown: option bytes now RDPR 00FF' in text and 'LiteForth starting' in text,
            'lockdown.py run: the report has both ports\' output', text)
    with open(os.path.join(rig.board.dir, 'flash.bin'), 'rb') as f:
        f.seek(t.V5F_ADDR)
        check(f.read(os.path.getsize(v5f)) == open(v5f, 'rb').read(),
                'lockdown.py run: the V5F image in its slot', '')
    rig.log()
    code, text = rig.lockdown(wlink, 'check')
    check(code == 0 and 'locked down' in text and 'NOT' not in text,
            'lockdown.py check: a locked board', text)
    rig.log()
    code, text = rig.lockdown(wlink, *run)
    log = rig.log()
    check(code == 1 and 'read-protected already' in text
            and not any(l.startswith('flash') for l in log),
            'lockdown.py run: a locked board refused before any flash', (text, log))
    rig.close()

    rig = Rig('lockdown-script-lock')   # lock: a level 2 board in recovery, no flashing
    code, text = rig.lockdown(wlink, 'lock')
    log = rig.log()
    check(code == 0 and 'Locked down.' in text and rig.ob()
          and not any(l.startswith('flash') for l in log),
          'lockdown.py lock: locked, nothing flashed', (text, log))
    check('Stage 1: OBR 00000304, WPR FFFFFFFF' in text and log[:1] == ['status'],
          'lockdown.py lock: the option bytes from stage 1, wlink only after the lock',
          (text, log))
    rig.close()
    if t.failures:
        print(f'lockdown.py on the simulated board: {t.failures} checks FAILED')
        return 1
    print('lockdown.py on the simulated board: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
