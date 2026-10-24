#!/usr/bin/env python3
"""Provisioning new boards (doc/admin/provisioning.md): a blank CH32H417
flashed with a release's level 2 build, locked down and enrolled (pass 1),
then, once the vendor has packaged the release for the batch, the release
installed (pass 2). The station holds no private key.

    provision.py station-check [--station DIR]
    provision.py batch-new NAME [--station DIR]
    provision.py card DRIVE [--station DIR]
    provision.py pass1 --batch NAME [--station DIR] [--cli PORT] [--debug PORT] [--wlink EXE]
    provision.py pass2 --batch NAME --package P [--package P ...] [--test FILE] [...]
    provision.py batch-review BATCHDIR --boards boards.csv [--append] [--split N]

The station's folder (--station, default the current one):

    release/      the vendor's signed release: manifest.json, the files it
                  names, and root.pub (sealblocks.py manifest)
    boards.csv    the vendor's copy, read only here: no UID in it is enrolled again
    batches/NAME/ batch.json, enrolled.csv (UID,public key), records/UID.md,
                  installed.csv

station-check checks the release against root.pub and lists the probe and
ports it finds. batch-new starts a batch, tied to the release as it is
now. card prepares a microSD card in the PC's reader: an empty exFAT
volume gets the release's lfblocks.fb4 and blank lfstage.bin and
lfbackup.bin at the manifest's sizes, read back.

pass1, for each board the operator connects (its card in, the WCH-LinkE
on its debug header, its USB to the PC): refuses a protected chip, or one
that isn't blank unless this batch flashed it and it isn't locked yet;
flashes the release's images; checks that stage 1 runs at security level 2
in recovery; locks the board down (the operator types LOCK once per run);
checks it came back locked; sends `enroll`, which must make a new key, for
a UID the probe read and that isn't enrolled already; checks with the probe
that the flash is protected; and records the board: its line in
enrolled.csv and its report, both ports' output word for word.

batch-review, at the vendor: checks a batch's enrolled.csv against its
records and boards.csv, splits it into parts that fit one package each,
prints the sealblocks.py commands, and with --append adds the lines to
boards.csv (commit that, reviewed).

pass2, for each board: checks the packages once (signature, product,
version as the manifest says), then on the board: `enroll` gives a UID with
a pass 1 record and the same key, a recipient of one of the packages,
which goes to stage 1's recovery console. It must install, start at level
2 with the manifest's version, and be confirmed; `revision .` must give
the version, and again after a `reboot`. --test FILE then sends each line
`COMMAND ==> TEXT` and expects TEXT in the reply.

Needs pyserial, the cryptography package and wlink, as lockdown.py does.
Exit status: 0 if everything done in this run passed.
"""
import argparse
import csv
import datetime
import getpass
import hashlib
import json
import os
import platform
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import sealblocks as sb  # noqa: E402
try:                                    # the board's ports need pyserial; batch-review doesn't
    import lockdown as ld  # noqa: E402
    from hiltest import DebugPort, UsbPort, find_link_uart  # noqa: E402
    Fail = ld.Fail
    NO_SERIAL = None
except SystemExit as e:
    ld, NO_SERIAL = None, str(e)

    class Fail(Exception):
        pass


def say(text=''):
    ld.say(text)


def block(title, text):
    ld.block(title, text)

RECOVERY_PROMPT = 'recovery ok> '
CSV_LINE = re.compile(r'\b([0-9a-f]{16}),([0-9a-f]{64})\b')
ERASED_WORDS = (b'\xff' * 4, bytes.fromhex('39E339E3'))     # erased flash reads one of these
PACKAGE_BOARDS = 157                    # recipients in one package's block 0 (one hash table)
CARD_JUNK = {'system volume information', '.trashes', '.fseventsd', '.spotlight-v100'}


def now():
    return datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')


def ask(text):
    """A question for the operator, on a line of its own (so a script
    driving this sees it whole), answered with a line."""
    print(text, flush=True)
    try:
        return input().strip()
    except EOFError:
        return 'q'


