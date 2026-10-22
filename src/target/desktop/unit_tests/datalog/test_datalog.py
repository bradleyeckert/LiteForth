#!/usr/bin/env python3
"""Data logs end to end (doc/admin/data-logs.md): dltest (src/datalog.c,
src/logfile.c and exfat_create/exfat_trim, as the board runs them) writes
logs into card images from mkexfat.py, and scripts/admin/lflog.py, the
server's side, opens them. Checks:

- a log round trip: the board named, every record's tag, the sample
  blocks byte for byte, the application header, closed;
- the exFAT side: the file made on a run of free clusters with its bits
  set, then trimmed to the blocks written with the rest cleared, and a
  second log beside it; a name that's taken refused;
- tampering: a changed byte in a record, in the header's identity; two
  records swapped; a record from another log; the wrong board key;
- power cuts at every stride of sector writes, through making, writing
  and closing: the next start's recovery closes the log at its last whole
  record, which the server opens, every record checked, reported as not
  closed (or closed, if the cut came in the close itself), and the bitmap
  never gives the file's clusters away.
"""
import json
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..', '..'))
WORK = os.path.join(HERE, 'work')
DLTEST = os.path.join(HERE, 'dltest')
LFLOG = [sys.executable, os.path.join(ROOT, 'scripts', 'admin', 'lflog.py')]
MKEXFAT = [sys.executable, os.path.join(ROOT, 'scripts', 'mkexfat.py')]
RECORD = 32768
BYTES = 4 * 1024 * 1024                 # the log files here: 128 blocks (4 GB on the board)
CLUSTER = 32768
UID = '0102030405060708'

failures = checks = 0


def check(ok, what, detail=''):
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
        print(f'FAIL: {what}' + (f'\n{detail}' if detail else ''))


def run(cmd, ok_codes=(0,)):
    r = subprocess.run(cmd, cwd=WORK, capture_output=True, text=True)
    if r.returncode not in ok_codes:
        print(f'{" ".join(cmd)}: exit {r.returncode}\n{r.stdout}{r.stderr}')
    return r


def keys():
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
    run(LFLOG + ['server-key', 'server', '--key-id', '7'])
    with open(os.path.join(WORK, 'server.key')) as f:
        S = bytes.fromhex(json.load(f)['public'])
    with open(os.path.join(WORK, 'server.pub.bin'), 'wb') as f:
        f.write(S)
    for name in ('board', 'other'):
        k = X25519PrivateKey.generate()
        with open(os.path.join(WORK, name + '.bin'), 'wb') as f:
            f.write(k.private_bytes_raw())
        with open(os.path.join(WORK, name + '.csv'), 'w') as f:
            f.write(f'{UID},{k.public_key().public_bytes_raw().hex()},{name}\n')


def card(name='card.img'):
    path = os.path.join(WORK, name)
    run(MKEXFAT + [path, '--size-mb', '32', '--cluster', str(CLUSTER),
                   '--file', 'lfblocks.fb4,size=65536'])
    return path


def write(img, name, records, seed, cut=None, close=True, ok=(0, 3)):
    cmd = [DLTEST, 'write' if close else 'open', img, name, str(BYTES), str(records), str(seed),
           'server.pub.bin', 'board.bin', name + '.plain']
    if cut is not None:
        cmd.append(str(cut))
    return run(cmd, ok_codes=ok)


def cat(img, name):
    r = run([DLTEST, 'cat', img, name, name + '.log'], ok_codes=(0, 1))
    if r.returncode:
        return None, None
    length, valid = map(int, r.stdout.split())
    return length, valid


def lflog(log, boards='board.csv', out='out.bin'):
    r = run(LFLOG + ['open', log, '--server-key', 'server.key', '--boards', boards, '-o', out,
                     '--app-header', 'app.bin'], ok_codes=(0, 1))
    return r.returncode, r.stdout


def read(path):
    with open(os.path.join(WORK, path), 'rb') as f:
        return f.read()


