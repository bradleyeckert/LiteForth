#!/usr/bin/env python3
"""Flashes a CH32H417 board with a SECURITY_LEVEL 2 build and locks it
down: read protection on, the V3F's sectors write-protected, the boot ROM's
USB and USART downloads off. The debug port stays: a chip erase through the
WCH-LinkE (`wlink unprotect`, done by hand) always takes a board back, and
nothing else does.

    lockdown.py check [--wlink EXE]
    lockdown.py run --v3f V3F --v5f V5F [--cli PORT] [--debug PORT]
    lockdown.py lock [--cli PORT] [--debug PORT]
                    [--v3f-address A] [--v5f-address A] [--wlink EXE] [--report FILE]

check only reads: the probe, the chip, its UID and the option bytes, and
whether the board is locked down. lock is run without the flashing, for a
board that already runs a level 2 build and sits in recovery, fresh from a
power-up: it doesn't use wlink until the board is locked, and takes the
option bytes from stage 1's recovery console instead.

wlink halts the V3F to read it and leaves it halted (its USB stops
answering), and on a board in recovery the WCH-LinkE then often fails to
attach again (protocol error 0x55). So run resumes and resets it after
flashing, and asks you to turn the power off and on if it still doesn't
start; lock reads the probe's view only at the end.

run:
 1. Refuses a board that's read- or write-protected already: wlink's flash
    would unprotect it first, which wipes it or clears its write protection.
 2. Flashes V3F (default at 0x08000000) and V5F (default at 0x08014000)
    with wlink, without erasing anything else, reads the start of each back
    to check it, and resets the chip. A .hex or .elf carries its addresses.
 3. On the debug port, the level 2 build's stage 1 says the board isn't
    locked down and stays in recovery; on the cli port, its recovery
    console must offer `lockdown`, or this isn't a level 2 build.
 4. Asks you to type LOCK, then sends `lockdown yes`: stage 1 programs the
    option bytes, reads them back and resets the chip.
 5. Waits for the board to start locked, with no "not locked down": in
    recovery saying no V5F image is installed (a level 2 board starts only
    an image stage 1 installed and checked), or LiteForth starting if one
    is; if the option bytes didn't load at that reset, asks you to turn
    the power off and on. Then `wlink status` must say the
    flash is protected.
The report (--report, default lockdown-report-DATE.md) has every step and
both ports' output word for word, the images' sha256 and the UID, for
doc/admin/board-tests.md. Exit status 0 if the board ended up locked down.

After it: run `enroll` (a level 2 board's key comes from the RNG), put the
new line in boards.csv, and seal the card's lfblocks.fb4 and packages for it.

It needs wlink (scripts/README.md, lfmcp.py) and pyserial, as hiltest.py
does. Its ports: the cli port, the board's own USB terminal (--cli, or
--usb), and the debug port, the WCH-Link's UART (--debug); each is found by
its USB IDs (LiteForth's 1A86:FE0C after the flash) when not given. Try it on a spare board first.
"""
import argparse
import hashlib
import os
import re
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
from hiltest import (DebugPort, UsbPort, list_ports, find_board_port,  # noqa: E402
                     find_link_uart, serial)
from lfmcp import Wlink, OBR_RDPRT, WPR_NONE  # noqa: E402

V3F_ADDRESS = 0x08000000
V5F_ADDRESS = 0x08014000                # UPD_V5F_ADDR in stage1_layout.h
V3F_MAX = 64 * 1024
V5F_MAX = 128 * 1024                    # the V5F slot
LOCKED_WPR = 0xFFFFFF00                 # STAGE1_WRP_WRITABLE: the V3F's 64 KB protected
DOWNLOADS = 0xC0 << 2                   # OBR's USARTDL and USBFSDL bits

report = []
debug_seen = {}                         # the debug port and where this run began, for a stop


def say(text=''):
    print(text, flush=True)
    report.append(text + '\n')


def block(title, text):
    say(f'{title}:')
    report.append('\n```\n' + (text.rstrip() or '(nothing)') + '\n```\n\n')
    print(text.rstrip(), flush=True)


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def ob_text(obr, wpr):
    user = obr >> 2
    return (f'OBR {obr:08X}, WPR {wpr:08X}: read protection {"on" if obr & OBR_RDPRT else "off"}, '
            f'USB download {"on" if user & 0x40 else "off"}, '
            f'USART download {"on" if user & 0x80 else "off"}, '
            f'write protection {"as stage 1 wants" if wpr == LOCKED_WPR else "none" if wpr == WPR_NONE else "other"}')


