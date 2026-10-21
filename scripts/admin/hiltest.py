#!/usr/bin/env python3
"""Hardware-in-the-loop tests of stage 1 on a real CH32H417 board: the
same scenarios as unit_tests/stage1sim plays on the simulated board,
driven from the PC with nobody at the board.

    hiltest.py --list
    hiltest.py check [--debug PORT] [--cli PORT] [--reset-cmd "COMMAND"]
    hiltest.py run --good GOOD.lfu --hang HANG.lfu [--only NAME] [--report FILE] [...]

It needs:
- the cli port: the board's USB terminal (LiteForth's CDC port, USB 1A86:FE0C; --cli,
  found by its IDs if not given);
- the debug port: the debug UART, through the WCH-LinkE (--debug, found by
  its USB IDs if not given, else COM14; 9600 baud);
- nothing else to reset the chip: it types `reboot` to LiteForth (or
  `reset` to stage 1's recovery console), and the V3F resets the chip by
  itself when the V5F doesn't start. --reset-cmd "COMMAND" (or HIL_RESET)
  uses a command instead, a debug probe's say;
- the card in the board with lfstage.bin and lfbackup.bin (at least 33
  blocks), as for any update;
- pyserial (`py -m pip install pyserial`).

--list shows every serial port with its USB IDs, maker and product, so you
can tell the WCH-LinkE's UART from the board's own CDC port. `check`
resets the board once and shows that all three work. `run` plays:

  install    GOOD.lfu staged with `-2 ymodem` over USB, installed at the
             next reset, started on trial and confirmed by LiteForth
  rollback   HANG.lfu (an image that never starts) installed; the V3F
             resets the chip each time it doesn't start, so after three
             trial boots it's rolled back, with no resets from here
  recovery   Ctrl+X x3 twice while the V5F restarts: recovery; GOOD.lfu
             sent to stage 1 itself, installed without a backup, confirmed
  lockdown   the boot's lock-down line, as the board reports it

GOOD.lfu and HANG.lfu are packages for this board (sealblocks.py package):
GOOD.lfu a V5F image that runs LiteForth, HANG.lfu one that doesn't start
(v5f-2-hang.lfu in doc/admin/board-tests.md B4). They write the V5F's flash slot
and the security record; the WCH-LinkE can always put things right.

The report (--report, default hil-report-DATE.md) has each scenario's
steps and both ports' output word for word, the packages' sha256 and the
board's build line, ready for doc/admin/board-tests.md. Exit status 0 if every
scenario passed.
"""
import argparse
import hashlib
import shlex
import inspect
import os
import queue
import re
import subprocess
import sys
import threading
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit('hiltest.py needs pyserial: py -m pip install pyserial')

LITEFORTH_VID, LITEFORTH_PID = 0x1A86, 0xFE0C
WCH_VID = 0x1A86

SOH, STX, EOT, ACK, NAK, CAN, SUB, C = 1, 2, 4, 6, 0x15, 0x18, 0x1A, 0x43

report = []
failures = 0


def note(text=''):
    report.append(text + '\n')


def stamp():
    return time.strftime('%H:%M:%S')


# ---------------------------------------------------------------------------
# Ports
# ---------------------------------------------------------------------------
def list_serial():
    ports = sorted(list_ports.comports(), key=lambda p: p.device)
    if not ports:
        print('no serial ports')
    for p in ports:
        ids = f'USB {p.vid:04X}:{p.pid:04X}' if p.vid is not None else 'not USB'
        what = ''
        if p.vid == LITEFORTH_VID and p.pid == LITEFORTH_PID:
            what = '  <- the cli port: the board\'s USB terminal (LiteForth)'
        elif p.vid == WCH_VID and 'link' in (p.description or p.product or '').lower():
            what = '  <- the debug port: a WCH-Link\'s UART'
        print(f'{p.device:10} {ids}  {p.description or ""}{what}')
        extra = ', '.join(x for x in (p.manufacturer and f'maker "{p.manufacturer}"',
                                      p.product and f'product "{p.product}"',
                                      p.serial_number and f'serial "{p.serial_number}"') if x)
        if extra:
            print(f'{"":10} {extra}')


