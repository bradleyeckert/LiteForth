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
        self.fat = start + struct.unpack_from('<I', self.data, b + 80)[0]
        self.heap = start + struct.unpack_from('<I', self.data, b + 88)[0]
        self.clusters = struct.unpack_from('<I', self.data, b + 92)[0]
        self.root = struct.unpack_from('<I', self.data, b + 96)[0]
        self.cluster_bytes = 512 << self.data[b + 109]
        self.spc = self.cluster_bytes // 512
        self.files = {}
        self.owned = set(self.chain(self.root))
        for at in self.entries():         # up to the end-of-directory entry, as the spec says
            t = self.data[at]
            if t in (0x81, 0x82):         # the bitmap, the up-case table
                first, length = struct.unpack_from('<IQ', self.data, at + 20)
                self.owned.update(self.chain(first, length))
                if t == 0x81:
                    self.bitmap = self.sector(first) * 512
            if t == 0x85:
                st = at + 32
                n = self.data[st + 3]
                name = self.data[at + 66:at + 66 + 2 * n].decode('utf-16-le')
                first = struct.unpack_from('<I', self.data, st + 20)[0]
                length = struct.unpack_from('<Q', self.data, st + 24)[0]
                valid = struct.unpack_from('<Q', self.data, st + 8)[0]
                self.files[name] = (first, length, valid)
                runs = (range(first, first + -(-length // self.cluster_bytes))
                        if self.data[st + 1] & 2 else self.chain(first, length))
                self.owned.update(runs)

    def entries(self):
        for c in self.chain(self.root):
            for at in range(self.sector(c) * 512, self.sector(c) * 512 + self.cluster_bytes, 32):
                if self.data[at] == 0:
                    return
                yield at

    def chain(self, c, length=None):
        out = []
        while 2 <= c < 0xFFFFFFF7 and (length is None or len(out) * self.cluster_bytes < length):
            out.append(c)
            c = struct.unpack_from('<I', self.data, self.fat * 512 + 4 * c)[0]
        return out

    def sector(self, c):
        return self.heap + (c - 2) * self.spc

    def used(self, c):
        i = c - 2
        return bool(self.data[self.bitmap + i // 8] & (1 << (i % 8)))

    def unowned(self):
        """Clusters marked used that nothing owns (space lost, never two owners)"""
        return [c for c in range(2, self.clusters + 2) if self.used(c) and c not in self.owned]

    def free_owned(self):
        """Clusters a file owns but the bitmap says are free: that's the bad one"""
        return [c for c in self.owned if not self.used(c)]

    def contents(self, name):
        first, length, _ = self.files[name]
        at = self.sector(first) * 512
        return bytes(self.data[at:at + length])


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
    check(not v.unowned() and not v.free_owned(), 'the bitmap: every used cluster owned, '
          'every owned one used')

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
    for cut in list(range(0, 80)) + list(range(80, total, 5)) + [total - 1]:
        img = os.path.join(WORK, 'cut.img')
        shutil.copy(base, img)
        r = write(img, 'cut.bin', 6, 9, cut=cut)
        if r.returncode != 3:
            check(False, f'cut {cut}: the cut happened', r.stdout + r.stderr)
            continue
        rec = run([DLTEST, 'recover', img, str(BYTES)])
        length, valid = cat(img, 'cut.bin')
        v = Volume('cut.img')
        check(not v.free_owned(), f'cut {cut}: no owned cluster is free in the bitmap')
        if length is None:          # cut before the entry set, or a file deleted by recovery
            check('cut.bin' not in rec.stdout or 'cut.bin 0 0' in rec.stdout,
                  f'cut {cut}: deleted, not a log', rec.stdout)
            outcomes['no file'] = outcomes.get('no file', 0) + 1
            continue
        first = v.files['cut.bin'][0]
        check(len(v.unowned()) <= BYTES // CLUSTER, f'cut {cut}: at most one file\'s '
              f'clusters lost ({len(v.unowned())})')
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


def test_many_logs():
    """Twelve logs on one card: each one visible after the end-of-directory
    entry moves past it (three entries a log), each opens, the bitmap exact"""
    img = card('many.img')
    for i in range(12):
        r = write(img, f'm{i}.bin', 2, 100 + i)
        check(r.returncode == 0, f'log m{i}', r.stdout + r.stderr)
    v = Volume('many.img')
    check(all(f'm{i}.bin' in v.files for i in range(12)), 'all twelve in the directory, '
          'before its end', str(sorted(v.files)))
    check(not v.unowned() and not v.free_owned(), 'the bitmap, exact', str(v.unowned()))
    for i in range(12):
        length, valid = cat(img, f'm{i}.bin')
        code, out = lflog(f'm{i}.bin.log')
        check(valid == 3 * RECORD and code == 0 and read('out.bin') == read(f'm{i}.bin.plain'),
              f'm{i} opens', out)
    r = write(img, 'm3.bin', 1, 1, ok=(1,))
    check(r.returncode == 1, 'a name used far down the directory is still taken')


def delete(path, name):
    """What a PC does: the entry set unused, the bits cleared; the data stays"""
    v = Volume(path)
    first, length, _ = v.files[name]
    for at in v.entries():
        if v.data[at] == 0x85 and v.data[at + 66:at + 66 + 2 * len(name)] == name.encode('utf-16-le'):
            for k in range(v.data[at + 1] + 1):
                v.data[at + 32 * k] &= 0x7F
    for c in range(first, first + -(-length // v.cluster_bytes)):
        i = c - 2
        v.data[v.bitmap + i // 8] &= ~(1 << (i % 8)) & 0xFF
    with open(os.path.join(WORK, path), 'wb') as f:
        f.write(v.data)


def test_stale():
    """A new log on a deleted one's clusters, cut before its block 0 is
    whole: recovery never brings the old log back"""
    base = card('stale.img')
    write(base, 'old.bin', 10, 50)
    delete('stale.img', 'old.bin')
    for cut in range(0, 72):
        img = os.path.join(WORK, 'st.img')
        shutil.copy(base, img)
        r = write(img, 'new.bin', 4, 51, cut=cut)
        if r.returncode != 3:
            continue
        rec = run([DLTEST, 'recover', img, str(BYTES)])
        length, valid = cat(img, 'new.bin')
        if length is None:
            continue
        code, out = lflog('new.bin.log', out='st.out')
        check(valid < RECORD or 'not a data log' in out or 'torn' in out or '10 records' not in out,
              f'stale cut {cut}: the deleted log doesn\'t come back', out + rec.stdout)


def test_lflog_edges():
    with open(os.path.join(WORK, 'short.log'), 'wb') as f:
        f.write(b'LFLOG001x')
    code, out = lflog('short.log')
    check(code == 1 and 'not a data log' in out, 'a short file is no log', out)
    # a log copied off the card before recovery trimmed it: the whole 4 MB
    img = card('untrim.img')
    write(img, 'u.bin', 3, 60, close=False)
    v = Volume('untrim.img')
    with open(os.path.join(WORK, 'u.log'), 'wb') as f:
        f.write(v.contents('u.bin'))
    code, out = lflog('u.log', out='u.out')
    check(code == 0 and 'wasn\'t trimmed' in out and 'not closed' in out
          and out.count('problem') == 0 and read('u.out') == read('u.bin.plain'),
          'an untrimmed file: one note, every record there', out)
    # a board at level 0 or 1: its key comes from its UID
    sys.path.insert(0, os.path.join(ROOT, 'scripts', 'admin'))
    import sealblocks
    from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
    dev = sealblocks.insecure_key(bytes.fromhex(UID))
    shutil.copy(os.path.join(WORK, 'board.bin'), os.path.join(WORK, 'board.real'))
    with open(os.path.join(WORK, 'board.bin'), 'wb') as f:
        f.write(dev)
    pub = X25519PrivateKey.from_private_bytes(dev).public_key().public_bytes_raw().hex()
    with open(os.path.join(WORK, 'dev.csv'), 'w') as f:
        f.write(f'{UID},{pub},dev\n')
    img = card('dev.img')
    write(img, 'd.bin', 2, 70)
    cat(img, 'd.bin')
    code, out = lflog('d.bin.log', boards='dev.csv')
    check(code == 0 and 'WARNING' in out and 'development key' in out,
          'a development board\'s log is flagged', out)
    shutil.copy(os.path.join(WORK, 'board.real'), os.path.join(WORK, 'board.bin'))


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
    test_many_logs()
    test_stale()
    test_lflog_edges()
    if failures:
        print(f'datalog tests: {failures} of {checks} checks failed')
        return 1
    print(f'datalog tests passed ({checks} checks)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