def locked(obr, wpr):
    return bool(obr & OBR_RDPRT) and not (obr & DOWNLOADS) and wpr == LOCKED_WPR


class Probe(Wlink):
    """wlink, tried again when the probe fails to attach: it sometimes says
    "underlying protocol error" and works a second later. The read-only
    commands (status, dump, reset) get four tries; a flash is tried again
    only if it failed before it wrote anything (no "Flashing" in its
    output), which is safe because the chip was found unprotected first.
    A wrong read-back isn't a probe failure, so it's never retried. A short
    pause before each call gives the probe time after the last one."""

    def run(self, args, timeout=30):
        tries = 4
        for i in range(tries):
            time.sleep(0.5)
            code, text = super().run(args, timeout)
            wrote = args[0] == 'flash' and 'Flashing' in text
            if code == 0 or wrote or i == tries - 1:
                return code, text
            say(f'(wlink {args[0]} failed, try {i + 1}: '
                f'{text.strip().splitlines()[-2:][0] if text.strip() else "no output"}; trying again)')
            time.sleep(1)


class Fail(Exception):
    pass


def status(wl):
    """wlink status: (its output, the UID, whether the flash is protected)"""
    code, text = wl.run(['status'])
    uid = re.search(r'UID\(([0-9a-fA-F-]+)\)', text)
    prot = re.search(r'Flash protected: (\w+)', text)
    return text, uid and uid.group(1).replace('-', ''), prot and prot.group(1) == 'true'


def release(wl, restart):
    """wlink halts the core to read it, and leaves its halt request set:
    then the V3F stays halted (its USB stops answering), and even a soft
    reset halts it again. `resume` clears the request; with restart, a
    reset then gives a clean start."""
    code, text = wl.run(['resume'])
    block('wlink resume', text)
    if restart:
        code, text = wl.run(['reset'])
        block('wlink reset', text)


def cmd_check(a):
    wl = Probe(a.wlink)
    text, uid, prot = status(wl)
    block('wlink status', text)
    try:
        obr, wpr = wl.option_bytes()
    except OSError as e:
        say(f'option bytes: not read ({e})')
        return 1
    finally:
        release(wl, False)
    say(ob_text(obr, wpr))
    say('locked down' if locked(obr, wpr) else 'NOT locked down')
    return 0 if locked(obr, wpr) else 1


def image(path, address, maximum, name):
    if not os.path.isfile(path):
        raise Fail(f'{name}: no such file {path}')
    size = os.path.getsize(path)
    binary = path.lower().endswith('.bin')
    if binary and size > maximum:
        raise Fail(f'{name}: {size} bytes, more than its {maximum // 1024} KB')
    say(f'- {name}: `{os.path.basename(path)}`, {size} bytes, sha256 {sha(path)}'
        + (f', at {address:#010x}' if binary else ' (addresses in the file)'))
    return binary


def flash(wl, path, address, binary):
    args = ['flash', '-R']              # -R: no reset; never --erase
    if binary:
        args += ['--address', hex(address)]
    code, text = wl.run(args + [path], timeout=300)
    block(f'wlink {" ".join(args)} {os.path.basename(path)}', text)
    if code:
        raise Fail(f'flashing {path} failed')


def verify(wl, path, address):
    with open(path, 'rb') as f:
        want = f.read(256)
    fd, out = tempfile.mkstemp(suffix='.bin')
    os.close(fd)
    try:
        code, text = wl.run(['dump', hex(address), str(len(want)), '-o', out])
        with open(out, 'rb') as f:
            got = f.read()
    finally:
        os.remove(out)
    if code or got[:len(want)] != want:
        raise Fail(f'{os.path.basename(path)} doesn\'t read back at {address:#010x}:\n{text}')
    say(f'- read back the first {len(want)} bytes at {address:#010x}: as the image')


def debug_log(dbg, start, title):
    block(title, '\n'.join(dbg.since(start)))


