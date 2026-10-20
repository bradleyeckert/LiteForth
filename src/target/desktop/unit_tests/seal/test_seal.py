#!/usr/bin/env python3
"""Tests scripts/admin/sealblocks.py: round trips with AES and SM4, the counter
layout, and each refusal a board makes (doc/admin/encrypted-blocks.md section 4).
Runs in a temporary directory on a copy of bin/lfblocks.fb4."""
import contextlib
import io
import os
import shutil
import struct
import hashlib
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '../../../../..'))
sys.path.insert(0, os.path.join(ROOT, 'scripts', 'admin'))
import sealblocks as sb  # noqa: E402

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey  # noqa: E402
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey  # noqa: E402,E501
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes  # noqa: E402

BLOCK = sb.BLOCK
failures = 0


def run(*args):
    """Runs sealblocks.py in-process; returns (status, stdout + stderr)."""
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            status = sb.main([str(a) for a in args])
        except SystemExit as e:
            status = e.code
    return status, out.getvalue()


def expect(name, ok, detail=''):
    global failures
    if not ok:
        failures += 1
        print(f'FAIL: {name} {detail}')


def expect_ok(name, *args):
    status, out = run(*args)
    expect(name, status == 0, out)
    return out


def expect_refused(name, why, *args):
    status, out = run(*args)
    expect(name, status == 1 and why in out, f'(status {status}) {out.strip()}')


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def write(path, data):
    with open(path, 'wb') as f:
        f.write(data)


def resign(data, release_key):
    """data with block 0 signed again by the release key, so a test reaches
    the checks after the signature."""
    priv = Ed25519PrivateKey.from_private_bytes(
        bytes.fromhex(sb.read_json(release_key, 'release')['private']))
    b0 = bytearray(data[:BLOCK])
    b0[sb.SIG_AT:] = priv.sign(bytes(b0[:sb.SIG_AT]))
    return bytes(b0) + data[BLOCK:]