def sha_file(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


# ---------------------------------------------------------------------------
# The station
# ---------------------------------------------------------------------------
class Station:
    def __init__(self, path):
        self.dir = os.path.abspath(path)
        self.release = os.path.join(self.dir, 'release')
        self.boards_csv = os.path.join(self.dir, 'boards.csv')
        self.batches = os.path.join(self.dir, 'batches')

    def manifest(self):
        """The release's manifest, checked with every file it names."""
        root = os.path.join(self.release, 'root.pub')
        if not os.path.isfile(root):
            raise Fail(f'no {root}: the release folder needs the root public key')
        try:
            m = sb.check_manifest(self.release, sb.root_public(root))
        except sb.SealError as e:
            raise Fail(f'the release doesn\'t check out: {e}')
        m['sha256'] = sha_file(os.path.join(self.release, sb.MANIFEST))
        return m

    def enrolled_uids(self):
        """Every UID in boards.csv"""
        return set(uid for uid, _ in read_lines(self.boards_csv, missing_ok=True))

    def enrolled_keys(self):
        """Every public key in boards.csv"""
        return set(pub for _, pub in read_lines(self.boards_csv, missing_ok=True))

    def batch(self, name):
        d = os.path.join(self.batches, name)
        if not os.path.isfile(os.path.join(d, 'batch.json')):
            raise Fail(f'no batch {name} (provision.py batch-new {name})')
        return Batch(d)


class Batch:
    def __init__(self, d):
        self.dir = d
        self.name = os.path.basename(d)
        with open(os.path.join(d, 'batch.json')) as f:
            self.info = json.load(f)
        self.enrolled = os.path.join(d, 'enrolled.csv')
        self.records = os.path.join(d, 'records')
        self.started = os.path.join(d, 'started')
        self.installed = os.path.join(d, 'installed.csv')

    def lines(self):
        return read_lines(self.enrolled, missing_ok=True)

    def record(self, uid):
        return os.path.join(self.records, uid + '.md')

    def check_release(self, m):
        if m['sha256'] != self.info['manifest_sha256']:
            raise Fail(f'the release changed since batch {self.name} began (manifest '
                       f'{m["sha256"][:16]}..., the batch has {self.info["manifest_sha256"][:16]}...): '
                       'start a new batch')


def read_lines(path, missing_ok=False):
    """(uid, public key) for each line of a boards.csv-like file, in hex."""
    if missing_ok and not os.path.exists(path):
        return []
    out = []
    with open(path, newline='') as f:
        for i, row in enumerate(csv.reader(f), 1):
            if not row or not row[0].strip() or row[0].lstrip().startswith('#'):
                continue
            uid, pub = row[0].strip().lower(), (row[1].strip().lower() if len(row) > 1 else '')
            if not re.fullmatch(r'[0-9a-f]{16}', uid) or not re.fullmatch(r'[0-9a-f]{64}', pub):
                raise Fail(f'{path} line {i}: need UID (16 hex digits), public key (64)')
            out.append((uid, pub))
    return out


def cmd_station_check(a):
    st = Station(a.station)
    m = st.manifest()
    print(f'release: version {m["revision"]}, product {m["product"]:#x}, release key '
          f'{m["serial"]}, manifest {m["sha256"][:16]}...')
    for f in m['files']:
        where = f' at {f["address"]:#010x}' if 'address' in f else ''
        print(f'  {f["name"]}: {f["use"]}, {f["bytes"]} bytes{where}, sha256 {f["sha256"]}')
    print(f'  card: lfstage.bin {m["card"]["lfstage.bin"]} bytes, '
          f'lfbackup.bin {m["card"]["lfbackup.bin"]} bytes')
    print(f'boards.csv: {len(st.enrolled_uids())} board(s)')
    if NO_SERIAL:
        raise Fail(NO_SERIAL)
    wlink = a.wlink or shutil.which('wlink')
    print(f'wlink: {wlink or "NOT FOUND (put it on the PATH, or give --wlink)"}')
    print(f'debug port (WCH-LinkE UART): {find_link_uart() or "not found"}')
    return 0 if wlink else 1


def cmd_batch_new(a):
    st = Station(a.station)
    m = st.manifest()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', a.name):
        raise Fail('a batch name takes letters, digits, ".", "_" and "-"')
    d = os.path.join(st.batches, a.name)
    if os.path.exists(d):
        raise Fail(f'batch {a.name} exists')
    os.makedirs(os.path.join(d, 'records'))
    with open(os.path.join(d, 'batch.json'), 'w') as f:
        json.dump({'name': a.name, 'created': now(), 'revision': m['revision'],
                   'product': m['product'], 'manifest_sha256': m['sha256']}, f, indent=1)
        f.write('\n')
    print(f'batch {a.name}: release version {m["revision"]}, in {d}')
    return 0


# ---------------------------------------------------------------------------
# card
# ---------------------------------------------------------------------------
def filesystem(path):
    """The file system type of the volume path is on, or None."""
    if os.name == 'nt':
        import ctypes
        name = ctypes.create_unicode_buffer(64)
        root = os.path.splitdrive(os.path.abspath(path))[0] + '\\'
        ok = ctypes.windll.kernel32.GetVolumeInformationW(
            ctypes.c_wchar_p(root), None, 0, None, None, None, name, 64)
        return name.value if ok else None
    best, kind = '', None
    try:
        with open('/proc/mounts') as f:
            for line in f:
                parts = line.split()
                mnt = parts[1].replace('\\040', ' ')
                if os.path.abspath(path).startswith(mnt.rstrip('/') + '/') or \
                        os.path.abspath(path) == mnt:
                    if len(mnt) > len(best):
                        best, kind = mnt, parts[2]
    except OSError:
        pass
    return kind


def cmd_card(a):
    st = Station(a.station)
    m = st.manifest()
    drive = a.drive
    if os.name == 'nt' and re.fullmatch(r'[A-Za-z]:?', drive):
        drive = drive[0] + ':\\'
    if not os.path.isdir(drive):
        raise Fail(f'{drive} isn\'t there: is the card in the reader?')
    fs = filesystem(drive)
    if not a.any_fs and (fs or '').lower() != 'exfat':
        raise Fail(f'{drive} is {fs or "of an unknown file system"}, not exFAT: format it as '
                   'exFAT (Quick Format), doc/sdcard.md')
    there = [n for n in os.listdir(drive) if n.lower() not in CARD_JUNK]
    if there:
        raise Fail(f'{drive} isn\'t empty ({", ".join(sorted(there)[:5])}): use a new card, or '
                   'format it as exFAT first, so its files are contiguous')
    blocks = next(f for f in m['files'] if f['use'] == 'card')
    files = [(blocks['name'], blocks['path'], blocks['bytes'])]
    files += [(n, None, size) for n, size in sorted(m['card'].items())]
    for name, src, size in files:
        dst = os.path.join(drive, name)
        with open(dst, 'wb') as out:
            if src:
                with open(src, 'rb') as f:
                    shutil.copyfileobj(f, out, 1 << 20)
            else:
                left = size
                while left:
                    n = min(left, 1 << 20)
                    out.write(bytes(n))
                    left -= n
            out.flush()
            os.fsync(out.fileno())
    for name, src, size in files:
        dst = os.path.join(drive, name)
        if os.path.getsize(dst) != size:
            raise Fail(f'{name} on the card is {os.path.getsize(dst)} bytes, not {size}')
        if src and sha_file(dst) != sha_file(src):
            raise Fail(f'{name} on the card reads back differently: try another card')
        if not src:
            with open(dst, 'rb') as f:
                while True:
                    b = f.read(1 << 20)
                    if not b:
                        break
                    if b.count(0) != len(b):
                        raise Fail(f'{name} on the card reads back differently: try another card')
        print(f'{name}: {size} bytes, read back')
    print('Card ready: eject it, then put it in the board.')
    return 0


# ---------------------------------------------------------------------------
# The board: probe and ports
# ---------------------------------------------------------------------------
class Session:
    """The probe and the debug port, for one run of pass1 or pass2."""

    def __init__(self, a):
        if NO_SERIAL:
            raise Fail(NO_SERIAL)
        self.a = a
        self.wl = ld.Probe(a.wlink)
        debug = a.debug or find_link_uart()
        if not debug:
            raise Fail('no debug port: is the WCH-LinkE plugged in? (or give --debug)')
        self.dbg = DebugPort(debug)

    def usb(self, timeout=20):
        u = UsbPort(self.a.usb)
        if not u.open(timeout):
            raise Fail('no cli port: is the board\'s own USB connected? (or give --cli)')
        return u

    def console(self, u, prompt, timeout=10):
        """Sends a line break and waits for prompt: (seen, the text so far)"""
        u.write('\r')
        ok = u.wait(prompt, timeout)
        return ok, bytes(u.text).decode('latin-1')

    def command(self, u, line, prompt, timeout=15):
        """Sends a line to the console; returns what came back up to prompt."""
        u0 = len(u.text)
        u.write(line + '\r')
        ok = u.wait(prompt, timeout)
        text = bytes(u.text[u0:]).decode('latin-1')
        block(f'Cli port: `{line}`', text)
        if not ok:
            raise Fail(f'no {prompt.strip()!r} after `{line}`')
        return text

    def power_cycle(self, start, pattern, timeout=180):
        print('\nTurn the board off and on: unplug the WCH-LinkE and the board\'s USB, wait '
              '2 s, plug them in.', flush=True)
        say('(asked for a power cycle)')
        return self.dbg.wait(pattern, start, timeout)


def same_uid(probe, line):
    """How the probe's UID and enroll's match: '' (the same), 'reversed',
    'words reversed', or None. wlink's byte order isn't known for sure."""
    if probe == line:
        return ''
    b = bytes.fromhex(probe)
    if b[::-1].hex() == line:
        return 'reversed'
    if (b[3::-1] + b[7:3:-1]).hex() == line:
        return 'words reversed'
    return None


def blank(data):
    return len(data) >= 4 and len(data) % 4 == 0 and any(
        data == w * (len(data) // 4) for w in ERASED_WORDS)


def boot_level(dbg, start, timeout=20):
    """The security level and installed version stage 1 reports at boot"""
    line = dbg.wait(r'V3F stage 1: security level', start, timeout)
    m = line and re.search(r'security level (\d+), installed version (\d+)', line)
    return (int(m.group(1)), int(m.group(2)), line) if m else (None, None, line)


def write_record(path, head, mode='w'):
    with open(path, mode, encoding='utf-8', newline='\n') as f:
        f.write(head + ''.join(ld.report))
    ld.report.clear()


# ---------------------------------------------------------------------------
# pass1
# ---------------------------------------------------------------------------
def pass1_board(s, st, b, m, state):
    """One board, as far as it gets: returns its UID once it's recorded."""
    wl = s.wl
    say('## The chip')
    text, uid, prot = ld.status(wl)
    block('wlink status', text)
    uid = (uid or '').lower()
    if not re.fullmatch(r'[0-9a-f]{16}', uid):
        raise Fail('the probe didn\'t give the chip\'s UID: is the WCH-LinkE on the board?')
    state['uid'] = uid
    if prot:
        raise Fail('the chip is protected: locked already. Set the board aside')
    obr, wpr = wl.option_bytes()
    say(ld.ob_text(obr, wpr))
    if obr & ld.OBR_RDPRT or wpr != ld.WPR_NONE:
        raise Fail('the chip is protected (its option bytes): set the board aside')
    if uid in st.enrolled_uids() or uid in dict(b.lines()):
        raise Fail(f'UID {uid} already enrolled: set the board aside, tell the release manager')
    data = read_flash(wl, sb.FLASH_BASE, 256)       # the V3F's image only: never the records
    started = os.path.join(b.started, uid)
    if blank(data):
        say('- the V3F\'s image area is blank')
    elif os.path.exists(started):
        say('- not blank, but this batch flashed it and it isn\'t locked: flashing it again')
    else:
        raise Fail('the chip isn\'t blank: set the board aside for the release manager')
    os.makedirs(b.started, exist_ok=True)
    open(started, 'w').close()
    say()
    say('## Flashing')
    start = s.dbg.mark()
    images = sorted((f for f in m['files'] if 'address' in f), key=lambda f: f['use'] == 'v3f')
    for f in images:                    # the V3F last (B25: then the probe may not attach)
        say(f'- {f["use"].upper()}: `{f["name"]}`, {f["bytes"]} bytes, sha256 {f["sha256"]}, '
            f'at {f["address"]:#010x}')
        ld.flash(wl, f['path'], f['address'], True)
    for f in images:
        ld.verify(wl, f['path'], f['address'])
    start = s.dbg.mark()
    ld.release(wl, True)
    say()
    say('## Stage 1 on the open board')
    line = s.dbg.wait(r'V3F: recovery mode|LiteForth starting', start, 15)
    if not line:
        line = s.power_cycle(start, r'V3F: recovery mode|LiteForth starting', 60)
    level, version, lline = boot_level(s.dbg, start, 5)
    block('Debug port', '\n'.join(s.dbg.since(start)))
    if not line or 'recovery' not in line or level != 2:
        raise Fail(f'stage 1 isn\'t a level 2 build in recovery ({lline or line}): the wrong '
                   'images in release/? Stop the batch, tell the release manager')
    u, console = ld.open_console(argparse.Namespace(usb=s.a.usb), s.dbg, s.dbg.mark())
    say()
    say('## Locking')
    if not state.get('lock_ok'):
        answer = ask('This locks boards down: read protection on, the V3F\'s sectors write-'
                     'protected, the boot ROM\'s downloads off. Only a chip erase undoes it.\n'
                     'Type LOCK to lock this board and the rest of this run\'s:')
        if answer != 'LOCK':
            u.close()
            raise Fail(f'not locked (the answer was {answer!r}); the board stays in recovery')
        state['lock_ok'] = True
        say('LOCK typed (once for this run).')
    start = s.dbg.mark()
    u.text.clear()
    u.write('lockdown yes\r')
    line = s.dbg.wait(r'lockdown: option bytes now', start, 20)
    time.sleep(0.5)
    u.close()
    if not line or not line.endswith(': ok'):
        block('Debug port', '\n'.join(s.dbg.since(start)))
        raise Fail(f'the option bytes weren\'t programmed as they should be ({line})')
    say(line)
    boot = r'LiteForth starting|V3F: recovery mode'
    booted = s.dbg.wait(boot, start, 40)
    if booted and any('not locked down' in x for x in s.dbg.since(start)):
        block('Debug port', '\n'.join(s.dbg.since(start)))
        say('The option bytes didn\'t load at that reset.')
        start = s.dbg.mark()
        booted = s.power_cycle(start, boot)
    lines = s.dbg.since(start)
    block('Debug port', '\n'.join(lines))
    if any('not locked down' in x for x in lines):
        raise Fail('still not locked down after the power cycle: power it off and on again; '
                   'if it stays, set the board aside')
    if not booted or not any('no V5F image installed' in x for x in lines):
        raise Fail(f'stage 1 didn\'t come back in recovery with no image installed ({booted})')
    say('Locked: stage 1 found the option bytes as it wants them.')
    say()
    say('## Enrolling')
    u = s.usb()
    s.console(u, RECOVERY_PROMPT)
    text = s.command(u, 'enroll', RECOVERY_PROMPT, 30)
    u.close()
    found = CSV_LINE.search(text)
    if not found:
        raise Fail('enroll gave no boards.csv line')
    luid, pub = found.group(1), found.group(2)
    if 'new key made' not in text:
        raise Fail('enroll didn\'t make a new key: the board had a record already. Set it aside')
    match = same_uid(uid, luid)
    if match is None:
        raise Fail(f'enroll\'s UID {luid} isn\'t the probe\'s {uid}: a board swapped? Set it aside')
    if match:
        say(f'(the probe gives the UID\'s bytes {match}: {uid})')
    if luid in st.enrolled_uids() or luid in dict(b.lines()):
        raise Fail(f'UID {luid} already enrolled: set the board aside, tell the release manager')
    if pub in st.enrolled_keys() or pub in set(p for _, p in b.lines()):
        raise Fail(f'its public key is another board\'s: its RNG may be broken. Set it aside, '
                   'tell the release manager')
    state['uid'] = luid
    say(f'boards.csv line: `{luid},{pub}`')
    say()
    say('## The probe\'s view (last: it halts the V3F)')
    text, _, prot = ld.status(wl)
    block('wlink status', text)
    try:
        obr, wpr = wl.option_bytes()
        say(ld.ob_text(obr, wpr))
    finally:
        ld.release(wl, False)
    if not prot or not ld.locked(obr, wpr):
        raise Fail('the probe doesn\'t see the board locked down: set it aside')
    with open(b.enrolled, 'a', newline='\n') as f:
        f.write(f'{luid},{pub}\n')
    os.remove(started)
    say()
    say(f'**Result:** enrolled, `{luid},{pub}`.')
    return luid


def read_flash(wl, address, n):
    import tempfile
    fd, out = tempfile.mkstemp(suffix='.bin')
    os.close(fd)
    try:
        code, text = wl.run(['dump', hex(address), str(n), '-o', out])
        with open(out, 'rb') as f:
            data = f.read()
    finally:
        os.remove(out)
    if code or len(data) < n:
        raise Fail(f'the probe couldn\'t read {address:#010x}:\n{text}')
    return data[:n]


def board_head(title, b, m, a):
    return (f'# {title}\n\n'
            f'- Batch: {b.name}; release version {m["revision"]}, product {m["product"]:#x}, '
            f'release key {m["serial"]}, manifest sha256 {m["sha256"]}\n'
            f'- Station: {platform.node()}; operator: {a.operator}; {now()}\n\n')


def cmd_pass1(a):
    st = Station(a.station)
    m = st.manifest()
    b = st.batch(a.batch)
    b.check_release(m)
    s = Session(a)
    state = {}
    done, failed = [], 0
    while True:
        if ask('\nConnect the next board (its card in, the WCH-LinkE on its debug header, its '
               'USB to this PC), then press Enter; or type q and Enter to stop.').lower() == 'q':
            break
        state.pop('uid', None)
        ld.report.clear()
        head = None
        try:
            uid = pass1_board(s, st, b, m, state)
            head = board_head(f'Board {uid}, pass 1', b, m, a)
            write_record(b.record(uid), head)
            done.append(uid)
            print(f'\nBoard {uid} enrolled: label it {uid}, then unplug it.', flush=True)
        except (Fail, OSError) as e:
            say()
            say(f'**Stopped:** {e}')
            uid = state.get('uid') or 'unknown'
            path = os.path.join(b.records, f'{uid}-failed-{time.strftime("%Y%m%d-%H%M%S")}.md')
            write_record(path, board_head(f'Board {uid}, pass 1, stopped', b, m, a))
            failed += 1
            print(f'\nThis board stopped (report: {path}). Set it aside as section 8 of '
                  'provisioning.md says.', flush=True)
    print(f'\npass 1: {len(done)} board(s) enrolled in this run, {failed} stopped; '
          f'{len(b.lines())} in batch {b.name}.')
    return 1 if failed else 0


# ---------------------------------------------------------------------------
# batch-review (the vendor's)
# ---------------------------------------------------------------------------
def cmd_batch_review(a):
    d = a.batch
    enrolled = os.path.join(d, 'enrolled.csv')
    lines = read_lines(enrolled)
    if not lines:
        raise Fail(f'{enrolled} lists no boards')
    old = read_lines(a.boards, missing_ok=True)
    known, known_keys = set(u for u, _ in old), set(p for _, p in old)
    seen, keys, problems = set(), set(), []
    for uid, pub in lines:
        if uid in seen:
            problems.append(f'{uid}: twice in the batch')
        if pub in keys or pub in known_keys:
            problems.append(f'{uid}: its public key is another board\'s (a broken RNG?)')
        seen.add(uid)
        keys.add(pub)
        if uid in known:
            problems.append(f'{uid}: in {a.boards} already')
        rec = os.path.join(d, 'records', uid + '.md')
        try:
            with open(rec, encoding='utf-8') as f:
                text = f.read()
        except OSError:
            problems.append(f'{uid}: no record ({rec})')
            continue
        if f'**Result:** enrolled, `{uid},{pub}`.' not in text:
            problems.append(f'{uid}: its record doesn\'t end enrolled with this line')
        if 'Locked: stage 1 found the option bytes' not in text or 'new key made' not in text:
            problems.append(f'{uid}: its record doesn\'t show the lock and a new key')
    if problems:
        raise Fail('the batch doesn\'t check out:\n  ' + '\n  '.join(problems))
    n = min(a.split, PACKAGE_BOARDS)
    parts = [lines[i:i + n] for i in range(0, len(lines), n)]
    names = []
    for i, part in enumerate(parts, 1):
        name = os.path.join(d, 'enrolled.csv' if len(parts) == 1 else f'enrolled-{i}.csv')
        if len(parts) > 1:
            with open(name, 'w', newline='\n') as f:
                f.writelines(f'{u},{p}\n' for u, p in part)
        names.append(name)
    print(f'{enrolled}: {len(lines)} board(s), each with its record, none in {a.boards}.')
    if a.append:
        with open(a.boards, 'a', newline='\n') as f:
            f.write(f'# batch {os.path.basename(os.path.abspath(d))}, added {now()}\n')
            f.writelines(f'{u},{p}\n' for u, p in lines)
        print(f'{a.boards}: {len(lines)} line(s) added. Commit it, reviewed.')
    else:
        print(f'Add them to {a.boards} (--append does), and commit it, reviewed.')
    print('Then package the release for each part, and check it:')
    for i, name in enumerate(names, 1):
        pkg = 'batch.lfu' if len(names) == 1 else f'batch-{i}.lfu'
        print(f'  sealblocks.py package LiteForth_V5F.bin {pkg} --component app --pages lfpages.bin '
              f'--release RELEASE.key --boards {name} --revision VERSION')
        print(f'  sealblocks.py check {pkg} --root root.pub')
    return 0


# ---------------------------------------------------------------------------
# pass2
# ---------------------------------------------------------------------------
def load_packages(paths, m, root):
    """Each package checked: (path, its recipients' UIDs, sha256)"""
    out = []
    for p in paths:
        with open(p, 'rb') as f:
            data = f.read()
        try:
            h = sb.verify(data, root, product=m['product'])
        except sb.SealError as e:
            raise Fail(f'{p}: {e}')
        if h['package']['component'] != sb.COMPONENTS['app']:
            raise Fail(f'{p}: not an app package (level 2 installs only those)')
        if h['revision'] != m['revision']:
            raise Fail(f'{p}: version {h["revision"]}, but the release is version {m["revision"]}')
        uids = set(u.hex() for u, _ in h['recipients'])
        digest = hashlib.sha256(data).hexdigest()
        say(f'- `{os.path.basename(p)}`: version {h["revision"]}, {len(uids)} board(s), '
            f'release key {h["serial"]}, sha256 {digest}')
        out.append((p, uids, digest))
    return out


def run_test(s, u, path):
    """Each line COMMAND ==> TEXT: TEXT must be in the reply"""
    with open(path, encoding='utf-8') as f:
        for i, line in enumerate(f, 1):
            line = line.rstrip('\r\n')
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            if '==>' not in line:
                raise Fail(f'{path} line {i}: COMMAND ==> TEXT')
            cmd, want = (x.strip() for x in line.split('==>', 1))
            got = s.command(u, cmd, 'ok>', 20)
            if want not in got:
                raise Fail(f'the product test: `{cmd}` didn\'t give {want!r}')
            say(f'- `{cmd}`: {want!r}, as it should')


def pass2_board(s, st, b, m, pkgs, state):
    want = m['revision']
    say('## The board')
    u = s.usb()
    ok, text = s.console(u, RECOVERY_PROMPT)
    if not ok:
        u.close()
        raise Fail('no recovery console: the board has a release installed already, or isn\'t '
                   'one of this batch\'s')
    text = s.command(u, 'enroll', RECOVERY_PROMPT, 30)
    found = CSV_LINE.search(text)
    if not found:
        raise Fail('enroll gave no boards.csv line')
    uid, pub = found.group(1), found.group(2)
    state['uid'] = uid
    lines = dict(b.lines())
    if uid not in lines or not os.path.exists(b.record(uid)):
        raise Fail(f'{uid} has no pass 1 record in batch {b.name}')
    if lines[uid] != pub:
        raise Fail(f'{uid}\'s key isn\'t the one pass 1 recorded: its key was made again. '
                   'Set it aside')
    mine = [p for p in pkgs if uid in p[1]]
    if not mine:
        raise Fail(f'{uid} isn\'t a recipient of any package given: is it in the batch the '
                   'vendor packaged?')
    pkg, _, digest = mine[0]
    say()
    say(f'## Installing `{os.path.basename(pkg)}` (sha256 {digest})')
    start = s.dbg.mark()
    u0 = len(u.text)
    if not u.ymodem(pkg):
        block('Cli port', bytes(u.text[u0:]).decode('latin-1'))
        raise Fail('the package didn\'t go across (YMODEM): try again')
    line = s.dbg.wait(r'V3F update: version \d+ installed|V3F update refused|update: stopped',
                      start, 120)
    time.sleep(0.5)
    block('Cli port', bytes(u.text[u0:]).decode('latin-1'))
    u.close()
    if not line or f'version {want} installed' not in line:
        block('Debug port', '\n'.join(s.dbg.since(start)))
        raise Fail(f'stage 1 didn\'t install it ({line})')
    confirmed = s.dbg.wait(rf'V3F update: version {want} confirmed', start, 60)
    level, version, lline = boot_level(s.dbg, start, 1)
    lines_ = s.dbg.since(start)
    block('Debug port', '\n'.join(lines_))
    if level != 2 or version != want:
        raise Fail(f'stage 1 doesn\'t report level 2 and version {want} ({lline})')
    if not confirmed:
        raise Fail('the release wasn\'t confirmed: it didn\'t start. Set the board aside')
    for x in lines_:
        v = re.search(r'LiteForth starting, security level (\d+)', x)
        if v and v.group(1) != '2':
            raise Fail(f'the V5F reports level {v.group(1)}: ({x})')
    say()
    say('## Checking')
    for boot in (1, 2):
        u = s.usb()
        s.console(u, 'ok>')
        text = s.command(u, 'revision .', 'ok>')
        got = re.search(r'(\d+)\s*ok>', text)
        if not got or int(got.group(1)) != want:
            u.close()
            raise Fail(f'`revision .` doesn\'t give {want}')
        if boot == 1 and s.a.test:
            run_test(s, u, s.a.test)
        if boot == 2:
            u.close()
            break
        start = s.dbg.mark()
        u.write('reboot\r')
        time.sleep(0.3)
        u.close()
        started = s.dbg.wait(r'LiteForth starting|recovery mode', start, 40)
        level, version, lline = boot_level(s.dbg, start, 1)
        block('Debug port, after `reboot`', '\n'.join(s.dbg.since(start)))
        if not started or 'LiteForth' not in started or level != 2 or version != want:
            raise Fail(f'after `reboot` the release didn\'t start again ({lline or started})')
    with open(b.installed, 'a', newline='\n') as f:
        f.write(f'{uid},{want},{digest},{now()}\n')
    say()
    say(f'**Result:** version {want} installed and checked.')
    return uid


def cmd_pass2(a):
    st = Station(a.station)
    m = st.manifest()
    b = st.batch(a.batch)
    b.check_release(m)
    if NO_SERIAL:
        raise Fail(NO_SERIAL)
    ld.report.clear()
    pkgs = load_packages(a.package, m, sb.root_public(os.path.join(st.release, 'root.pub')))
    pkg_text = ''.join(ld.report)
    ld.report.clear()
    print(pkg_text, end='')
    missing = [u for u, _ in b.lines() if not any(u in p[1] for p in pkgs)]
    if missing:
        print(f'note: {len(missing)} board(s) of the batch aren\'t in these packages: '
              + ' '.join(missing[:8]) + (' ...' if len(missing) > 8 else ''))
    s = Session(a)
    state = {}
    done, failed = [], 0
    while True:
        if ask('\nConnect the next board (its USB to this PC, the WCH-LinkE\'s UART too), then '
               'press Enter; or type q and Enter to stop.').lower() == 'q':
            break
        state.pop('uid', None)
        ld.report.clear()
        try:
            uid = pass2_board(s, st, b, m, pkgs, state)
            write_record(b.record(uid), '\n' + board_head(f'Board {uid}, pass 2', b, m, a)
                         .replace('# ', '## ', 1) + '### The packages\n\n' + pkg_text + '\n', 'a')
            done.append(uid)
            print(f'\nBoard {uid} done: version {m["revision"]} installed. Unplug it.', flush=True)
        except (Fail, OSError) as e:
            say()
            say(f'**Stopped:** {e}')
            uid = state.get('uid') or 'unknown'
            path = os.path.join(b.records, f'{uid}-pass2-failed-{time.strftime("%Y%m%d-%H%M%S")}.md')
            write_record(path, board_head(f'Board {uid}, pass 2, stopped', b, m, a))
            failed += 1
            print(f'\nThis board stopped (report: {path}). See section 8 of provisioning.md.',
                  flush=True)
    print(f'\npass 2: {len(done)} board(s) done in this run, {failed} stopped.')
    return 1 if failed else 0


# ---------------------------------------------------------------------------
def main(argv=None):
    p = argparse.ArgumentParser(description='Provision new boards (doc/admin/provisioning.md)',
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=__doc__.split('\n\n', 1)[1])
    sub = p.add_subparsers(dest='cmd', required=True)

    def station(s):
        s.add_argument('--station', default='.', help='the station\'s folder (default: here)')

    def board(s):
        station(s)
        s.add_argument('--batch', required=True)
        s.add_argument('--cli', '--usb', dest='usb', help='the board\'s USB terminal '
                       '(default: found by its USB IDs)')
        s.add_argument('--debug', default=os.environ.get('HIL_DEBUG'),
                       help='the WCH-LinkE\'s UART (default: found by its USB IDs)')
        s.add_argument('--wlink', help='the wlink program (default: on the PATH)')
        s.add_argument('--operator', default=os.environ.get('PROVISION_OPERATOR')
                       or getpass.getuser())

    s = sub.add_parser('station-check', help='check the release and the station')
    station(s)
    s.add_argument('--wlink')
    s = sub.add_parser('batch-new', help='start a batch')
    s.add_argument('name')
    station(s)
    s = sub.add_parser('card', help='prepare a microSD card in this PC\'s reader')
    s.add_argument('drive', help='the card: a drive (E:) or a mounted folder')
    station(s)
    s.add_argument('--any-fs', action='store_true', help=argparse.SUPPRESS)  # tests: a folder
    s = sub.add_parser('pass1', help='flash, lock and enroll boards')
    board(s)
    s = sub.add_parser('pass2', help='install the release on enrolled boards')
    board(s)
    s.add_argument('--package', action='append', required=True,
                   help='the batch\'s package (more than one for a batch in parts)')
    s.add_argument('--test', help='the product\'s test: lines COMMAND ==> TEXT')
    s = sub.add_parser('batch-review', help='the vendor\'s check of a batch')
    s.add_argument('batch', help='the batch\'s folder, as the station sent it')
    s.add_argument('--boards', required=True, help='the vendor\'s boards.csv')
    s.add_argument('--append', action='store_true', help='add the batch\'s lines to it')
    s.add_argument('--split', type=int, default=PACKAGE_BOARDS,
                   help=f'boards per package (at most {PACKAGE_BOARDS})')
    a = p.parse_args(argv)
    try:
        return {'station-check': cmd_station_check, 'batch-new': cmd_batch_new,
                'card': cmd_card, 'pass1': cmd_pass1, 'pass2': cmd_pass2,
                'batch-review': cmd_batch_review}[a.cmd](a)
    except (Fail, OSError) as e:
        print(f'provision: {e}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('\nprovision: stopped', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