def open_console(a, dbg, start):
    """The recovery console on the cli port, with its help: (Usb, text)"""
    usb = UsbPort(a.usb)
    if not usb.open(20):
        name = a.usb or find_board_port()
        if not name:
            raise Fail('no cli port (LiteForth\'s USB port, 1A86:FE0C): is the board\'s own '
                       'USB connected to this PC? (`hiltest.py --list` shows the ports)')
        if os.name == 'nt' and name not in [p.device for p in list_ports.comports()]:
            raise Fail(f'there\'s no {name} (`hiltest.py --list` shows the ports)')
        try:
            serial.Serial(name, 115200).close()
        except serial.SerialException as e:
            raise Fail(f'{name} is there but won\'t open ({e}): is kwai or another terminal '
                       'using it, or is the V3F halted (wlink halts it: turn the power off and '
                       'on)? Then run `lock`.')
        raise Fail(f'{name} wouldn\'t open within 20 s')
    usb.write('help\r')                 # any other line: stage 1 prints its help
    if not usb.wait('recovery ok> ', 8):
        usb.write('\r')
        usb.wait('recovery ok> ', 5)
    console = bytes(usb.text).decode('latin-1')
    debug_log(dbg, start, 'Debug port')
    block('Cli port (recovery console)', console)
    if 'recovery ok> ' not in console:
        raise Fail('no recovery console on the cli port: is the board locked already, or not '
                   'a level 2 build? (Turn the power off and on if wlink touched it last.)')
    if 'lockdown: lock this board' not in console:
        raise Fail('the recovery console doesn\'t offer lockdown: not a level 2 build, or '
                   'locked already')
    return usb, console


def cmd_run(a):
    wl = Probe(a.wlink)
    say(f'# Lock-down {a.cmd}, {time.strftime("%Y-%m-%d %H:%M:%S")}')
    say()
    uid = None
    if a.cmd == 'run':
        say('## The board before')
        text, uid, prot = status(wl)
        block('wlink status', text)
        if prot:
            raise Fail('the flash is read-protected already: wlink would wipe it to flash it. '
                       'Nothing done.')
        obr, wpr = wl.option_bytes()
        say(ob_text(obr, wpr))
        if obr & OBR_RDPRT or wpr != WPR_NONE:
            raise Fail('the option bytes protect the flash already: wlink would unprotect it '
                       'to flash it. Nothing done.')
        say(f'UID: {uid or "not found"}')
        say()
        say('## Flashing')
        v3f_bin = image(a.v3f, a.v3f_address, V3F_MAX, 'V3F')
        v5f_bin = image(a.v5f, a.v5f_address, V5F_MAX, 'V5F')
    debug = a.debug or find_link_uart()
    if not debug:
        raise Fail('no debug port: give --debug (`hiltest.py --list` shows the ports)')
    say(f'Debug port: {debug}')
    dbg = DebugPort(debug)
    debug_seen.update(port=dbg, start=dbg.mark())
    start = dbg.mark()
    if a.cmd == 'run':
        flash(wl, a.v3f, a.v3f_address, v3f_bin)
        flash(wl, a.v5f, a.v5f_address, v5f_bin)
        if v3f_bin:
            verify(wl, a.v3f, a.v3f_address)
        if v5f_bin:
            verify(wl, a.v5f, a.v5f_address)
        start = dbg.mark()
        release(wl, True)               # a clean start, not halted, if the probe can
        say()
        say('## The level 2 build on an open board')
        line = dbg.wait(r'V3F: recovery mode|LiteForth starting', start, 15)
        if not line:
            say('No start on the debug port after the reset: the probe left the V3F halted.')
            print('\nTurn the board off and on (unplug the WCH-LinkE and the board\'s USB, '
                  'wait, plug them in), then press Enter.')
            input()
            say('Power turned off and on.')
            line = dbg.wait(r'V3F: recovery mode|LiteForth starting', start, 60)
        if not line or 'recovery' not in line:
            debug_log(dbg, start, 'Debug port')
            raise Fail('stage 1 didn\'t stay in recovery: not a level 2 build, or it didn\'t '
                       'start')
    else:
        say('## The board before (from stage 1)')
        say('wlink isn\'t used before the lock: it leaves the V3F halted, and its USB with it. '
            'Stage 1\'s recovery console says what the option bytes are.')
    usb, console = open_console(a, dbg, start)
    ob = re.search(r'\(OBR ([0-9A-F]{8}), WPR ([0-9A-F]{8})\)', console)
    if ob:
        say('Stage 1: ' + ob_text(int(ob.group(1), 16), int(ob.group(2), 16)))
    say()
    say('## Locking')
    print('\nThis locks the board down: read protection on, the V3F\'s sectors write-protected,\n'
          'the boot ROM\'s USB and USART downloads off. Only a chip erase through the\n'
          'WCH-LinkE undoes it.' + (f' Board UID {uid}.' if uid else ''))
    answer = input('Type LOCK to lock it down: ').strip()
    if answer != 'LOCK':
        say(f'Not locked (the answer was {answer!r}); the board stays in recovery.')
        return 1
    say('Typed LOCK.')
    start = dbg.mark()
    usb.text.clear()
    usb.write('lockdown yes\r')
    line = dbg.wait(r'lockdown: option bytes now', start, 20)
    time.sleep(0.5)
    usb.close()
    if not line or not line.endswith(': ok'):
        debug_log(dbg, start, 'Debug port')
        raise Fail(f'the option bytes weren\'t programmed as they should be ({line})')
    say()
    say('## The board after')
    booted = dbg.wait(r'LiteForth starting|V3F: recovery mode', start, 40)
    if booted and any('V3F security: not locked down' in l for l in dbg.since(start)):
        debug_log(dbg, start, 'Debug port')
        say('The option bytes didn\'t load at that reset.')
        print('\nTurn the board\'s power off, wait a few seconds, and turn it on again.')
        start = dbg.mark()
        booted = dbg.wait(r'LiteForth starting|V3F: recovery mode', start, 180)
    debug_log(dbg, start, 'Debug port')
    lines = dbg.since(start)
    if any('V3F security: not locked down' in l for l in lines):
        raise Fail('the board still says it isn\'t locked down')
    if not booted:
        raise Fail('stage 1 didn\'t start')
    if 'LiteForth starting' in booted:
        say('Stage 1 started the V5F with no lock-down warning: its check of OBR and WPR passed.')
    elif any('no V5F image installed' in l for l in lines):
        say('Stage 1 passed its check of OBR and WPR (no lock-down warning) and stays in '
            'recovery: no V5F image is installed yet, and level 2 starts only one it installed.')
    else:
        raise Fail('stage 1 went to recovery for another reason')
    say()
    say('## The probe\'s view (last: it halts the V3F)')
    text, uid2, prot = status(wl)
    block('wlink status', text)
    try:
        obr, wpr = wl.option_bytes()
        say(ob_text(obr, wpr))
    except OSError as e:
        say(f'option bytes: not read through the debug port ({e})')
    release(wl, False)
    say('wlink says the flash is ' + ('protected.' if prot else 'NOT protected (or it couldn\'t '
                                      'tell).'))
    say()
    say('**Locked down.** The probe may have left the V3F halted: turn the power off and on. '
        'Next: `enroll`, the new line in boards.csv, and the card\'s files sealed for it.')
    return 0