def main():
    tmp = tempfile.mkdtemp()
    try:
        os.chdir(tmp)
        plain = 'plain.fb4'
        shutil.copy(os.path.join(ROOT, 'bin', 'lfblocks.fb4'), plain)
        pdata = read(plain)
        c = len(pdata) // BLOCK

        # keys
        expect_ok('root key', 'root-key', 'root')
        expect_ok('second root key', 'root-key', 'other')
        expect_refused('root key not overwritten', 'exists', 'root-key', 'root')
        expect_ok('release key', 'release-key', 'root.key', 1, 'rel1.json', '--product', 0x4C46)
        expect_ok('release key 2', 'release-key', 'root.key', 2, 'rel2.json', '--product', 0x4C46)
        expect_ok('release key, other root', 'release-key', 'other.key', 3, 'rogue.json',
                  '--product', 0x4C46)
        expect_ok('release key, other product', 'release-key', 'root.key', 1, 'prod2.json',
                  '--product', 0x1234)
        lines = []
        for name, uid in (('a', '0102030405060708'), ('b', '1112131415161718'),
                          ('c', '2122232425262728')):
            lines.append(expect_ok('board key ' + name, 'board-key', name + '.json',
                                   '--uid', uid).strip())
        with open('boards.csv', 'w') as f:
            f.write('# test boards\n' + lines[0] + '\n\n' + lines[1] + '\n')

        # board-key --insecure: the key a CH32H417 below SECURITY_LEVEL 2 derives
        # from its UID, BLAKE2s (person LFinsec1) of the UID's 8 bytes
        uid = '69a90e647f9cf6a9'
        l1 = expect_ok('insecure board key', 'board-key', 'i1.json', '--uid', uid,
                       '--insecure').strip()
        l2 = expect_ok('insecure board key again', 'board-key', 'i2.json', '--uid', uid,
                       '--insecure').strip()
        expect('insecure board key is the same each time',
               l1.split(',')[:2] == l2.split(',')[:2], l1 + ' / ' + l2)
        priv = hashlib.blake2s(bytes.fromhex(uid), digest_size=32,
                               person=b'LFinsec1').digest()
        expect('insecure board key is BLAKE2s of the UID',
               sb.read_json('i1.json', 'board')['private'] == priv.hex())
        expect('insecure board line', l1.split(',')[:2] == [uid, sb.raw_public(
            X25519PrivateKey.from_private_bytes(priv)).hex()], l1)
        expect_refused('insecure board key needs a UID', '--uid', 'board-key', 'i3.json',
                       '--insecure')

        # root-header: the C header a CH32H417 build reads the root key from
        expect_ok('root-header', 'root-header', 'root.pub', '--product', 0x4C46, '-o', 'root.h')
        with open('root.h') as f:
            header = f.read()
        pub = bytes.fromhex(sb.read_json('root.pub', 'root')['public'])
        listed = bytes(int(x, 16) for x in __import__('re').findall(r'0x([0-9a-f]{2}),?', header.split('SEALED_ROOT_KEY')[1].split('}')[0]))
        expect('root-header holds the root key', listed == pub, header)
        expect('root-header holds the product', 'SEALED_PRODUCT 0x00004c46u' in header, header)
        expect_refused('root-header not overwritten', 'exists', 'root-header', 'root.pub',
                       '--product', 1, '-o', 'root.h')
        expect('root-header: no security flags (SECURITY_LEVEL has them)',
               not any(f in header for f in ('SEALED_ONLY', 'INSECURE', 'STRICT_RDP')), header)

        # firmware update packages (doc/admin/firmware-updates.md section 5)
        image = bytes((i * 7 + 3) & 255 for i in range(9000))      # not a whole block
        write('fw.bin', image)
        base_pk = ['--release', 'rel1.json', '--boards', 'boards.csv']
        out = expect_ok('package v5f', 'package', 'fw.bin', 'fw.lfu', '--component', 'v5f',
                        *base_pk, '--revision', 3, '--hw-rev', 2, '--min-version', 2)
        expect('package says what it made', '9000 bytes in 3 block(s), version 3' in out, out)
        pk = read('fw.lfu')
        expect('package: 3 content blocks and a table', len(pk) == 5 * BLOCK, len(pk))
        expect('package: block 0 starts with LITEFORTHF', pk[:10] == b'LITEFORTHF')
        expect('package: the description at 180',
               struct.unpack_from('<BBHII', pk, 180) == (1, 0, 2, 9000, 2),
               struct.unpack_from('<BBHII', pk, 180))
        expect('package: the image hash at 4000', pk[4000:4032] == hashlib.blake2s(
            image, digest_size=32, person=b'LFimage').digest())
        out = expect_ok('check a package', 'check', 'fw.lfu', '--root', 'root.pub')
        expect('check describes the package',
               'a firmware package: v5f image, 9000 bytes, version 3, hardware revision 2, '
               'new minimum 2' in out, out)
        expect_ok('open a package', 'open', 'fw.lfu', 'fw.out', '--root', 'root.pub',
                  '--board-key', 'a.json')
        expect('package round trip: the image exactly', read('fw.out') == image)
        expect_refused('a package for another board', 'not a recipient', 'open', 'fw.lfu',
                       'x.out', '--root', 'root.pub', '--board-key', 'c.json')
        big = bytes(range(256)) * 276                                  # 68 KB + 1 KB
        write('big.bin', big)
        write('s1.bin', big[:68 * 1024])
        expect_ok('package stage1, SM4', 'package', 's1.bin', 's1.lfu', '--component', 'stage1',
                  *base_pk, '--revision', 1, '--cipher', 'sm4')
        expect_ok('open stage1', 'open', 's1.lfu', 's1.out', '--root', 'root.pub',
                  '--board-key', 'a.json')
        expect('stage1 round trip', read('s1.out') == big[:68 * 1024])
        expect_refused('stage1 image too big', 'larger than the stage1 slot', 'package',
                       'big.bin', 'x.lfu', '--component', 'stage1', *base_pk, '--revision', 1)
        write('empty.bin', b'')
        expect_refused('empty image', 'is empty', 'package', 'empty.bin', 'x.lfu',
                       '--component', 'v5f', *base_pk, '--revision', 1)
        expect_refused('a block file as an image', 'is a block file', 'package', 'plain.fb4',
                       'x.lfu', '--component', 'v5f', *base_pk, '--revision', 1)
        expect_refused('seal refuses a package', 'not a plain block file', 'seal', 'fw.lfu',
                       'x.fb4', *base_pk, '--revision', 1)
        for name, at, value, why in (
                ('unknown component', 180, 9, 'unknown component'),
                ('image flags', 181, 1, 'unknown image flags'),
                ('length too long', 185, 0x40, "doesn't fill"),
                ('data before the image hash', 3990, 1, 'reserved bytes')):
            d = bytearray(pk)
            d[at] = value
            write('bad.lfu', resign(bytes(d), 'rel1.json'))
            expect_refused('package refused: ' + name, why, 'check', 'bad.lfu',
                           '--root', 'root.pub')
        d = bytearray(pk)
        d[4000] ^= 1
        write('bad.lfu', resign(bytes(d), 'rel1.json'))
        expect_ok('a wrong image hash passes check (it needs the key)', 'check', 'bad.lfu',
                  '--root', 'root.pub')
        expect_refused('open: a wrong image hash', "doesn't match its hash in block 0", 'open',
                       'bad.lfu', 'x.out', '--root', 'root.pub', '--board-key', 'a.json')
        d = bytearray(pk)
        d[4000] ^= 1
        write('bad.lfu', bytes(d))
        expect_refused('a changed image hash breaks the signature', "signature doesn't match",
                       'check', 'bad.lfu', '--root', 'root.pub')

        # round trips
        for cipher in ('aes', 'sm4'):
            sealed = f'sealed-{cipher}.fb4'
            expect_ok('seal ' + cipher, 'seal', plain, sealed, '--release', 'rel1.json',
                      '--boards', 'boards.csv', '--revision', 5, '--cipher', cipher)
            sdata = read(sealed)
            t = (c + 255) // 256
            expect('sealed size ' + cipher, len(sdata) == (c + t) * BLOCK, len(sdata))
            expect('sealed header ' + cipher, sdata[:10] == b'LITEFORTHS')
            expect('content encrypted ' + cipher,
                   all(sdata[n * BLOCK:(n + 1) * BLOCK] != pdata[n * BLOCK:(n + 1) * BLOCK]
                       for n in range(1, c)))
            expect_ok('check ' + cipher, 'check', sealed, '--root', 'root.pub')
            for board in ('a', 'b'):
                out = f'open-{cipher}-{board}.fb4'
                expect_ok(f'open {cipher} {board}', 'open', sealed, out, '--root', 'root.pub',
                          '--board-key', board + '.json', '--min-revision', 5,
                          '--min-serial', 1, '--product', 0x4C46)
                odata = read(out)
                expect(f'round trip {cipher} {board}',
                       len(odata) == len(pdata) and odata[BLOCK:] == pdata[BLOCK:]
                       and odata[:9] == b'LITEFORTH')

        # The counter: 16 bytes at offset 16 i of block n use nonce || n * 256 + i,
        # so any piece of any block decrypts on its own.
        sdata = read('sealed-aes.fb4')
        h = sb.parse_header(sdata[:BLOCK], len(sdata) // BLOCK)
        cek = sb.unwrap(h, sb.read_json('a.json', 'board'))
        for n, i in ((1, 0), (1, 255), (c - 1, 100)):
            ctr = h['nonce'] + (n * 256 + i).to_bytes(8, 'big')
            piece = sdata[n * BLOCK + 16 * i:n * BLOCK + 16 * i + 16]
            dec = Cipher(algorithms.AES(cek), modes.CTR(ctr)).decryptor().update(piece)
            expect(f'counter block {n} piece {i}',
                   dec == pdata[n * BLOCK + 16 * i:n * BLOCK + 16 * i + 16])

        # The layout of doc/admin/encrypted-blocks.md, computed here from its formulas
        # and offsets rather than with sealblocks.py's own helpers.
        b0 = sdata[:BLOCK]
        ver, ciph, flags, prod, rev, cc, tt, rr = struct.unpack_from('<BBIIIIHH', b0, 10)
        expect('doc: header fields', (ver, ciph, flags, prod, rev, cc, tt, rr)
               == (1, 1, 0, 0x4C46, 5, c, (c + 255) // 256, 2))
        nonce = b0[32:40]
        expect('doc: CEK check', b0[72:80] == hashlib.blake2s(
            nonce, digest_size=8, key=cek, person=b'LFcheck').digest())
        table = sdata[cc * BLOCK:(cc + 1) * BLOCK]
        expect('doc: table hash', b0[192:224] == hashlib.blake2s(
            table, digest_size=32, salt=(0).to_bytes(8, 'little'), person=b'LFtable').digest())
        expect('doc: entry 0 is zero', table[:16] == bytes(16))
        for n in (1, 2, cc - 1):
            expect(f'doc: entry {n}', table[16 * n:16 * n + 16] == hashlib.blake2s(
                sdata[n * BLOCK:(n + 1) * BLOCK], digest_size=16,
                salt=n.to_bytes(8, 'little'), person=b'LFblock').digest())
        board = sb.read_json('b.json', 'board')
        entry = b0[192 + 32 * tt + 24:192 + 32 * tt + 48]       # the second board
        expect('doc: recipient UID', entry[:8] == bytes.fromhex(board['uid']))
        priv = X25519PrivateKey.from_private_bytes(bytes.fromhex(board['private']))
        shared = priv.exchange(X25519PublicKey.from_public_bytes(b0[40:72]))
        kek = hashlib.blake2s(b0[40:72] + bytes.fromhex(board['public']), digest_size=16,
                              key=shared, salt=struct.pack('<II', 5, 0x4C46),
                              person=b'LFwrap').digest()
        expect('doc: wrapped CEK', bytes(a ^ b for a, b in zip(entry[8:], kek)) == cek)
        root = sb.root_public('root.pub')
        try:
            root.verify(b0[116:180], b0[16:20] + b0[80:116])
            sb.Ed25519PublicKey.from_public_bytes(b0[84:116]).verify(b0[4032:], b0[:4032])
            expect('doc: signatures', True)
        except Exception as e:
            expect('doc: signatures', False, repr(e))

        # every release has a new key and nonce
        expect_ok('seal again', 'seal', plain, 'again.fb4', '--release', 'rel1.json',
                  '--boards', 'boards.csv', '--revision', 5)
        again = read('again.fb4')
        expect('fresh nonce and key', again[32:80] != sdata[32:80]
               and again[BLOCK:2 * BLOCK] != sdata[BLOCK:2 * BLOCK])

        # refusals: wrong board, root, product and minimums
        s = 'sealed-aes.fb4'
        base = ('--root', 'root.pub')
        expect_refused('not a recipient', 'not a recipient', 'open', s, 'x.fb4', *base,
                       '--board-key', 'c.json')
        expect_refused('wrong root', 'certificate', 'check', s, '--root', 'other.pub')
        expect_refused('old revision', 'revision 5 is below 6', 'open', s, 'x.fb4', *base,
                       '--board-key', 'a.json', '--min-revision', 6)
        expect_refused('retired release key', 'serial 1 is below 2', 'open', s, 'x.fb4',
                       *base, '--board-key', 'a.json', '--min-serial', 2)
        expect_refused('other product', 'product', 'open', s, 'x.fb4', *base,
                       '--board-key', 'a.json', '--product', 0x1234)
        expect_ok('rogue release key seals', 'seal', plain, 'rogue.fb4', '--release',
                  'rogue.json', '--boards', 'boards.csv', '--revision', 9)
        expect_refused('release key from another root', 'certificate', 'check', 'rogue.fb4',
                       *base)

        # refusals: damage
        def damaged(name, at, why, resign_with=None):
            d = bytearray(sdata)
            d[at] ^= 1
            d = bytes(d)
            if resign_with:
                d = resign(d, resign_with)
            write('bad.fb4', d)
            expect_refused(name, why, 'open', 'bad.fb4', 'x.fb4', *base,
                           '--board-key', 'a.json')

        damaged('flipped content bit', 3 * BLOCK + 77, "block 3 doesn't match")
        damaged('flipped last content bit', c * BLOCK - 1, f"block {c - 1} doesn't match")
        damaged('flipped hash table bit', c * BLOCK + 40, "hash table block")
        damaged('flipped header bit', 20, 'signature')
        damaged('flipped signature bit', BLOCK - 1, 'signature')
        damaged('flipped certificate bit', 120, 'certificate')
        rcp = sb.TABLE_HASHES + 32 * h['t']
        damaged('flipped wrapped key, signed again', rcp + 8 + 3, 'CEK check', 'rel1.json')
        damaged('flipped UID, signed again', rcp, 'not a recipient', 'rel1.json')
        damaged('flipped revision, signed again', 20, 'CEK check', 'rel1.json')
        damaged('reserved byte, signed again', 3000, 'reserved', 'rel1.json')

        # a malicious ephemeral key of small order (here all zeros), signed by
        # the real release key: X25519 must not give a usable shared secret
        d = bytearray(sdata)
        d[40:72] = bytes(32)
        write('bad.fb4', resign(bytes(d), 'rel1.json'))
        expect_refused('small-order ephemeral key', 'X25519', 'open', 'bad.fb4', 'x.fb4',
                       *base, '--board-key', 'a.json')

        # header fields a board checks before anything else
        for name, at, value, why in (('format version', 10, 2, 'format version'),
                                     ('cipher', 11, 9, 'cipher'),
                                     ('flags', 12, 1, 'flags')):
            d = bytearray(sdata)
            d[at] = value
            write('bad.fb4', resign(bytes(d), 'rel1.json'))
            expect_refused(name, why, 'check', 'bad.fb4', *base)
        d = bytearray(sdata)
        struct.pack_into('<H', d, 28, h['t'] + 1)
        write('bad.fb4', resign(bytes(d), 'rel1.json'))
        expect_refused('table count', 'hash table blocks', 'check', 'bad.fb4', *base)
        write('bad.fb4', sdata[:-BLOCK])
        expect_refused('truncated file', 'header needs', 'check', 'bad.fb4', *base)
        expect_refused('plain file', 'LITEFORTHS', 'check', plain, *base)

        # input checks when sealing
        expect_refused('seal a sealed file', 'not a plain block file', 'seal', s, 'x.fb4',
                       '--release', 'rel1.json', '--boards', 'boards.csv', '--revision', 1)
        with open('dup.csv', 'w') as f:
            f.write(lines[0] + '\n' + lines[0] + '\n')
        expect_refused('board listed twice', 'listed twice', 'seal', plain, 'x.fb4',
                       '--release', 'rel1.json', '--boards', 'dup.csv', '--revision', 1)
        with open('many.csv', 'w') as f:
            for i in range(200):
                f.write(f'{i:016x},{lines[0].split(",")[1]}\n')
        expect_refused('too many boards', "don't fit", 'seal', plain, 'x.fb4',
                       '--release', 'rel1.json', '--boards', 'many.csv', '--revision', 1)
        write('odd.fb4', pdata + b'x')
        expect_refused('partial block', 'whole number', 'seal', 'odd.fb4', 'x.fb4',
                       '--release', 'rel1.json', '--boards', 'boards.csv', '--revision', 1)

        # a file with more than one hash table block
        big = pdata + b' ' * BLOCK * (300 - c)
        write('big.fb4', big)
        expect_ok('seal 300 blocks', 'seal', 'big.fb4', 'bigs.fb4', '--release', 'rel2.json',
                  '--boards', 'boards.csv', '--revision', 7)
        expect_ok('open 300 blocks', 'open', 'bigs.fb4', 'bigo.fb4', '--root', 'root.pub',
                  '--board-key', 'b.json', '--min-serial', 2)
        expect('round trip 300 blocks', read('bigo.fb4')[BLOCK:] == big[BLOCK:])
        bd = bytearray(read('bigs.fb4'))
        bd[290 * BLOCK] ^= 0x80
        write('bad.fb4', bytes(bd))
        expect_refused('flipped bit under table 1', "block 290 doesn't match", 'check',
                       'bad.fb4', *base)
    finally:
        os.chdir(HERE)
        shutil.rmtree(tmp)

    if failures:
        print(f'{failures} seal test(s) failed')
        return 1
    print('seal tests passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
