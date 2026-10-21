#!/usr/bin/env python3
"""Plays stage 1's scenarios on the simulated board (sim.c): installs,
trials, rollbacks, refusals, recovery over USB (from the card and from
RAM), recovery on request (Ctrl+X x3 twice), the lock-down check and
security level 2's lock-down, and a power cut at every flash and card write of an install.
Each scenario starts from a fresh board: flash with an adopted V5F image,
a card made by mkexfat.py.

    test_stage1.py [--report FILE] [--only NAME] [--quick]

--report writes every scenario's steps and the board's output word for
word (Markdown), as evidence; --quick cuts the power at every 5th step
only. Exit status 0 if everything passed.
"""
import contextlib
import hashlib
import inspect
import io
import json
import os
import queue
import re
import shutil
import signal
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..', '..', '..'))
WORK = os.path.join(HERE, 'work')
sys.path.insert(0, os.path.join(ROOT, 'scripts', 'admin'))
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
sys.path.insert(0, os.path.join(HERE, '..', 'ymodem'))
import sealblocks as sb  # noqa: E402
from sender import packet, header, packets, EOT, ACK, NAK, CAN, C  # noqa: E402

UID = '0102030405060708'
PRODUCT = 0x4C46
SLOT = 0x20000
V5F_ADDR = 0x10000
FLASH_SIZE = 0x100000
ERASED = bytes.fromhex('39E339E3') * (FLASH_SIZE // 4)    # 0xE339E339, little-endian
LOCKED_OBR = '%X' % (0x2 | (0x01 << 2))  # read protection on, downloads off
LOCKED_WPR = 'FFFFFF00'                  # the V3F's 64 KB protected
TIMEOUT = 20

failures = 0
checks = 0
report = []


def check(cond, what):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print('FAIL:', what)
        report.append(f'**FAIL:** {what}\n')
    return cond


def note(text):
    report.append(text + '\n')


# ---------------------------------------------------------------------------
# Keys, images and packages
# ---------------------------------------------------------------------------
def tool(*args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        status = sb.main([str(a) for a in args])
    if status != 0:
        sys.exit(f'test_stage1.py: sealblocks {args}: {out.getvalue()}')


def image(tag, size, seed):
    body = bytes((i * seed + (i >> 8)) & 255 for i in range(size - len(tag)))
    return tag + body


IMAGES = {
    'v1': image(b'SIMV5F:OK v1\0', 9000, 5),
    'v2hang': bytes.fromhex('6f000000') * 1024,      # `j .`: never starts
    'v3': image(b'SIMV5F:OK v3\0', 30000, 7),
    'stuck': image(b'SIMV5F:STUCK\0', 6000, 3),
}
PACKAGES = {}


def gen():
    if os.path.isdir(WORK):
        shutil.rmtree(WORK)
    os.makedirs(WORK)
    cwd = os.getcwd()
    os.chdir(WORK)
    try:
        tool('root-key', 'root')
        tool('release-key', 'root.key', 1, 'rel.key', '--product', PRODUCT)
        tool('board-key', 'board.key', '--uid', UID, '--insecure')
        tool('board-key', 'other.key', '--uid', '1112131415161718')
        for name, key in (('boards.csv', 'board.key'), ('other.csv', 'other.key')):
            with open(key) as f:
                k = json.load(f)
            with open(name, 'w') as f:
                f.write(f'{k["uid"]},{k["public"]},{name}\n')
        tool('root-header', 'root.pub', '--product', PRODUCT, '-o', 'sealed_root.h')
        with open('sim_root.h', 'w') as f:
            f.write('/* the test root key; below SECURITY_LEVEL 2 the board key comes from the UID */\n'
                    '#include "sealed_root.h"\n')
        for name, data in IMAGES.items():
            with open(name + '.bin', 'wb') as f:
                f.write(data)
        v5f = ['--component', 'v5f', '--release', 'rel.key', '--boards', 'boards.csv']
        for name, rev in (('v1', 1), ('v2hang', 2), ('v3', 3), ('stuck', 4)):
            tool('package', name + '.bin', name + '.lfu', *v5f, '--revision', rev)
        tool('package', 'v3.bin', 'other.lfu', '--component', 'v5f', '--release', 'rel.key',
             '--boards', 'other.csv', '--revision', 3)
        for name in ('v1', 'v2hang', 'v3', 'stuck', 'other'):
            PACKAGES[name] = os.path.join(WORK, name + '.lfu')
        cards = {
            'full': ['--file', 'lfblocks.fb4', '--file', 'lfstage.bin,size=1048576',
                     '--file', 'lfbackup.bin,size=262144'],
            'nobackup': ['--file', 'lfblocks.fb4', '--file', 'lfstage.bin,size=1048576'],
            'nostage': ['--file', 'lfblocks.fb4', '--file', 'lfbackup.bin,size=262144'],
        }
        for name, args in cards.items():
            subprocess.check_call([sys.executable, os.path.join(ROOT, 'scripts', 'mkexfat.py'),
                                   f'card_{name}.img', '--size-mb', '8', *args],
                                  stdout=subprocess.DEVNULL)
        flash = bytearray(ERASED)
        adopted = b'SIMV5F:OK v0\0'             # what the debugger left in the slot
        flash[V5F_ADDR:V5F_ADDR + len(adopted)] = adopted
        with open('flash0.bin', 'wb') as f:
            f.write(flash)
    finally:
        os.chdir(cwd)
    subprocess.check_call(['make', '-s', 'sims'], cwd=HERE)


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


# ---------------------------------------------------------------------------
# The board
# ---------------------------------------------------------------------------
class Run:
    """One boot of the simulated board, from reset to its next reset."""

    def __init__(self, board, env, strict):
        self.board = board
        exe = os.path.join(WORK, 'sim_strict' if strict else 'sim')
        self.p = subprocess.Popen([exe], cwd=board.dir, env=env, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.usb = queue.Queue()
        self.debug = []                 # lines of the debug UART
        self.debug_cv = threading.Condition()
        self.usb_text = bytearray()
        threading.Thread(target=self._read_usb, daemon=True).start()
        threading.Thread(target=self._read_debug, daemon=True).start()

    def _read_usb(self):
        fd = self.p.stdout.fileno()
        while True:
            b = os.read(fd, 4096)
            if not b:
                break
            for c in b:
                self.usb.put(c)

    def _read_debug(self):
        for line in self.p.stderr:
            line = line.decode('utf-8', 'replace').rstrip('\r\n')
            with self.debug_cv:
                self.debug.append(line)
                self.debug_cv.notify_all()

    def wait_debug(self, pattern, timeout=TIMEOUT):
        """Waits for a debug line matching pattern; returns it, or None."""
        end = time.time() + timeout
        rx = re.compile(pattern)
        seen = 0
        with self.debug_cv:
            while True:
                for line in self.debug[seen:]:
                    if rx.search(line):
                        return line
                seen = len(self.debug)
                left = end - time.time()
                if left <= 0 or (self.p.poll() is not None and seen == len(self.debug)):
                    if self.p.poll() is not None:
                        time.sleep(0.05)
                        if len(self.debug) > seen:
                            continue
                    return None
                self.debug_cv.wait(min(left, 0.1))

    def usb_byte(self, timeout):
        try:
            c = self.usb.get(timeout=timeout)
        except queue.Empty:
            return None
        self.usb_text.append(c)
        return c

    def wait_usb(self, text, timeout=TIMEOUT):
        """Reads USB output until it ends with text; returns all of it, or None."""
        end = time.time() + timeout
        start = len(self.usb_text)
        want = text.encode()
        while time.time() < end:
            if self.usb_byte(0.1) is not None and bytes(self.usb_text[start:]).endswith(want):
                return bytes(self.usb_text[start:]).decode('latin-1')
        return None

    def send(self, data):
        if isinstance(data, str):
            data = data.encode()
        self.p.stdin.write(data)
        self.p.stdin.flush()

    def expect_byte(self, wanted, timeout=10):
        end = time.time() + timeout
        while time.time() < end:
            c = self.usb_byte(0.1)
            if c in wanted:
                return c
        return None

    def ymodem(self, path):
        """kwai's ~x FILE -2: types `-2 ymodem`, then sends FILE. Returns
        whether the receiver took it all."""
        with open(path, 'rb') as f:
            data = f.read()
        self.send('-2 ymodem\n')
        c = self.expect_byte({C, CAN}, 30)
        if c != C:
            return False
        self.send(header(os.path.basename(path), data))
        if self.expect_byte({ACK, CAN}) != ACK or self.expect_byte({C, CAN}) != C:
            return False
        for p in packets(data, 1024):
            self.send(p)
            if self.expect_byte({ACK, NAK, CAN}) != ACK:
                return False
        self.send(bytes([EOT]))
        self.expect_byte({ACK, NAK})
        self.send(bytes([EOT]))
        if self.expect_byte({ACK, NAK}) != ACK or self.expect_byte({C}) != C:
            return False
        self.send(packet(0, bytes(128)))
        return self.expect_byte({ACK, CAN}) == ACK

    def wait_exit(self, timeout=TIMEOUT):
        try:
            return self.p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            return None

    def stop(self):
        """The reset button (or the power switch, with power_cut)."""
        if self.p.poll() is None:
            self.p.send_signal(signal.SIGKILL)
            self.p.wait()
        time.sleep(0.05)                # let the readers finish
        self.board.last = self
        while True:                     # what the host hasn't read yet
            try:
                self.usb_text.append(self.usb.get_nowait())
            except queue.Empty:
                break
        self.board.log_run(self)


class Board:
    def __init__(self, name, card='full'):
        self.name = name
        self.dir = os.path.join(WORK, 'boards', name)
        if os.path.isdir(self.dir):
            shutil.rmtree(self.dir)
        os.makedirs(self.dir)
        shutil.copy(os.path.join(WORK, 'flash0.bin'), os.path.join(self.dir, 'flash.bin'))
        if card:
            shutil.copy(os.path.join(WORK, f'card_{card}.img'), os.path.join(self.dir, 'card.img'))
        self.power_on()
        self.env = dict(os.environ, SIM_UID=UID)
        self.boots = 0
        note(f'\n### {name}\n')
        note(f'Card: {card or "none"}.\n')

    def power_on(self):
        """SRAM loses what the shared block held."""
        with open(os.path.join(self.dir, 'shared.bin'), 'wb') as f:
            f.write(bytes(0x8000))

    def sim(self, *args):
        r = subprocess.run([os.path.join(WORK, 'sim'), *args], cwd=self.dir, env=self.env,
                           capture_output=True, text=True)
        shown = ' '.join(os.path.basename(x) for x in args)
        note(f'- `sim {shown}`: {r.stderr.strip()}')
        return r.returncode

    def stage(self, pkg):
        """What the V5F's `-2 ymodem` leaves: the package in lfstage.bin and the flag."""
        return self.sim('--stage', PACKAGES[pkg])

    def zap(self, name):
        return self.sim('--zap', name)

    def boot(self, strict=False, **env):
        e = dict(self.env)
        for k, v in env.items():
            e['SIM_' + k.upper()] = str(v)
        self.boots += 1
        return Run(self, e, strict)

    def log_run(self, run):
        note(f'Boot {self.boots}, debug UART:\n\n```\n' + '\n'.join(run.debug) + '\n```')
        usb = bytes(run.usb_text).decode('latin-1')
        usb = ''.join(ch if ch in '\r\n' or ' ' <= ch < '\x7f' else f'<{ord(ch):02X}>' for ch in usb)
        usb = re.sub(r'(<[0-9A-F]{2}>){8,}', '<YMODEM>', usb)
        if usb.strip():
            note('USB terminal:\n\n```\n' + usb.replace('\r', '').strip('\n') + '\n```')

    def slot(self, n):
        with open(os.path.join(self.dir, 'flash.bin'), 'rb') as f:
            f.seek(V5F_ADDR)
            return f.read(n)

    def has(self, name):
        data = IMAGES[name]
        return self.slot(len(data)) == data

    def boot_until(self, pattern, **kw):
        """Boots, waits for a debug line, presses reset. Returns the line."""
        r = self.boot(**kw)
        line = r.wait_debug(pattern)
        r.stop()
        return line


def until_started(board, **kw):
    return board.boot_until(r'V5F: LiteForth starting|resetting the chip|recovery mode', **kw)


# ---------------------------------------------------------------------------
# The scenarios
# ---------------------------------------------------------------------------
def s_fresh():
    """A board that was never updated boots as before: no update lines."""
    b = Board('fresh')
    r = b.boot()
    check(r.wait_debug(r'V5F: LiteForth starting') is not None, 'fresh: LiteForth starts')
    r.stop()
    check(not any('update' in l for l in r.debug), 'fresh: no update lines')
    check(any('not locked down: read protection off' in l for l in r.debug),
          'fresh: the lock-down warning')


def s_install():
    """B3: a package installs, starts on trial, and LiteForth confirms it."""
    b = Board('install')
    b.stage('v1')
    r = b.boot()
    check(r.wait_debug(r'version 1 installed, on trial') is not None, 'install: installed')
    check(r.wait_debug(r'version 1 confirmed') is not None, 'install: confirmed')
    r.stop()
    check(b.has('v1'), 'install: v1 in flash')
    line = until_started(b)
    check(line and 'LiteForth' in line, 'install: the next boot starts LiteForth')


def s_rollback():
    """B4: an image that never starts is rolled back after three trial boots."""
    b = Board('rollback')
    b.stage('v1')
    until_started(b)
    b.stage('v2hang')
    check(b.boot_until(r'version 2 installed') is not None, 'rollback: v2 installed')
    for i in (1, 2, 3):
        check(b.boot_until(rf'boot {i} of 3') is not None, f'rollback: trial boot {i}')
    r = b.boot()
    check(r.wait_debug(r'rolled back to version 1') is not None, 'rollback: rolled back')
    check(r.wait_debug(r'V5F: LiteForth starting') is not None, 'rollback: v1 runs')
    r.stop()
    check(b.has('v1'), 'rollback: v1 in flash again')


def s_rollback_unattended():
    """An image that never starts: the V3F resets the chip after
    STAGE1_V5F_START_MS each time, so the trial runs out and the old image
    comes back with nobody pressing reset."""
    b = Board('rollback-unattended')
    b.stage('v1')
    until_started(b)
    b.stage('v2hang')
    boots, line = 0, None
    while boots < 8:
        r = b.boot()
        line = r.wait_debug(r'V5F: LiteForth starting|recovery mode')
        code = r.wait_exit(0.5)
        r.stop()
        boots += 1
        if line:
            break
        check(code == 2, f'unattended: boot {boots} ends in a chip reset ({code})')
    check(line and 'LiteForth' in line, f'unattended: LiteForth runs again ({line})')
    check(boots == 5, f'unattended: the install, three trial boots, the rollback ({boots} boots)')
    check(any('rolled back to version 1' in l for l in b.last.debug) and b.has('v1'),
          'unattended: v1 rolled back')


def s_nostart():
    """An installed, confirmed image that stops starting: after
    STAGE1_START_TRIES boots of it, recovery, where a package replaces it."""
    b = Board('nostart')
    b.stage('v1')
    until_started(b)
    for i in (1, 2, 3):
        r = b.boot(v5f='dead')
        check(r.wait_debug(rf"didn't start within .*\({i} in a row\)") is not None,
              f'nostart: boot {i} times out')
        check(r.wait_exit() == 2, f'nostart: boot {i} resets the chip')
        r.stop()
    r = b.boot(v5f='dead')
    check(r.wait_debug(r"the V5F didn't start, 3 boots in a row") is not None, 'nostart: recovery')
    check(r.wait_usb('recovery ok> ') is not None, 'nostart: prompt')
    check(r.ymodem(PACKAGES['v3']), 'nostart: v3 sent')
    check(r.wait_debug(r'marked as not working') is not None, 'nostart: the image marked')
    check(r.wait_debug(r'version 3 installed') is not None, 'nostart: installed')
    check(r.wait_exit() == 2, 'nostart: reset')
    r.stop()
    r = b.boot()
    check(r.wait_debug(r'version 3 confirmed') is not None, 'nostart: confirmed')
    r.stop()
    check(b.has('v3'), 'nostart: v3 in flash')


def s_reboot():
    """`reboot` in LiteForth resets the chip, so a staged package installs."""
    b = Board('reboot')
    r = b.boot()
    r.wait_debug(r'LiteForth starting')
    b.stage('v1')                       # while it runs, as `-2 ymodem` does
    r.send('reboot\n')
    check(r.wait_exit() == 2, 'reboot: the chip resets')
    r.stop()
    r = b.boot()
    check(r.wait_debug(r'version 1 installed') is not None, 'reboot: the package installs')
    r.stop()


def s_refusals():
    """A package for another board, one with a damaged block, and no room
    for a backup: refused, the image untouched, LiteForth starts."""
    b = Board('refusals')
    b.stage('v1')
    until_started(b)
    b.stage('other')
    r = b.boot()
    check(r.wait_debug(r'refused') is not None, 'refusals: another board\'s package')
    check(r.wait_debug(r'LiteForth starting') is not None, 'refusals: still starts')
    r.stop()
    check(b.has('v1'), 'refusals: v1 untouched')

    b = Board('no-backup', card='nobackup')
    b.stage('v1')
    line = b.boot_until(r'refused|installed')
    check(line and 'lfbackup.bin needs 33 blocks' in line, f'no backup: refused ({line})')


def s_recovery_card():
    """B5: no backup to roll back to, so recovery; a package over USB into
    lfstage.bin installs without one."""
    b = Board('recovery-card')
    b.stage('v1')
    until_started(b)
    b.stage('v2hang')
    b.boot_until(r'version 2 installed')
    b.zap('lfbackup.bin')
    for i in (1, 2, 3):
        b.boot_until(rf'boot {i} of 3')
    r = b.boot()
    check(r.wait_debug(r"can't roll back") is not None, 'recovery: no backup')
    check(r.wait_debug(r'recovery mode') is not None, 'recovery: entered')
    check(r.wait_usb('recovery ok> ') is not None, 'recovery: prompt on USB')
    check(r.ymodem(PACKAGES['v1']), 'recovery: v1 sent')
    check(r.wait_debug(r'version 1 installed') is not None, 'recovery: v1 installed')
    check(r.wait_exit() == 2, 'recovery: the chip resets to try it')
    r.stop()
    r = b.boot()
    check(r.wait_debug(r'version 1 on trial, boot 1 of 3') is not None, 'recovery: trial boot')
    check(r.wait_debug(r'version 1 confirmed') is not None, 'recovery: confirmed')
    r.stop()
    check(b.has('v1'), 'recovery: v1 in flash')


def s_recovery_ram_and_ctrl_x():
    """Recovery on request (Ctrl+X x3 twice, the V5F stuck before
    LiteForth), and with no lfstage.bin the package goes into RAM."""
    b = Board('recovery-ram', card='nostage')
    r = b.boot(v5f='stuck')
    check(r.wait_debug(r'V5F stuck') is not None, 'ctrl+x: the V5F gets stuck')
    r.send(b'\x18\x18\x18')
    check(r.wait_debug(r'resetting the chip; the V5F starts in safe mode') is not None,
          'ctrl+x: safe restart')
    check(r.wait_exit() == 2, 'ctrl+x: by a chip reset')
    r.stop()
    r = b.boot(v5f='stuck')                 # safe_boot survived the reset
    check(r.wait_debug(r'V5F stuck') is not None, 'ctrl+x: it restarted, stuck again')
    r.send(b'\x18\x18\x18')
    check(r.wait_debug(r'the V5F is stuck; resetting into recovery') is not None,
          'ctrl+x: again: recovery')
    check(r.wait_exit() == 2, 'ctrl+x: the chip resets')
    r.stop()
    r = b.boot(v5f='stuck')
    check(r.wait_debug(r'recovery mode') is not None, 'ctrl+x: recovery after the reset')
    check(r.wait_usb('recovery ok> ') is not None, 'ctrl+x: prompt')
    check(r.ymodem(PACKAGES['v1']), 'ram: v1 sent')
    check(r.wait_debug(r'the package goes into RAM') is not None, 'ram: into RAM')
    check(r.wait_debug(r'marked as not working') is not None, 'ram: the stuck image marked')
    check(r.wait_debug(r'version 1 installed') is not None, 'ram: installed')
    check(r.wait_exit() == 2, 'ram: reset')
    r.stop()
    r = b.boot()
    check(r.wait_debug(r'version 1 confirmed') is not None, 'ram: confirmed')
    r.stop()
    check(b.has('v1'), 'ram: v1 in flash')

    b = Board('ctrl-x-healthy')             # a healthy V5F: just a safe restart, every time
    r = b.boot()
    r.wait_debug(r'LiteForth starting')
    debug = []
    for _ in range(2):
        r.send(b'\x18\x18\x18')
        check(r.wait_exit() == 2, 'healthy: a chip reset')
        r.stop()
        debug += r.debug
        r = b.boot()
        check(r.wait_debug(r'safe boot') is not None, 'healthy: safe boot')
    r.stop()
    debug += r.debug
    check(not any('recovery' in l for l in debug), 'healthy: never recovery')


def s_recovery_commands():
    """In recovery: an unknown line gets the help, and a refused package
    leaves the board in recovery."""
    b = Board('recovery-commands', card='nostage')
    b.stage  # (no lfstage.bin on this card)
    r = b.boot(v5f='stuck')
    r.wait_debug(r'V5F stuck')
    r.send(b'\x18\x18\x18')
    r.wait_exit()
    r.stop()
    r = b.boot(v5f='stuck')
    r.wait_debug(r'V5F stuck')
    r.send(b'\x18\x18\x18')
    r.wait_exit()
    r.stop()
    r = b.boot(v5f='stuck')
    r.wait_usb('recovery ok> ')
    r.send('hello\n')
    check(r.wait_usb('recovery ok> ') is not None and 'reset: start again' in
          bytes(r.usb_text).decode('latin-1'), 'commands: help')
    check(r.ymodem(PACKAGES['other']), 'commands: another board\'s package sent')
    check(r.wait_debug(r'refused') is not None, 'commands: refused')
    r.send('reset\n')
    check(r.wait_exit() == 2, 'commands: reset')
    r.stop()
    line = until_started(b)
    check(line and 'recovery mode' in line, f'commands: still in recovery ({line})')


def s_lockdown():
    """The lock-down check, and security level 2 (no V5F on an open board)."""
    b = Board('lockdown')
    r = b.boot(obr=LOCKED_OBR, wpr=LOCKED_WPR)
    check(r.wait_debug(r'LiteForth starting') is not None, 'locked: starts')
    r.stop()
    check(not any('security' in l for l in r.debug), 'locked: no warning')
    for obr, wpr, what in ((LOCKED_OBR, 'FFFFFFFF', "V3F's sectors not all write-protected"),
                           (LOCKED_OBR, 'FFFFFE00', 'sectors that updates write'),
                           ('%X' % (0x2 | (0x41 << 2)), LOCKED_WPR, 'USB download on'),
                           ('%X' % (0x81 << 2), LOCKED_WPR, 'read protection off')):
        line = b.boot_until(r'security', obr=obr, wpr=wpr)
        check(line and what in line, f'lockdown: {what} ({line})')
    r = b.boot(strict=True)
    check(r.wait_debug(r'security level 2') is not None or r.wait_debug(r'recovery mode') is not None,
          'strict: recovery')
    check(r.wait_usb('recovery ok> ') is not None, 'strict: prompt')
    r.send('-2 ymodem\n')
    check(r.expect_byte({CAN}, 5) == CAN, 'strict: a package refused')
    r.stop()
    check(not any('LiteForth starting' in l for l in r.debug), 'strict: the V5F never starts')
    line = b.boot_until(r'LiteForth starting|recovery', strict=True, obr=LOCKED_OBR, wpr=LOCKED_WPR)
    check(line and 'LiteForth' in line, 'strict: a locked board starts')


def s_lockdown_command():
    """Level 2's `lockdown` in the recovery console of an open board: it
    says what it will do, `lockdown yes` programs the option bytes and
    resets, and the board then starts locked. Below level 2 there's no
    such command."""
    b = Board('lockdown-command')
    ob = os.path.join(b.dir, 'ob.bin')
    r = b.boot(strict=True)
    got = r.wait_usb('recovery ok> ')
    check(got is not None and 'lockdown: lock this board' in got, 'lockdown: offered in help')
    r.send('lockdown\n')
    got = r.wait_usb('recovery ok> ')
    check(got is not None and 'lockdown yes:' in got and not os.path.exists(ob),
          'lockdown: says what it will do, changes nothing')
    r.send('lockdown yes\n')
    check(r.wait_exit() == 2, 'lockdown yes: resets')
    line = r.wait_debug(r'option bytes now')
    r.stop()
    check(line and 'RDPR 00FF USER 3E01 WRPR0 0000: ok' in line,
          f'lockdown yes: option bytes as the check wants ({line})')
    r = b.boot(strict=True)
    check(r.wait_debug(r'LiteForth starting') is not None, 'locked: the V5F starts')
    r.stop()
    check(not any('not locked down' in l for l in r.debug), 'locked: no warning')
    b2 = Board('lockdown-level0')       # level 0: recovery by Ctrl+X x3 twice
    r = b2.boot(v5f='stuck')
    r.wait_debug(r'V5F stuck')
    r.send(b'\x18\x18\x18')
    r.wait_exit()
    r.stop()
    r = b2.boot(v5f='stuck')
    r.wait_debug(r'V5F stuck')
    r.send(b'\x18\x18\x18')
    r.wait_exit()
    r.stop()
    r = b2.boot(v5f='stuck')
    r.wait_usb('recovery ok> ')
    r.send('lockdown yes\n')
    got = r.wait_usb('recovery ok> ')
    r.stop()
    check(got is not None and 'lockdown' not in got.split('lockdown yes', 1)[-1]
          and not os.path.exists(os.path.join(b2.dir, 'ob.bin')), f'level 0: no lockdown ({got!r})')


def s_power_cuts(stride):
    """A power cut at every flash and card write of an install (v3 over the
    adopted image): after it, the board ends up running the old image or
    the new one, never anything else."""
    template = Board('power-template')
    template.stage('v3')
    files = ('flash.bin', 'card.img', 'shared.bin')
    saved = {f: open(os.path.join(template.dir, f), 'rb').read() for f in files}
    k, old, new, tried = 0, 0, 0, 0
    while True:
        b = Board(f'power-{k}')
        for f in files:
            with open(os.path.join(b.dir, f), 'wb') as out:
                out.write(saved[f])
        r = b.boot(cut_at=k)
        line = r.wait_debug(r'SIM: power cut|LiteForth starting|recovery mode')
        cut = r.wait_exit(2) == 3
        r.stop()
        if not cut:                         # past the last write: done
            break
        tried += 1
        b.power_on()
        for _ in range(4):
            line = until_started(b)
            if line and 'LiteForth' in line:
                break
        if b.has('v3'):
            new += 1
        elif b.slot(13) == b'SIMV5F:OK v0\0':
            old += 1
        else:
            check(False, f'power cut at step {k}: neither image ({line})')
        check(line and 'LiteForth' in line, f'power cut at step {k}: LiteForth starts ({line})')
        k += stride
    check(tried > 20, f'power cuts: {tried} tried')
    note(f'\nPower cuts: {tried} cut points tried; {new} ended with the new image, {old} with the old.')
    print(f'power cuts: {tried} tried, {new} new, {old} old')


SCENARIOS = [s_fresh, s_install, s_rollback, s_rollback_unattended, s_nostart, s_reboot,
             s_refusals, s_recovery_card,
             s_recovery_ram_and_ctrl_x, s_recovery_commands, s_lockdown, s_lockdown_command]


def main():
    args = sys.argv[1:]
    out = None
    only = None
    quick = '--quick' in args
    if '--report' in args:
        out = args[args.index('--report') + 1]
    if '--only' in args:
        only = args[args.index('--only') + 1]
    gen()
    note('# Stage 1 on the simulated board\n')
    try:
        rev = subprocess.check_output(['git', 'describe', '--always', '--dirty'], cwd=ROOT,
                                      text=True).strip()
    except Exception:
        rev = 'unknown'
    note(f'Built from `{rev}`, {time.strftime("%Y-%m-%d %H:%M:%S")}. '
         'Packages (sha256):\n')
    for name, path in sorted(PACKAGES.items()):
        note(f'- `{name}.lfu` {sha(path)}')
    def s_power_cuts_install():
        s_power_cuts(5 if quick else 1)
    s_power_cuts_install.__doc__ = s_power_cuts.__doc__
    for s in SCENARIOS + [s_power_cuts_install]:
        name = s.__name__
        if only and only not in name:
            continue
        doc = inspect.cleandoc(s.__doc__ or 'Power cuts')
        note(f'\n## {name}\n\n{doc}\n')
        before = failures
        t = time.time()
        s()
        print(f'{name}: {"ok" if failures == before else "FAILED"} ({time.time() - t:.1f} s)')
    if out:
        with open(out, 'w') as f:
            f.write(''.join(report))
    if failures:
        print(f'stage1sim tests: {failures} of {checks} checks failed')
        return 1
    print(f'stage1sim tests passed ({checks} checks)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