def find_link_uart():
    """The debug port: the WCH-Link's UART, if exactly one is plugged in"""
    found = [p.device for p in list_ports.comports()
             if p.vid == WCH_VID and 'link' in (p.description or p.product or '').lower()]
    return found[0] if len(found) == 1 else None


def find_board_port():
    for p in list_ports.comports():
        if p.vid == LITEFORTH_VID and p.pid == LITEFORTH_PID:
            return p.device
    return None


class DebugPort:
    """The debug port (the WCH-Link's UART): always open; lines with the time they came."""

    def __init__(self, name, baud=9600):
        self.s = serial.Serial(name, baud, timeout=0.1)
        self.lines = []
        self.cv = threading.Condition()
        self.part = b''
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        while True:
            try:
                b = self.s.read(256)
            except serial.SerialException:
                time.sleep(0.5)
                continue
            if not b:
                continue
            self.part += b
            while b'\n' in self.part:
                line, self.part = self.part.split(b'\n', 1)
                with self.cv:
                    self.lines.append(line.decode('utf-8', 'replace').rstrip('\r'))
                    self.cv.notify_all()

    def mark(self):
        with self.cv:
            return len(self.lines)

    def wait(self, pattern, start, timeout):
        """The first line from index start on that matches, or None."""
        rx = re.compile(pattern)
        end = time.time() + timeout
        i = start
        with self.cv:
            while True:
                while i < len(self.lines):
                    if rx.search(self.lines[i]):
                        return self.lines[i]
                    i += 1
                left = end - time.time()
                if left <= 0:
                    return None
                self.cv.wait(min(left, 0.2))

    def since(self, start):
        with self.cv:
            return self.lines[start:]


