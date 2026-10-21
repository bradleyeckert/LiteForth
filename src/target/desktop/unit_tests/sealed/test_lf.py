#!/usr/bin/env python3
"""lf and sealed files: a firmware update package or a retired sealed block
file (block 0 LITEFORTHF or LITEFORTHS) is never a block file, as a file or
inside a card image, and `-1 ymodem` never installs one; `-2 ymodem` stages
packages and checks them with lf -K (a board file) as stage 1 will.
Usage: test_lf.py LF"""
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
sys.path.insert(0, os.path.join(ROOT, 'scripts', 'admin'))
import sealblocks as sb  # noqa: E402
sys.path.insert(0, os.path.join(HERE, '..', 'ymodem'))
import sender  # noqa: E402

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


def install(name, path, *args, block=-1):
    """Runs lf with args, installs the file at path with -1 ymodem (or
    stages it with -2), and returns what lf said."""
    shutil.copy(os.path.join(ROOT, 'bin', 'lfflash.bin'), 'lfflash.bin')
    with open(path, 'rb') as f:
        data = f.read()
    p = sender.Lf(LF, '.', [str(a) for a in args])
    p.drain(0.5)
    sender.transfer(p, [(name, data)], block=block, size=1024)
    p.write(b'capacity .\nbye\n')
    p.drain(0.5)
    p.p.wait(timeout=10)
    return p.text.decode('utf-8', 'replace')


