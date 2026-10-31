#!/usr/bin/env python3
"""Turnkey root-of-trust hardness test for a locked CH32H417 board, for a
testing lab: one command that proves as much of the RoT's hardness as can
be shown without opening the chip or holding the signing keys.

    hardness.py --list
    hardness.py check [--debug PORT] [--cli PORT]
    hardness.py run --good GOOD.lfu [--hang HANG.lfu] [--option-bytes] [...]

It proves hardness two ways, and only these two (see "Out of scope" below):

  - it READS the board's locked state and checks it is as a shipped board
    should be: the boot's own report of the security level, the lock-down,
    and the PMP (all four entries set and locked); and, only with
    --option-bytes, the option bytes over wlink (read protection on, the
    boot-ROM downloads off, the write-protect mask set). The option-byte
    read is off by default and runs last, because wlink halts the core and
    resets the chip, which on a locked board needs a power cycle before the
    serial ports come back -- so the default run stays repeatable;

  - it THROWS BAD INPUTS at the one surface a field attacker has, a
    firmware update package over the terminal, and checks the board refuses
    every one and still boots its good image afterward. The bad packages
    are made here by corrupting the lab's own GOOD.lfu -- a flipped byte in
    the release signature, the image hash, the machine-mode root signature
    or a content block, a wrong magic, a truncated file -- so NO private
    key is needed.

It never tries to defeat a protection from the inside (no poke at the PMP,
no privilege-escalation probe): a shipped board is the attacker's, so the
proof that a fence holds is that its locked state reads back as set, not
that a probe bounced off it.

It is built on hiltest.py (the same two ports, the same YMODEM and report),
so it needs what hiltest needs: the cli port (the board's USB terminal,
1A86:FE0C), the debug port (the WCH-Link's UART), the card in the board with
lfstage.bin and lfbackup.bin, and pyserial. --option-bytes also needs a
WCH-Link (wlink, through lockdown.py). Point it at a LOCKED, level-2 board
with its good image installed: the signature and lock-down checks only mean
something there, and the battery needs a good image to fall back to.

GOOD.lfu is a valid update package for this board (sealblocks.py package,
the one provisioning installed). --hang HANG.lfu (an image that never
starts, as in hiltest) adds the rollback-survival check.

The report (--report, default hardness-DATE.md) records every check, the
board's output word for word, and the out-of-scope list, ready for a lab
file or doc/admin/board-tests.md. Exit status 0 if every check passed.

Out of scope, and why:
  - Physical attacks -- voltage or clock glitching, side channels, fault
    injection, decapsulation. These need the bench, not a script; the
    per-board keys limit any one extraction to that board.
  - Attacks that need the root or release PRIVATE key -- re-signing a
    changed image, a version or serial downgrade. These rest on key
    custody (the keys never leave the signing host) and are exercised in
    the desktop tests (unit_tests/seal, unit_tests/update), not here.
"""
import argparse
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))       # scripts/ (lfmcp)
sys.path.insert(0, HERE)                         # scripts/admin (hiltest, lockdown)


def deps():
    """hiltest and lockdown, imported on use so the pure helpers below (and
    their test) don't need pyserial. hiltest imports serial at its top."""
    try:
        import hiltest
        import lockdown
        return hiltest, lockdown
    except ImportError as e:
        sys.exit(f'hardness.py needs pyserial and hiltest.py/lockdown.py beside it: {e}\n'
                 'install pyserial: py -m pip install pyserial')


# ---------------------------------------------------------------------------
# The sealed package's block 0 (doc/admin/encrypted-blocks.md section 3;
# sealblocks.py keeps the authoritative copy). Only the offsets a corruption
# needs are here; test_hardness.py asserts they still match sealblocks.py.
# ---------------------------------------------------------------------------
BLOCK = 4096
SIGNATURE = b'LITEFORTH'                 # block 0's first 9 bytes
PACKAGE_MARK = b'F'                      # data[9] for an update package
SIG_AT = BLOCK - 64                      # the release key's Ed25519 signature (4032)
IMAGE_HASH_AT = SIG_AT - 32              # the plaintext image hash (4000)
MMODE_SIG_AT = IMAGE_HASH_AT - 104       # the machine-mode region's root signature (3896):
#                                          addr(4) len(4) hash(32) sig(64); the sig is at +40


def flip(data, at):
    """A copy of data with one bit flipped at byte `at`"""
    b = bytearray(data)
    b[at] ^= 0x01
    return bytes(b)