class UsbPort:
    """The board's USB terminal. It goes away at every reset, so it's
    opened again whenever it's needed; DTR on, which the bridge needs."""

    def __init__(self, name):
        self.name = name
        self.s = None
        self.q = queue.Queue()
        self.text = bytearray()

    def open(self, timeout=20):
        self.close()
        end = time.time() + timeout
        while time.time() < end:
            name = self.name or find_board_port()
            if name:
                try:
                    self.s = serial.Serial(name, 115200, timeout=0.05)
                    try:
                        self.s.dtr = True       # pyserial raises it on open anyway
                    except OSError:
                        pass                    # a pty (tests) has no DTR
                    threading.Thread(target=self._read, args=(self.s,), daemon=True).start()
                    return True
                except serial.SerialException:
                    pass
            time.sleep(0.3)
        return False

    def _read(self, s):
        while self.s is s:
            try:
                b = s.read(256)
            except Exception:           # closed under us: that's the end of it
                break
            for c in b:
                self.q.put(c)

    def close(self):
        s, self.s = self.s, None
        if s:
            try:
                s.close()
            except (serial.SerialException, OSError):
                pass

    def write(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.s.write(data)
        self.s.flush()

    def byte(self, timeout):
        try:
            c = self.q.get(timeout=timeout)
        except queue.Empty:
            return None
        self.text.append(c)
        return c

    def wait(self, text, timeout):
        end = time.time() + timeout
        start = len(self.text)
        want = text.encode()
        while time.time() < end:
            if self.byte(0.1) is not None and bytes(self.text[start:]).endswith(want):
                return True
        return False

    def expect(self, wanted, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            c = self.byte(0.1)
            if c in wanted:
                return c
        return None

    def drain(self):
        while self.byte(0.05) is not None:
            pass

    # YMODEM, as kwai's ~x FILE -2 does it
    @staticmethod
    def crc16(data):
        crc = 0
        for b in data:
            crc ^= b << 8
            for _ in range(8):
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
        return crc

    def packet(self, n, chunk):
        head = SOH if len(chunk) == 128 else STX
        return bytes([head, n & 0xFF, 255 - (n & 0xFF)]) + chunk + self.crc16(chunk).to_bytes(2, 'big')

    def ymodem(self, path):
        with open(path, 'rb') as f:
            data = f.read()
        self.write('-2 ymodem\n')
        if self.expect({C, CAN}, 60) != C:
            return False
        info = os.path.basename(path).encode() + b'\0' + str(len(data)).encode()
        self.write(self.packet(0, info + bytes(128 - len(info))))
        if self.expect({ACK, CAN}) != ACK or self.expect({C, CAN}) != C:
            return False
        data_p = data + bytes([SUB]) * (-len(data) % 1024)
        for i in range(0, len(data_p), 1024):
            for _ in range(10):
                self.write(self.packet(i // 1024 + 1, data_p[i:i + 1024]))
                r = self.expect({ACK, NAK, CAN})
                if r == ACK:
                    break
                if r == CAN:
                    return False
            else:
                return False
        for _ in range(10):
            self.write(bytes([EOT]))
            if self.expect({ACK, NAK}) == ACK:
                break
        if self.expect({C}) != C:
            return False
        self.write(self.packet(0, bytes(128)))
        return self.expect({ACK, CAN}) == ACK


# ---------------------------------------------------------------------------
# The rig: the board and its two ports
# ---------------------------------------------------------------------------
class Rig:
    def __init__(self, a):
        self.a = a
        self.debug = DebugPort(a.debug)
        self.usb = UsbPort(a.usb)

    def reset(self):
        """A chip reset: `reboot` in LiteForth, `reset` in recovery, or
        --reset-cmd. True once the V3F has started again."""
        start = self.debug.mark()
        if self.a.reset_cmd:
            self.usb.close()
            cmd = [w.strip('"') for w in shlex.split(self.a.reset_cmd, posix=(os.name != 'nt'))]
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            if r.returncode != 0:
                print(f'{" ".join(cmd)} failed:\n{r.stdout}{r.stderr}')
                return False
            return self.debug.wait(r'^V3F', start, 10) is not None
        if not self.usb.open(10):
            print('no cli port to type reboot into')
            return False
        for line in ('\nreboot\n', '\nreset\n'):     # LiteForth, then the recovery console
            try:
                self.usb.write(line)
            except Exception:
                break                           # it went away: that was the reset
            if self.debug.wait(r'^V3F', start, 5):
                break
        self.usb.close()
        return self.debug.wait(r'^V3F', start, 5) is not None

    def section(self, name, doc):
        note(f'\n### {name}\n\n{doc}\n')

    def log(self, start, usb_from=None):
        lines = self.debug.since(start)
        if lines:
            note('Debug port:\n\n```\n' + '\n'.join(lines) + '\n```')
        if usb_from is not None:
            usb = bytes(self.usb.text[usb_from:]).decode('latin-1')
            usb = ''.join(ch if ch in '\n' or ' ' <= ch < '\x7f' else ('' if ch == '\r' else '.')
                          for ch in usb)
            usb = re.sub(r'\.{8,}', '<YMODEM>', usb)
            if usb.strip():
                note('Cli port:\n\n```\n' + usb.strip('\n') + '\n```')

    def reset_and_wait(self, pattern, timeout=40):
        start = self.debug.mark()
        if not self.reset():
            return None, start
        return self.debug.wait(pattern, start, timeout), start

    def stage(self, pkg):
        """`-2 ymodem` on LiteForth: the package into lfstage.bin."""
        if not self.usb.open():
            return False, 'no cli port'
        u0 = len(self.usb.text)
        self.usb.write('\n')
        self.usb.wait('ok>', 10)
        ok = self.usb.ymodem(pkg) and self.usb.wait('ok>', 60)
        text = bytes(self.usb.text[u0:]).decode('latin-1')
        self.usb.close()
        return ok and 'waiting for stage 1' in text, text


def check(cond, what):
    global failures
    if not cond:
        failures += 1
    note(f'- {"PASS" if cond else "**FAIL**"}: {what}')
    print(f'  {"ok  " if cond else "FAIL"} {what}')
    return cond


# ---------------------------------------------------------------------------
# The scenarios
# ---------------------------------------------------------------------------
def h_install(rig, a):
    """GOOD.lfu staged by LiteForth's `-2 ymodem`, installed at the next
    reset, started on trial and confirmed."""
    start = rig.debug.mark()
    u0 = len(rig.usb.text)
    ok, text = rig.stage(a.good)
    check(ok, 'staged: "waiting for stage 1 to install it"')
    rig.log(start, u0)
    line, start = rig.reset_and_wait(r'V3F update: .*(installed, on trial|refused)|recovery mode', 60)
    check(line and 'installed, on trial' in line, f'installed ({line})')
    check(rig.debug.wait(r'confirmed', start, 30) is not None, 'confirmed by LiteForth')
    rig.log(start)


def h_rollback(rig, a):
    """HANG.lfu installed. It never starts, so the V3F resets the chip each
    time it waits for it in vain: three trial boots, then the image before
    it is rolled back and starts LiteForth, with no resets from here."""
    start = rig.debug.mark()
    u0 = len(rig.usb.text)
    ok, text = rig.stage(a.hang)
    check(ok, 'the hanging image staged')
    rig.log(start, u0)
    line, start = rig.reset_and_wait(r'V3F update: .*(installed, on trial|refused)', 60)
    check(line and 'installed, on trial' in line, f'installed ({line})')
    for what in (r"didn't start within", 'boot 1 of 3', 'boot 2 of 3', 'boot 3 of 3',
                 'rolled back to version', 'LiteForth starting'):
        line = rig.debug.wait(what, start, 30)
        check(line is not None, f'by itself: {what.replace(chr(92), "")} ({line})')
        if line is None:
            break
    rig.log(start)


def h_recovery(rig, a):
    """Ctrl+X x3, which resets the chip with the V5F in safe mode, then
    Ctrl+X x3 again as soon as the port is back, while that safe start is
    still setting up: the chip resets into recovery. GOOD.lfu sent to stage
    1 itself is installed (the image marked as not working first, so no
    backup), on trial, and confirmed."""
    start = rig.debug.mark()
    if not check(rig.usb.open(), 'cli port open'):
        return
    u0 = len(rig.usb.text)
    rig.usb.write('\n')
    rig.usb.wait('ok>', 10)
    entered = None
    for attempt in range(3):
        rig.usb.write(b'\x18\x18\x18')
        if not rig.debug.wait(r'resetting the chip; the V5F starts in safe mode', start, 5):
            continue
        after = rig.debug.mark()
        rig.usb.close()                 # the chip resets: USB goes, then comes back
        if not rig.debug.wait(r'USB configured', after, 15) or not rig.usb.open():
            continue
        rig.usb.write(b'\x18\x18\x18')
        entered = rig.debug.wait(r'resetting into recovery', start, 5)
        if entered:
            break
        rig.debug.wait(r'LiteForth starting', start, 10)
        start2 = rig.debug.mark()
        note(f'- attempt {attempt + 1}: LiteForth was back before the second Ctrl+X x3')
        start = start2
    check(entered is not None, 'Ctrl+X x3 twice: "resetting into recovery"')
    rig.usb.close()
    line = rig.debug.wait(r'recovery mode', start, 30)
    check(line is not None, f'recovery mode ({line})')
    if not line or not check(rig.usb.open(), 'cli port open in recovery'):
        rig.log(start, u0)
        return
    seen = rig.usb.wait('recovery ok> ', 5)
    if not seen:                        # it came before the port was open: ask again
        rig.usb.write('\r')
        seen = rig.usb.wait('recovery ok> ', 5)
    check(seen, 'the recovery prompt')
    check(rig.usb.ymodem(a.good), 'GOOD.lfu sent to stage 1')
    check(rig.debug.wait(r'marked as not working', start, 30) is not None, 'the image marked as not working')
    check(rig.debug.wait(r'installed, on trial', start, 60) is not None, 'installed')
    check(rig.debug.wait(r'boot 1 of 3', start, 30) is not None, 'restarted, on trial')
    check(rig.debug.wait(r'confirmed', start, 30) is not None, 'confirmed')
    rig.usb.close()
    rig.log(start, u0)


def h_lockdown(rig, a):
    """The lock-down check at boot: what this board reports."""
    line, start = rig.reset_and_wait(r'security|LiteForth starting', 30)
    locked = line is not None and 'security' not in line
    note(f'- The board is {"locked down" if locked else "not locked down"}: `{line}`')
    print(f'  board: {line}')
    check(line is not None, 'the boot reported')
    rig.debug.wait(r'LiteForth starting', start, 30)
    rig.log(start)


SCENARIOS = [('install', h_install), ('rollback', h_rollback), ('recovery', h_recovery),
             ('lockdown', h_lockdown)]


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    p = argparse.ArgumentParser(description='Stage 1 tests on a real board',
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--list', action='store_true', help='list the serial ports and exit')
    p.add_argument('what', nargs='?', choices=('check', 'run'))
    p.add_argument('--debug', help='the debug port, the WCH-Link\'s UART (default: found by '
                                    'its USB IDs, else COM14)')
    p.add_argument('--cli', '--usb', dest='usb',
                   help="the cli port, the board's USB terminal (default: found by its USB IDs)")
    p.add_argument('--reset-cmd', default=os.environ.get('HIL_RESET'),
                   help='a command line that resets the board (default: type reboot)')
    p.add_argument('--good', help='a package of a V5F image that runs LiteForth')
    p.add_argument('--hang', help='a package of a V5F image that never starts')
    p.add_argument('--only', help='run only this scenario')
    p.add_argument('--report', default=time.strftime('hil-report-%Y%m%d-%H%M.md'))
    a = p.parse_args()
    a.debug = a.debug or find_link_uart() or 'COM14'
    if a.list:
        list_serial()
        return 0
    if not a.what:
        p.error('say --list, check or run')
    try:
        rig = Rig(a)
    except serial.SerialException as e:
        sys.exit(f'hiltest.py: {e}\nIs a terminal (kwai, or one watching the debug port) using '
                 'it? Close it and run this again. (`hiltest.py --list` shows the ports.)')
    if a.what == 'check':
        print(f'resetting with: {a.reset_cmd or "reboot, typed to LiteForth"}')
        line, start = rig.reset_and_wait(r'V3F', 10)
        print(f'debug port ({a.debug}): {"ok: " + line if line else "nothing after the reset"}')
        rig.debug.wait(r'USB configured', start, 20)
        for l in rig.debug.since(start):
            print('   ', l)
        ok = rig.usb.open()
        print(f'cli port ({a.usb or find_board_port()}): {"open" if ok else "not found"}')
        if ok:
            rig.usb.write('\n')
            print('LiteForth:', 'answers' if rig.usb.wait('ok>', 10) else 'no ok>')
            rig.usb.close()
        return 0 if line and ok else 1
    if a.what == 'run':
        need = {'install': [a.good], 'rollback': [a.hang], 'recovery': [a.good], 'lockdown': []}
        todo = [(n, f) for n, f in SCENARIOS if not a.only or n == a.only]
        for n, f in todo:
            if not all(need[n]):
                p.error(f'{n} needs --good and --hang packages')
        note('# Stage 1 on the board (hiltest.py)\n')
        note(f'Run {time.strftime("%Y-%m-%d %H:%M:%S")}; debug port {a.debug}, cli port '
             f'{a.usb or find_board_port()}, reset by {f"`{a.reset_cmd}`" if a.reset_cmd else "`reboot`"}.\n')
        for name, path in (('GOOD', a.good), ('HANG', a.hang)):
            if path:
                note(f'- {name}: `{os.path.basename(path)}`, sha256 {sha(path)}')
        for n, f in todo:
            print(f'{n}:')
            rig.section(n, inspect.cleandoc(f.__doc__))
            f(rig, a)
        builds = [l for l in rig.debug.lines if 'built' in l]
        if builds:
            note(f'\nBuild: `{builds[-1]}`')
        note(f'\n**{"All passed" if not failures else f"{failures} failed"}.**')
        with open(a.report, 'w', encoding='utf-8') as f:
            f.write(''.join(report))
        print(f'{"all passed" if not failures else f"{failures} failed"}; report in {a.report}')
        return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