def main():
    p = argparse.ArgumentParser(description='Flash a CH32H417 board and lock it down.')
    sub = p.add_subparsers(dest='cmd', required=True)
    for name in ('check', 'run', 'lock'):
        s = sub.add_parser(name)
        s.add_argument('--wlink', help='the wlink program (default: on the PATH)')
        if name == 'run':
            s.add_argument('--v3f', required=True, help='the V3F image (.bin, .hex or .elf)')
            s.add_argument('--v5f', required=True, help='the V5F image (.bin, .hex or .elf)')
            s.add_argument('--v3f-address', type=lambda v: int(v, 0), default=V3F_ADDRESS)
            s.add_argument('--v5f-address', type=lambda v: int(v, 0), default=V5F_ADDRESS)
        if name != 'check':
            s.add_argument('--cli', '--usb', dest='usb',
                           help='the cli port, the board\'s USB terminal (default: found by '
                                'its USB IDs)')
            s.add_argument('--debug', default=os.environ.get('HIL_DEBUG'),
                           help='the debug port, the WCH-Link\'s UART (default: found by its '
                                'USB IDs)')
            s.add_argument('--report', default=time.strftime('lockdown-report-%Y%m%d-%H%M.md'))
    a = p.parse_args()
    if a.cmd == 'check':
        return cmd_check(a)
    try:
        code = cmd_run(a)
    except (Fail, OSError) as e:
        if debug_seen and not report[-1].startswith('\n```'):
            say()
            debug_log(debug_seen['port'], debug_seen['start'], 'Debug port, this run')
        say()
        say(f'**Stopped:** {e}')
        code = 1
    with open(a.report, 'w', encoding='utf-8', newline='\n') as f:
        f.write(''.join(report))
    print(f'\nreport: {a.report}')
    return code


if __name__ == '__main__':
    sys.exit(main())