def stage_tests(c, sums):
    """-2 ymodem: a firmware update package into lfstage.bin"""
    board = ('-k', 'space.fb4', '-K', 'a.lfb')
    with open('fw.lfu', 'rb') as f:
        pk = f.read()
    n = len(pk) // BLOCK
    with open('lfstage.bin', 'wb') as f:
        f.write(bytes(8 * BLOCK))
    _, loaded = lf(LOAD, *board)
    _, cap0 = lf('capacity .\nbye\n', *board)
    cap0 = body(cap0).split()[0]
    got = install('fw.lfu', 'fw.lfu', *board, block=-2)
    expect('stage: staged', f'staged in lfstage.bin, {n} blocks, read back and checked' in got,
           got)
    expect('stage: checked', 'package: V5F image, 10240 bytes, version 3: signature, '
           'recipient and blocks check out; waiting for stage 1' in got, got)
    with open('lfstage.bin', 'rb') as f:
        st = f.read()
    expect('stage: written exactly', st[:len(pk)] == pk and st[len(pk):] == bytes(8 * BLOCK -
           len(pk)) and len(st) == 8 * BLOCK)
    expect('stage: the block file still mounted', body(got).strip().endswith(cap0), got)
    _, got2 = lf(LOAD, *board)
    expect('stage: the block file unchanged', body(got2) == body(loaded), got2)

    got = install('fw.lfu', 'fw.lfu', '-k', 'space.fb4', block=-2)
    expect('stage without -K: staged, not checked', 'staged in lfstage.bin' in got and
           'package refused: not checked: lf has no board file (-K)' in got, got)
    for why, lfb in (("isn't one of its recipients", 'c.lfb'),
                     ('has been replaced', 'serial.lfb'),
                     ('another product', 'product.lfb'),
                     ("certificate doesn't match", 'otherroot.lfb')):
        got = install('fw.lfu', 'fw.lfu', '-k', 'space.fb4', '-K', lfb, block=-2)
        expect('stage refused: ' + why, 'staged in lfstage.bin' in got and why in got
               and "stage 1 won't install it" in got, got)
    # made for this board's UID but another key (the board enrolled again
    # since): the V5F can't tell without the key; stage 1 refuses it
    tool('board-key', 'a2.key', '--uid', '0102030405060708')
    with open('a2.key') as f:
        a2 = sb.json.load(f)
    with open('boards_a2.csv', 'w') as f:
        f.write(f'{a2["uid"]},{a2["public"]},a2\n')
    tool('package', 'fw.bin', 'fwa2.lfu', '--component', 'v5f', '--release', 'rel1.key',
         '--boards', 'boards_a2.csv', '--revision', 3)
    got = install('fwa2.lfu', 'fwa2.lfu', *board, block=-2)
    expect("stage: another key for this UID passes (stage 1's to refuse)",
           'check out; waiting for stage 1' in got, got)
    got = install('fw.lfu', 'fw.lfu', '-k', 'space.fb4', '-K', 'newer.lfb', block=-2)
    expect("stage: the board's minimum is stage 1's to check", 'check out; waiting for '
           'stage 1' in got, got)
    before = sha('lfstage.bin')
    with open('plain.fb4', 'rb') as f:
        small = f.read(2 * BLOCK)
    with open('notpk.fb4', 'wb') as f:
        f.write(small)
    got = install('notpk.fb4', 'notpk.fb4', *board, block=-2)
    expect('stage refused: not a package', "not a firmware update package (block 0 doesn't "
           "start with LITEFORTHF); nothing written" in got, got)
    expect('stage refused: lfstage.bin unchanged', sha('lfstage.bin') == before)
    tool('package', 'fw.bin', 'fwc.lfu', '--component', 'v5f', '--release', 'rel1.key',
         '--boards', 'boards_c.csv', '--revision', 3)
    got = install('fwc.lfu', 'fwc.lfu', *board, block=-2)
    expect('stage: for another board', "package refused: package refused: this board isn't "
           "one of its recipients; stage 1 won't install it" in got
           or "isn't one of its recipients; stage 1 won't install it" in got, got)
    with open('lfstage.bin', 'wb') as f:
        f.write(bytes(2 * BLOCK))
    got = install('fw.lfu', 'fw.lfu', *board, block=-2)
    expect('stage refused: too big', "it's bigger than lfstage.bin; nothing written" in got, got)
    os.remove('lfstage.bin')
    got = install('fw.lfu', 'fw.lfu', *board, block=-2)
    expect('stage refused: no lfstage.bin', "there's no lfstage.bin to stage into" in got, got)

    # on a card image: lfstage.bin beside lfblocks.fb4
    with open('stage0.bin', 'wb') as f:
        f.write(bytes(8 * BLOCK))
    subprocess.run([sys.executable, os.path.join(ROOT, 'scripts', 'mkexfat.py'), 'scard.img',
                    '--file', 'lfblocks.fb4=plain.fb4', '--file', 'lfstage.bin=stage0.bin'],
                   check=True, capture_output=True)
    got = install('fw.lfu', 'fw.lfu', '-k', 'scard.img', '-K', 'a.lfb', block=-2)
    expect('stage on a card', 'staged in lfstage.bin' in got and 'check out; waiting for '
           'stage 1' in got, got)
    with open('scard.img', 'rb') as f:
        expect('stage on a card: the package is in the image', pk in f.read())