# The exFAT side, read back independently of exfat.c
class Volume:
    def __init__(self, path):
        self.data = bytearray(read(path))
        start = 0
        if self.data[3:11] != b'EXFAT   ':
            start = struct.unpack_from('<I', self.data, 446 + 8)[0]
        b = start * 512
        self.heap = start + struct.unpack_from('<I', self.data, b + 88)[0]
        self.clusters = struct.unpack_from('<I', self.data, b + 92)[0]
        self.root = struct.unpack_from('<I', self.data, b + 96)[0]
        self.spc = 1 << self.data[b + 109]
        self.files = {}
        at = self.sector(self.root) * 512
        while True:                     # the root fits its first cluster here
            t = self.data[at]
            if t == 0:
                break
            if t == 0x81:
                self.bitmap = self.sector(struct.unpack_from('<I', self.data, at + 20)[0]) * 512
            if t == 0x85:
                count = self.data[at + 1]
                s = at + 32
                n = self.data[s + 3]
                name = self.data[at + 66:at + 66 + 2 * n].decode('utf-16-le')
                self.files[name] = (struct.unpack_from('<I', self.data, s + 20)[0],
                                    struct.unpack_from('<Q', self.data, s + 24)[0],
                                    struct.unpack_from('<Q', self.data, s + 8)[0])
                at += 32 * count
            at += 32

    def sector(self, c):
        return self.heap + (c - 2) * self.spc

    def used(self, c):
        i = c - 2
        return bool(self.data[self.bitmap + i // 8] & (1 << (i % 8)))


def test_round_trip():
    img = card()
    r = write(img, 'log1.bin', 20, 1)
    check(r.returncode == 0, 'write a log', r.stdout + r.stderr)
    length, valid = cat(img, 'log1.bin')
    check(length == valid == 21 * RECORD, f'closed at 21 blocks ({length}, {valid})')
    code, out = lflog('log1.bin.log')
    check(code == 0 and 'closed' in out and 'not closed' not in out
          and 'board 0102030405060708' in out and 'firmware 42' in out, 'lflog opens it', out)
    check(read('out.bin') == read('log1.bin.plain'), 'the sample blocks, byte for byte')
    check(read('app.bin') == bytes((i * 7) & 255 for i in range(1024)), 'the application header')
    check('20 records, 19 * 255 + 100'.split(',')[0] in out
          and f'{19 * 255 + 100} sample blocks' in out, 'records and sample blocks counted', out)

    v = Volume('card.img')
    first, flen, fvalid = v.files['log1.bin']
    used = 21 * RECORD // CLUSTER
    whole = BYTES // CLUSTER
    check(all(v.used(first + i) for i in range(used)), 'its clusters are marked used')
    check(not any(v.used(first + i) for i in range(used, whole)), 'the trimmed ones are free')

    r = write(img, 'log1.bin', 3, 2, ok=(1,))
    check(r.returncode == 1, 'the same name again is refused', r.stdout + r.stderr)
    r = write(img, 'log2.bin', 5, 3)
    check(r.returncode == 0, 'a second log', r.stdout + r.stderr)
    v = Volume('card.img')
    a, b = v.files['log1.bin'][0], v.files['log2.bin'][0]
    check(b >= a + used, f'the second log after the first one\'s end ({a}, {b})')
    cat(img, 'log2.bin')
    code, out = lflog('log2.bin.log')
    check(code == 0 and read('out.bin') == read('log2.bin.plain'), 'the second log opens', out)


def tampered(what, change, expect, boards='board.csv'):
    data = bytearray(read('log1.bin.log'))
    change(data)
    with open(os.path.join(WORK, 't.log'), 'wb') as f:
        f.write(data)
    code, out = lflog('t.log', boards=boards, out='t.out')
    check(code == 1 and expect in out, f'tampering: {what}', out)


def test_tampering():
    def flip(at):
        def f(d):
            d[at] ^= 1
        return f

    def swap(d):
        d[2 * RECORD:3 * RECORD], d[3 * RECORD:4 * RECORD] = \
            d[3 * RECORD:4 * RECORD], d[2 * RECORD:3 * RECORD]

    other = read('log2.bin.log')

    def foreign(d):
        d[2 * RECORD:3 * RECORD] = other[2 * RECORD:3 * RECORD]

    def refit(d):                       # a record whose frame is made to fit: the tag still fails
        d[2 * RECORD:3 * RECORD] = other[2 * RECORD:3 * RECORD]
        d[2 * RECORD + 4:2 * RECORD + 12] = d[16:24]
        d[3 * RECORD - 12:3 * RECORD - 4] = d[16:24]

    tampered('a byte of record 3', flip(3 * RECORD + 5000), 'record 3 fails its tag')
    tampered('record 3\'s sample count', flip(3 * RECORD + 16), 'record 3 fails its tag')
    tampered('the identity', flip(60), 'board')
    tampered('the application header', flip(500), 'header\'s tag')
    tampered('records 2 and 3 swapped', swap, 'block 2 isn\'t record 2')
    tampered('a record from another log', foreign, 'block 2 isn\'t record 2')
    tampered('another log\'s record, framed to fit', refit, 'record 2 fails its tag')
    tampered('another board\'s key in boards.csv', lambda d: None, 'header\'s tag',
             boards='other.csv')


def test_power_cuts():
    """Every 5th sector write of making, writing and closing a 6-record log"""
    base = card('base.img')
    shutil.copy(base, os.path.join(WORK, 'ref.img'))
    r = write(os.path.join(WORK, 'ref.img'), 'cut.bin', 6, 9)
    total = int(r.stdout.split()[0])
    plain = read('cut.bin.plain')
    outcomes = {}
    for cut in list(range(0, total, 5)) + [total - 1]:
        img = os.path.join(WORK, 'cut.img')
        shutil.copy(base, img)
        r = write(img, 'cut.bin', 6, 9, cut=cut)
        if r.returncode != 3:
            check(False, f'cut {cut}: the cut happened', r.stdout + r.stderr)
            continue
        rec = run([DLTEST, 'recover', img, str(BYTES)])
        length, valid = cat(img, 'cut.bin')
        v = Volume('cut.img')
        if length is None:
            outcomes['no file'] = outcomes.get('no file', 0) + 1
            continue                    # cut before the entry set: nothing to see
        first = v.files['cut.bin'][0]
        if valid == 0:                  # block 0 never written: recovery leaves it
            check('cut.bin 0' in rec.stdout and length == BYTES,
                  f'cut {cut}: a file with no log is left alone', rec.stdout)
            check(all(v.used(first + i) for i in range(BYTES // CLUSTER)),
                  f'cut {cut}: its clusters stay used')
            outcomes['empty file'] = outcomes.get('empty file', 0) + 1
            continue
        blocks = valid // RECORD
        check(valid % RECORD == 0 and length == valid, f'cut {cut}: closed on a block ({valid})')
        check(all(v.used(first + i) for i in range(valid // CLUSTER)),
              f'cut {cut}: the log\'s clusters stay used')
        code, out = lflog('cut.bin.log', out='cut.out')
        got = read('cut.out')
        check(code == 0, f'cut {cut}: every record checks out', out)
        check(got == plain[:len(got)], f'cut {cut}: the sample blocks so far')
        if 'not closed' in out:
            outcomes['recovered'] = outcomes.get('recovered', 0) + 1
            check(blocks <= 7, f'cut {cut}: {blocks} blocks')
        else:
            outcomes['closed'] = outcomes.get('closed', 0) + 1
            check(got == plain, f'cut {cut}: a closed log is all there')
    print('power cuts:', ', '.join(f'{k} {v}' for k, v in sorted(outcomes.items())))
    check(outcomes.get('recovered', 0) > 20, 'most cuts were recovered logs', str(outcomes))


def main():
    try:
        import cryptography  # noqa: F401
    except ImportError:
        print('datalog tests skipped: python3 with the cryptography package not found')
        return 0
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    keys()
    test_round_trip()
    test_tampering()
    test_power_cuts()
    if failures:
        print(f'datalog tests: {failures} of {checks} checks failed')
        return 1
    print(f'datalog tests passed ({checks} checks)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