# Each corruption: (name, what it breaks, where the board should catch it,
# a function from the good package's bytes to the bad package's bytes).
# "stage" = refused by `-2 ymodem`/blk_stage_check before anything installs;
# "boot" = staged, then refused by stage 1 at the next boot (level 2).
CORRUPTIONS = [
    ('wrong-magic', "block 0's package magic", 'stage',
     lambda d: bytes(d[:9]) + b'\x00' + d[10:]),
    ('truncated', 'the file cut mid-block (not whole 4 KB blocks)', 'stage',
     lambda d: d[:len(d) - BLOCK // 2]),
    ('short', 'a whole block dropped from the end (its hash table no longer fits)', 'stage',
     lambda d: d[:len(d) - BLOCK]),
    ('release-sig', 'the release signature (one flipped bit at 4032)', 'stage',
     lambda d: flip(d, SIG_AT)),
    ('image-hash', 'the image hash (one flipped bit at 4000)', 'stage',
     lambda d: flip(d, IMAGE_HASH_AT)),
    ('mmode-root-sig', 'the machine-mode root signature (one flipped bit at 3936)', 'either',
     lambda d: flip(d, MMODE_SIG_AT + 40)),
    ('content-block', 'a content block (one flipped bit in block 1)', 'stage',
     lambda d: flip(d, BLOCK + 37)),
]


def make_bad(good, outdir):
    """Writes every corrupted package into outdir; returns [(name, what, where, path)]."""
    with open(good, 'rb') as f:
        data = f.read()
    out = []
    for name, what, where, fn in CORRUPTIONS:
        path = os.path.join(outdir, f'bad-{name}.lfu')
        with open(path, 'wb') as f:
            f.write(fn(data))
        out.append((name, what, where, path))
    return out


# ---------------------------------------------------------------------------
# The locked-state readbacks
# ---------------------------------------------------------------------------
def read_option_bytes(a):
    """(obr, wpr, text) through wlink, or (None, None, reason). wlink halts
    the core to read, so the core is resumed and the chip reset after."""
    _, lockdown = deps()
    try:
        wl = lockdown.Probe(a.wlink)
    except Exception as e:                      # noqa: BLE001 -- report any probe trouble
        return None, None, f'no wlink ({e})'
    try:
        text, uid, prot = lockdown.status(wl)
        obr, wpr = wl.option_bytes()
        return obr, wpr, text
    except OSError as e:
        return None, None, f'wlink could not read the option bytes ({e})'
    finally:
        try:
            lockdown.release(wl, True)          # resume the core and reset, so it runs again
        except Exception:                       # noqa: BLE001
            pass


def h_boot_config(rig, a):
    """What the board reports about itself at boot, read on the debug port
    (no wlink, nothing touched): the security level, the lock-down, and the
    PMP set and locked. The option bytes are read over wlink in the last
    phase, since that halts the core."""
    hiltest, _ = deps()
    check = hiltest.check
    line, start = rig.reset_and_wait(r'V5F PMP:|LiteForth starting|recovery mode', 40)
    if line is None:
        sys.exit('hardness.py: no response from the board after a reset (debug port). Is it '
                 'powered with the card in, kwai and any other terminal closed, and the ports '
                 'right? (`hardness.py --list`)')
    if 'recovery' in line:
        print('  ---- note: the board is in recovery (no good image installed); the battery '
              'needs a good image to fall back to -- install --good first')
    rig.debug.wait(r'LiteForth starting|recovery mode', start, 25)
    lines = rig.debug.since(start)
    joined = '\n'.join(lines)
    check(any('security level 2' in l for l in lines), 'the boot reports security level 2')
    check('not locked down' not in joined, 'the boot does not report "not locked down"')
    pmp = next((l for l in lines if l.startswith('V5F PMP:')), None)
    check(pmp is not None and 'as set' in pmp and 'locked' in pmp and 'NOT locked' not in pmp,
          f'the PMP reads back set and locked ({pmp})')
    rig.log(start)


def h_option_bytes(rig, a):
    """The option bytes, read over wlink: read protection on, the boot-ROM
    downloads off, the write-protect mask set. Last of the phases, because
    wlink halts the core to read and resets the chip after, so nothing
    serial should follow it."""
    hiltest, lockdown = deps()
    note, check = hiltest.note, hiltest.check
    obr, wpr, text = read_option_bytes(a)
    if obr is None:
        note(f'- (option bytes not read over wlink: {text})')
        print(f'  ---- option bytes not read: {text}')
        return
    note('wlink:\n\n```\n' + text.strip() + '\n```')
    note(f'- {lockdown.ob_text(obr, wpr)}')
    check(lockdown.locked(obr, wpr),
          'the option bytes are locked down (read protection on, downloads off, '
          'write-protect set)')


# ---------------------------------------------------------------------------
# The bad-input battery
# ---------------------------------------------------------------------------
def alive(rig):
    """The board still answers at the cli with an `ok>` prompt (LiteForth, the
    good image, still running). A light check with no reset, so it doesn't
    churn the USB port."""
    if not rig.usb.open(10):
        return False
    rig.usb.write('\r\n')
    ok = rig.usb.wait('ok>', 8)
    rig.usb.close()
    return ok


def refused(rig, path, where):
    """Feeds the bad package and returns (was_refused, detail, rebooted).
    Staged with `-2 ymodem`: a package the keyless stage check refuses never
    reaches flash and the board keeps running (rebooted False). Only a
    package that stages (a "boot"-class corruption, which our battery doesn't
    produce) needs a reset to read stage 1's verdict."""
    ok, text = rig.stage(path)
    if not ok:
        if 'no cli port' in text or 'cli' in text:
            return None, 'could not reach the board to stage it (port error)', False
        return True, 'refused at `-2 ymodem` (not staged)', False
    # It staged: reset once and read stage 1's verdict at install.
    line, start = rig.reset_and_wait(
        r'V3F update: .*(installed, on trial|refused|nothing to do)|recovery mode', 60)
    if line is None:
        return None, 'no verdict from stage 1 after the reset (port error?)', True
    if 'installed, on trial' in line:
        return False, f'INSTALLED a corrupt image ({line})', True
    return True, f'refused by stage 1 at boot ({line})', True


def h_battery(rig, a):
    """Every corrupted package refused, and the good image still boots after
    each. The packages are the lab's GOOD.lfu with one thing broken; none
    needs a key to make."""
    hiltest, _ = deps()
    check = hiltest.check
    with tempfile.TemporaryDirectory() as d:
        bad = make_bad(a.good, d)
        for name, what, where, path in bad:
            start = rig.debug.mark()
            was, detail, rebooted = refused(rig, path, where)
            if was is None:                     # couldn't reach the board: not a refusal
                check(False, f'{name}: could not test ({detail})')
                rig.log(start)
                continue
            check(was, f'{name}: {what} -> refused ({detail})')
            # the good image must still be there. A stage-refused package
            # never rebooted the board, so just confirm it still answers (no
            # reset: resets churn the USB port); a package refused at install
            # already rebooted into the good image, so read that.
            if rebooted:
                line = rig.debug.wait(r'LiteForth starting|recovery mode', start, 30)
                check(line is not None and 'recovery' not in line,
                      f'{name}: the good image booted after ({line})')
            else:
                check(alive(rig), f'{name}: the board kept running the good image')
            rig.log(start)


# ---------------------------------------------------------------------------
# Survival: the board still updates and rolls back (hiltest's own scenarios)
# ---------------------------------------------------------------------------
def h_survival(rig, a):
    """The board is still healthy after the battery: a good package installs
    and is confirmed, and (with --hang) a hanging image rolls back."""
    hiltest, _ = deps()
    hiltest.h_install(rig, a)
    if a.hang:
        hiltest.h_rollback(rig, a)


PHASES = [
    ('config', h_boot_config),
    ('bad-inputs', h_battery),
    ('survival', h_survival),
]
# Run last, and only with --option-bytes: wlink halts the core to read and
# resets the chip, which on a locked board needs a physical power cycle
# before the serial ports come back. So it's never part of the default,
# repeatable run.
OPTION_BYTES_PHASE = ('option-bytes', h_option_bytes)

OUT_OF_SCOPE = """\
Out of scope for this runner, and where that hardness rests instead:

- **Physical attacks** -- voltage or clock glitching, side channels, fault
  injection, decapsulation. These need the bench, not a script. Per-board
  keys limit any one physical extraction to the board it came from.
- **Attacks needing a private key** -- re-signing a changed image with the
  root or release key, a version or serial downgrade. These rest on key
  custody (the signing keys never leave the station) and are exercised in
  the desktop tests (unit_tests/seal, unit_tests/update).
- **Escalation probes** -- this runner never pokes a fence from inside to
  see if it holds; a shipped board is the attacker's, so the locked-state
  readback above is the proof a fence is in force.
"""


def main():
    p = argparse.ArgumentParser(description='Root-of-trust hardness test on a real board',
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--list', action='store_true', help='list the serial ports and exit')
    p.add_argument('what', nargs='?', choices=('check', 'run'))
    p.add_argument('--debug', help="the debug port, the WCH-Link's UART (default: found, else COM14)")
    p.add_argument('--cli', '--usb', dest='usb',
                   help="the cli port, the board's USB terminal (default: found by its USB IDs)")
    p.add_argument('--wlink', help='the wlink tool for the option-byte read (default: found)')
    p.add_argument('--option-bytes', action='store_true',
                   help='also read the option bytes over wlink, as a last phase. This halts the '
                        'core and resets the chip, which on a locked board needs a power cycle '
                        'before the serial ports come back, so it is off by default')
    p.add_argument('--reset-cmd', default=os.environ.get('HIL_RESET'),
                   help='a command line that resets the board (default: type reboot)')
    p.add_argument('--good', help='a valid update package for this board')
    p.add_argument('--hang', help='a package of a V5F image that never starts (adds rollback)')
    p.add_argument('--only', help='run only this phase (config, bad-inputs, survival, option-bytes)')
    p.add_argument('--report', default=time.strftime('hardness-%Y%m%d-%H%M.md'))
    a = p.parse_args()
    hiltest, lockdown = deps()
    note, check, sha = hiltest.note, hiltest.check, hiltest.sha
    a.debug = a.debug or hiltest.find_link_uart() or 'COM14'
    if a.list:
        hiltest.list_serial()
        return 0
    if not a.what:
        p.error('say --list, check or run')
    try:
        rig = hiltest.Rig(a)
    except Exception as e:                      # noqa: BLE001
        sys.exit(f'hardness.py: {e}\nIs a terminal (kwai, or one watching the debug port) using '
                 'the port? Close it and try again. (`hardness.py --list` shows the ports.)')
    if a.what == 'check':
        line, start = rig.reset_and_wait(r'V3F', 10)
        print(f'debug port ({a.debug}): {"ok: " + line if line else "nothing after the reset"}')
        rig.debug.wait(r'USB configured', start, 20)
        ok = rig.usb.open()
        print(f'cli port ({a.usb or hiltest.find_board_port()}): {"open" if ok else "not found"}')
        if ok:
            rig.usb.close()
        if a.option_bytes:                      # halts the board; only when asked
            obr, wpr, text = read_option_bytes(a)
            print(f'wlink: {"ok, " + lockdown.ob_text(obr, wpr) if obr is not None else text}')
        return 0 if line and ok else 1
    # run
    if not a.good:
        p.error('run needs --good (a valid package for this board)')
    phases = PHASES + ([OPTION_BYTES_PHASE] if a.option_bytes else [])
    todo = [(n, f) for n, f in phases if not a.only or n == a.only]
    if not todo:
        p.error(f'no such phase: {a.only} (--option-bytes enables the option-bytes phase)')
    if a.option_bytes:
        print('  ---- note: the option-bytes phase will halt the board over wlink at the end; '
              'power-cycle it afterward to get the serial ports back')
    note('# Root-of-trust hardness on the board (hardness.py)\n')
    note(f'Run {time.strftime("%Y-%m-%d %H:%M:%S")}; debug port {a.debug}, cli port '
         f'{a.usb or hiltest.find_board_port()}.\n')
    note(f'- GOOD: `{os.path.basename(a.good)}`, sha256 {sha(a.good)}')
    if a.hang:
        note(f'- HANG: `{os.path.basename(a.hang)}`, sha256 {sha(a.hang)}')
    for name, f in todo:
        print(f'{name}:')
        rig.section(name, f.__doc__)
        f(rig, a)
    builds = [l for l in rig.debug.lines if 'built' in l]
    if builds:
        note(f'\nBuild: `{builds[-1]}`')
    note('\n## ' + OUT_OF_SCOPE)
    note(f'\n**{"All passed" if not hiltest.failures else f"{hiltest.failures} failed"}.**')
    with open(a.report, 'w', encoding='utf-8') as f:
        f.write(''.join(hiltest.report))
    print(f'{"all passed" if not hiltest.failures else f"{hiltest.failures} failed"}; '
          f'report in {a.report}')
    return 1 if hiltest.failures else 0


if __name__ == '__main__':
    sys.exit(main())