def install_tests(c, sums):
    with open('plain.fb4', 'rb') as f:
        plain = f.read()
    with open('space.fb4', 'wb') as f:          # room for more
        f.write(plain + b' ' * (4 * BLOCK))
    board = ('-k', 'space.fb4', '-K', 'a.lfb')
    before = sha('space.fb4')
    got = install('fw.lfu', 'fw.lfu', *board)
    expect('install refused: a package', 'a firmware update package, not a block file; '
           'nothing written' in got, got)
    expect('install refused: package left the file alone', sha('space.fb4') == before)
    for args in (board, ('-k', 'space.fb4')):
        got = install('old.fb4', 'old.fb4', *args)
        expect('install refused: a sealed block file ' + ' '.join(args),
               'a sealed block file: no longer read' in got and 'nothing written' in got, got)
        expect('install refused: sealed file left the file alone', sha('space.fb4') == before)

    got = install('plain.fb4', 'plain.fb4', *board)
    expect('install plain', f'installed, {c} blocks, read back and checked; '
           f'capacity now {c + 4}' in got, got)
    with open('space.fb4', 'rb') as f:
        expect('install plain: in the space', f.read()[:len(plain)] == plain)

    subprocess.run([sys.executable, os.path.join(ROOT, 'scripts', 'mkexfat.py'), 'card2.img',
                    '--file', 'lfblocks.fb4=old.fb4'], check=True, capture_output=True)
    got = install('plain.fb4', 'plain.fb4', '-k', 'card2.img', '-K', 'a.lfb')
    expect('install plain over a sealed file on a card', f'installed, {c} blocks' in got, got)
    _, got = lf(LOAD, '-k', 'card2.img', '-K', 'a.lfb')
    expect('card image: loads after the install', body(got) == sums, got)


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
        with open('c.key') as f:
            ck = sb.json.load(f)
        with open('boards_c.csv', 'w') as f:
            f.write(f'{ck["uid"]},{ck["public"]},c\n')
        # a retired sealed block file: here, just its magic over a plain one
        with open('plain.fb4', 'rb') as f:
            data = f.read()
        with open('old.fb4', 'wb') as f:
            f.write(b'LITEFORTHS' + data[10:])

        # the plain file, for comparison
        _, want = lf(LOAD, '-k', 'plain.fb4')
        expect('plain file loads core.f', 'value test: 11 10' in want, want)
        sums = body(want)
        _, got = lf(LOAD, '-k', 'plain.fb4', '-K', 'a.lfb')
        expect('plain file with a board file', body(got) == sums, got)

        # a sealed block file offers nothing (capacity 0), and is never written
        cap = 'capacity .\nbye\n'
        for args in (('-k', 'old.fb4', '-K', 'a.lfb'), ('-k', 'old.fb4')):
            before = sha('old.fb4')
            _, got = lf(cap + '1 block drop update save-buffers\n', *args)
            expect('sealed block file refused ' + ' '.join(args),
                   'blocks: a sealed block file: no longer read' in got
                   and body(got).strip().startswith('0'), got)
            expect('sealed block file left alone', sha('old.fb4') == before)
        subprocess.run([sys.executable, os.path.join(ROOT, 'scripts', 'mkexfat.py'), 'card.img',
                        '--file', 'lfblocks.fb4=old.fb4'], check=True, capture_output=True)
        _, got = lf(cap, '-k', 'card.img', '-K', 'a.lfb')
        expect('sealed block file refused on a card', 'a sealed block file: no longer read' in got
               and body(got).strip().startswith('0'), got)

        # a firmware update package is never a block file
        with open('fw.bin', 'wb') as f:
            f.write(bytes(range(256)) * 40)
        tool('package', 'fw.bin', 'fw.lfu', '--component', 'v5f', '--release', 'rel1.key',
             '--boards', 'boards.csv', '--revision', 3)
        for args in (('-k', 'fw.lfu', '-K', 'a.lfb'), ('-k', 'fw.lfu')):
            before = sha('fw.lfu')
            _, got = lf(cap, *args)
            expect('package refused as a block file ' + ' '.join(args),
                   'a firmware update package, not a block file' in got
                   and body(got).strip().startswith('0'), got)
            expect('package left alone', sha('fw.lfu') == before)
        subprocess.run([sys.executable, os.path.join(ROOT, 'scripts', 'mkexfat.py'), 'fwcard.img',
                        '--file', 'lfblocks.fb4=fw.lfu'], check=True, capture_output=True)
        _, got = lf(cap, '-k', 'fwcard.img', '-K', 'a.lfb')
        expect('package refused on a card', 'a firmware update package, not a block file' in got
               and body(got).strip().startswith('0'), got)

        # enroll prints the board's boards.csv line from the board file
        _, got = lf('enroll\nbye\n', '-k', 'plain.fb4', '-K', 'a.lfb')
        expect('enroll prints the boards.csv line', f'{a["uid"]},{a["public"]}' in got, got)
        _, got = lf('enroll\nbye\n', '-k', 'plain.fb4')
        expect('enroll without -K', 'no board file' in got and 'ior=-21' in got, got)

        # -1 ymodem installs whole files into a block file's space
        install_tests(c, sums)
        stage_tests(c, sums)

        # a bad board file stops lf
        with open('bad.lfb', 'w') as f:
            f.write('uid 0102\n')
        status, got = lf(cap, '-k', 'plain.fb4', '-K', 'bad.lfb')
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
