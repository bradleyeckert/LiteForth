#!/usr/bin/env python3
"""Turnkey root-of-trust hardness test for a locked CH32H417 board, for a
testing lab: one command that proves as much of the RoT's hardness as can
be shown without opening the chip or holding the signing keys.

    hardness.py --list
    hardness.py check [--debug PORT] [--cli PORT] [--wlink PATH]
    hardness.py run --good GOOD.lfu [--hang HANG.lfu] [--report FILE] [...]

It proves hardness two ways, and only these two (see "Out of scope" below):

  - it READS the board's locked state and checks it is as a shipped board
    should be: the option bytes (read protection on, the boot-ROM downloads
    off, the write-protect mask set), and the boot's own report of the
    security level, the lock-down, and the PMP (all four entries set and
    locked);

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

It is built on hiltest.py (the same two ports, the same YMODEM and report)
and reads the option bytes through lockdown.py's wlink, so it needs what
they need: the cli port (the board's USB terminal, 1A86:FE0C), the debug
port (the WCH-Link's UART), the card in the board with lfstage.bin and
lfbackup.bin, pyserial, and -- only for the option-byte read -- a WCH-Link
(wlink); without one that one check is skipped with a note. Point it at a
LOCKED, level-2 board: the signature and lock-down checks only mean
something there.

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


def h_locked_state(rig, a):
    """What the board's protections read back as, without touching them: the
    option bytes (through wlink, if one is attached) and the boot's own
    report of the security level, the lock-down and the PMP."""
    hiltest, lockdown = deps()
    note, check = hiltest.note, hiltest.check
    obr, wpr, text = read_option_bytes(a)
    if obr is None:
        note(f'- (option bytes not read: {text})')
        print(f'  ---- option bytes: {text}')
    else:
        note('wlink:\n\n```\n' + text.strip() + '\n```')
        note(f'- {lockdown.ob_text(obr, wpr)}')
        check(lockdown.locked(obr, wpr),
              'the option bytes are locked down (read protection on, downloads off, '
              'write-protect set)')

    # The boot's own report, on the debug port
    line, start = rig.reset_and_wait(r'V3F stage 1: security level|not locked down|LiteForth starting', 30)
    lines = rig.debug.since(start)
    joined = '\n'.join(lines)
    level2 = any('security level 2' in l for l in lines)
    check(level2, 'the boot reports security level 2')
    check('not locked down' not in joined, 'the boot does not report "not locked down"')
    pmp = next((l for l in lines if l.startswith('V5F PMP:')), None)
    if pmp is None:
        pmp = rig.debug.wait(r'^V5F PMP:', start, 10)
    check(pmp is not None and 'as set' in pmp and 'locked' in pmp and 'NOT locked' not in pmp,
          f'the PMP reads back set and locked ({pmp})')
    rig.debug.wait(r'LiteForth starting', start, 30)
    rig.log(start)


# ---------------------------------------------------------------------------
# The bad-input battery
# ---------------------------------------------------------------------------
def refused(rig, path, where):
    """Feeds the bad package and returns (was_refused, detail). Staged with
    `-2 ymodem`; if the board takes it (a "boot"-class corruption it can't
    see until it installs), reset and read stage 1's verdict."""
    ok, text = rig.stage(path)
    if not ok:                                  # refused at the stage check
        return True, 'refused at `-2 ymodem` (not staged)'
    # It staged: only a "boot"/"either" corruption should reach here.
    line, start = rig.reset_and_wait(
        r'V3F update: .*(installed, on trial|refused|nothing to do)|recovery mode', 60)
    if line is None:
        return False, 'no verdict from stage 1 after the reset'
    if 'installed, on trial' in line:
        return False, f'INSTALLED a corrupt image ({line})'
    return True, f'refused by stage 1 at boot ({line})'


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
            was, detail = refused(rig, path, where)
            check(was, f'{name}: {what} -> refused ({detail})')
            # the good image must still boot: a refused update leaves it be
            line, bstart = rig.reset_and_wait(r'LiteForth starting|recovery mode', 40)
            check(line is not None and 'recovery' not in line,
                  f'{name}: the good image still boots after ({line})')
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
    ('locked-state', h_locked_state),
    ('bad-inputs', h_battery),
    ('survival', h_survival),
]

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
    p.add_argument('--reset-cmd', default=os.environ.get('HIL_RESET'),
                   help='a command line that resets the board (default: type reboot)')
    p.add_argument('--good', help='a valid update package for this board')
    p.add_argument('--hang', help='a package of a V5F image that never starts (adds rollback)')
    p.add_argument('--only', help='run only this phase (locked-state, bad-inputs, survival)')
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
        obr, wpr, text = read_option_bytes(a)
        print(f'wlink: {"ok, " + lockdown.ob_text(obr, wpr) if obr is not None else text}')
        return 0 if line and ok else 1
    # run
    if not a.good:
        p.error('run needs --good (a valid package for this board)')
    todo = [(n, f) for n, f in PHASES if not a.only or n == a.only]
    if not todo:
        p.error(f'no such phase: {a.only}')
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
