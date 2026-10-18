#!/usr/bin/env python3
"""lf on sealed block files (doc/encrypted-blocks.md, step 3): sealblocks.py
seals a copy of bin/lfblocks.fb4, and lf -K (a board file) must load core.f
from it and read every block exactly as from the plain file, with AES and
SM4, as a file and inside a card image; and must refuse what a board
refuses. Usage: test_lf.py LF"""
import contextlib
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '../../../../..'))
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import sealblocks as sb  # noqa: E402

LF = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, 'bin', 'lf')
BLOCK = 4096
PRODUCT = 0x4C46
failures = 0

# Load core.f as block 1 does, then print a sum of every content block
LOAD = ('0 open-flash 7 load 50 load\n'
        ': bsum ( n -- x ) block 0 swap 1024 for @+ rot + swap next drop ;\n'
        ': sums capacity 1- for r@ bsum . next ;\n'
        'close-flash\nhex sums decimal\nbye\n')


def expect(name, ok, detail=''):
    global failures
    if not ok:
        failures += 1
        print(f'FAIL: {name}\n{detail}')


def tool(*args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        status = sb.main([str(a) for a in args])
    if status != 0:
        raise RuntimeError(f'sealblocks {args}: {out.getvalue()}')


def lf(cmds, *args):
    """Runs lf on a fresh copy of the flash image; returns its exit status and
    its output, stderr (the blocks: lines) first."""
    shutil.copy(os.path.join(ROOT, 'bin', 'lfflash.bin'), 'lfflash.bin')
    r = subprocess.run([LF, '-o', '3'] + [str(a) for a in args], input=cmds.encode(),
                       capture_output=True, timeout=60)
    return r.returncode, (r.stderr + r.stdout).decode('utf-8', 'replace')


def body(out):
    """lf's output without the banner and the blocks: lines."""
    return '\n'.join(l for l in out.splitlines() if not l.startswith(('blocks:', '[')))


def sha(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def main():
    tmp = tempfile.mkdtemp()
    try:
        os.chdir(tmp)
        shutil.copy(os.path.join(ROOT, 'bin', 'lfblocks.fb4'), 'plain.fb4')
        c = os.path.getsize('plain.fb4') // BLOCK
        tool('root-key', 'root')
        tool('root-key', 'other')
        tool('release-key', 'root.key', 1, 'rel1.key', '--product', PRODUCT)
        tool('release-key', 'root.key', 1, 'relx.key', '--product', 0x1234)
        tool('board-key', 'a.key', '--uid', '0102030405060708')
        tool('board-key', 'c.key', '--uid', '2122232425262728')
        with open('a.key') as f:
            a = sb.json.load(f)
        with open('boards.csv', 'w') as f:
            f.write(f'{a["uid"]},{a["public"]},a\n')
        tool('lf-board', 'a.lfb', '--board-key', 'a.key', '--root', 'root.pub',
             '--product', PRODUCT)
        tool('lf-board', 'c.lfb', '--board-key', 'c.key', '--root', 'root.pub',
             '--product', PRODUCT)
        tool('lf-board', 'newer.lfb', '--board-key', 'a.key', '--root', 'root.pub',
             '--product', PRODUCT, '--min-revision', 6)
        tool('lf-board', 'serial.lfb', '--board-key', 'a.key', '--root', 'root.pub',
             '--product', PRODUCT, '--min-serial', 2)
        tool('lf-board', 'product.lfb', '--board-key', 'a.key', '--root', 'root.pub',
             '--product', 0x1234)
        tool('lf-board', 'otherroot.lfb', '--board-key', 'a.key', '--root', 'other.pub',
             '--product', PRODUCT)
        tool('seal', 'plain.fb4', 'aes.fb4', '--release', 'rel1.key', '--boards',
             'boards.csv', '--revision', 5)
        tool('seal', 'plain.fb4', 'sm4.fb4', '--release', 'rel1.key', '--boards',
             'boards.csv', '--revision', 5, '--cipher', 'sm4')

        # the plain file, for comparison
        _, want = lf(LOAD, '-k', 'plain.fb4')
        expect('plain file loads core.f', 'value test: 11 10' in want, want)
        sums = body(want)

        for name in ('aes', 'sm4'):
            before = sha(name + '.fb4')
            _, got = lf(LOAD, '-k', name + '.fb4', '-K', 'a.lfb')
            expect(f'{name}: mounted', f'blocks: sealed file, revision 5, release key 1, '
                   f'{"SM4" if name == "sm4" else "AES-128"}, {c} blocks' in got, got)
            expect(f'{name}: loads core.f and reads every block as the plain file',
                   body(got) == sums, got)
            expect(f'{name}: file unchanged', sha(name + '.fb4') == before)

        # inside a card image, as the board finds it
        subprocess.run([sys.executable, os.path.join(ROOT, 'scripts', 'mkexfat.py'), 'card.img',
                        '--file', 'lfblocks.fb4=aes.fb4'], check=True, capture_output=True)
        _, got = lf(LOAD, '-k', 'card.img', '-K', 'a.lfb')
        expect('card image: mounted', 'blocks: sealed file, revision 5' in got, got)
        expect('card image: same blocks', body(got) == sums, got)

        # refusals: the file offers nothing (capacity 0)
        cap = 'capacity .\nbye\n'
        for why, args in (
                ('no board file (-K)', ('-k', 'aes.fb4')),
                ("isn't one of its recipients", ('-k', 'aes.fb4', '-K', 'c.lfb')),
                ('an older revision', ('-k', 'aes.fb4', '-K', 'newer.lfb')),
                ('has been replaced', ('-k', 'aes.fb4', '-K', 'serial.lfb')),
                ('another product', ('-k', 'aes.fb4', '-K', 'product.lfb')),
                ("certificate doesn't match", ('-k', 'aes.fb4', '-K', 'otherroot.lfb'))):
            _, got = lf(cap, *args)
            expect(f'refused: {why}', why in got and body(got).strip().startswith('0'), got)

        def damaged(name, at, value=None):
            with open('aes.fb4', 'rb') as f:
                d = bytearray(f.read())
            d[at] = value if value is not None else d[at] ^ 1
            with open(name, 'wb') as f:
                f.write(d)

        damaged('hdr.fb4', 20)                          # the revision, unsigned
        _, got = lf(cap, '-k', 'hdr.fb4', '-K', 'a.lfb')
        expect('refused: changed header', "signature doesn't match" in got, got)
        damaged('ver.fb4', 10, 2)
        _, got = lf(cap, '-k', 'ver.fb4', '-K', 'a.lfb')
        expect('refused: format version', 'unknown format version' in got, got)
        with open('aes.fb4', 'rb') as f:
            data = f.read()
        with open('short.fb4', 'wb') as f:
            f.write(data[:-BLOCK])
        _, got = lf(cap, '-k', 'short.fb4', '-K', 'a.lfb')
        expect('refused: truncated', 'shorter than its header' in got, got)

        # damaged blocks: mounted, but the block fails its hash (-33)
        damaged('blk.fb4', 7 * BLOCK + 100)
        _, got = lf('6 block drop\n7 block drop\n8 block drop\nbye\n', '-k', 'blk.fb4',
                    '-K', 'a.lfb')
        expect('damaged block 7 refused, blocks 6 and 8 fine', got.count('ior=-33') == 1, got)
        damaged('tbl.fb4', c * BLOCK + 50)
        _, got = lf('3 block @ .\nbye\n', '-k', 'tbl.fb4', '-K', 'a.lfb')
        expect('damaged hash table refused', 'ior=-33' in got, got)

        # nothing can be written, and block 0 reads as it is
        before = sha('aes.fb4')
        _, got = lf('1 block drop update save-buffers\n0 block drop\nbye\n',
                    '-k', 'aes.fb4', '-K', 'a.lfb')
        expect('write refused, block 0 readable', got.count('ior=') == 1 and 'ior=-96' in got,
               got)
        expect('file unchanged after a write', sha('aes.fb4') == before)

        # enroll prints the board's boards.csv line from the board file
        _, got = lf('enroll\nbye\n', '-k', 'aes.fb4', '-K', 'a.lfb')
        expect('enroll prints the boards.csv line', f'{a["uid"]},{a["public"]}' in got, got)
        _, got = lf('enroll\nbye\n', '-k', 'plain.fb4')
        expect('enroll without -K', 'no board file' in got and 'ior=-21' in got, got)

        # a bad board file stops lf
        with open('bad.lfb', 'w') as f:
            f.write('uid 0102\n')
        status, got = lf(cap, '-k', 'aes.fb4', '-K', 'bad.lfb')
        expect('bad board file', status != 0 and 'bad setting' in got, got)
    finally:
        os.chdir(HERE)
        shutil.rmtree(tmp)
    if failures:
        print(f'{failures} sealed lf test(s) failed')
        return 1
    print('sealed lf tests passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
