#!/usr/bin/env python3
"""Makes the keys, images and packages that test_update.c installs, in the
directory given (default work/), with scripts/admin/sealblocks.py:

    root.pub.bin    the root's Ed25519 public key, 32 bytes
    board.bin       the board: UID (8 bytes) then X25519 private key (32)
    old.bin         the image in flash at the start
    new.bin         the image the packages install
    new.lfu         new.bin, version 2, new minimum 2
    new0.lfu        new.bin, version 2, no new minimum
    v3.lfu          another image (v3.bin), version 3
    v1.lfu          new.bin as version 1 (below the minimum new.lfu sets)
    product.lfu     for another product
    other.lfu       for another board only
    stage1.lfu      a stage 1 image, not a V5F one
    hwrev.lfu       for hardware revision 7 (the test board is 1)
    pages.bin       LiteForth's flash pages, as save-pages writes them
    app.lfu         new.bin and pages.bin (component app), version 4
    app.bin         what app.lfu puts in flash: new.bin padded to the slot, then the pages
    appshort.lfu    an app package with too few pages (stage 1 refuses it)
    app5.lfu        v3.bin and pages5.bin (component app), version 5
    app5.bin        what app5.lfu puts in flash
"""
import contextlib
import io
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', '..', '..', 'scripts', 'admin'))
import sealblocks as sb  # noqa: E402

PRODUCT = 0x4C46


def tool(*args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        status = sb.main([str(a) for a in args])
    if status != 0:
        sys.exit(f'gen.py: sealblocks {args}: {out.getvalue()}')


def image(n, seed):
    return bytes((i * seed + (i >> 7)) & 255 for i in range(n))


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else 'work'
    os.makedirs(work, exist_ok=True)
    os.chdir(work)
    for f in os.listdir('.'):
        os.remove(f)
    tool('root-key', 'root')
    tool('release-key', 'root.key', 1, 'rel.key', '--product', PRODUCT)
    tool('release-key', 'root.key', 1, 'relx.key', '--product', 0x1234)
    tool('board-key', 'a.key', '--uid', '0102030405060708')
    tool('board-key', 'b.key', '--uid', '1112131415161718')
    with open('a.key') as f:
        a = json.load(f)
    with open('b.key') as f:
        b = json.load(f)
    with open('boards.csv', 'w') as f:
        f.write(f'{a["uid"]},{a["public"]},a\n')
    with open('boards_b.csv', 'w') as f:
        f.write(f'{b["uid"]},{b["public"]},b\n')
    with open('root.pub') as f:
        root = bytes.fromhex(json.load(f)['public'])
    with open('root.pub.bin', 'wb') as f:
        f.write(root)
    with open('board.bin', 'wb') as f:
        f.write(bytes.fromhex(a['uid']) + bytes.fromhex(a['private']))

    images = {'old.bin': image(20000, 3), 'new.bin': image(30000, 5), 'v3.bin': image(9000, 7)}
    for name, data in images.items():
        with open(name, 'wb') as f:
            f.write(data)
    v5f = ['--component', 'v5f', '--release', 'rel.key', '--boards', 'boards.csv']
    tool('package', 'new.bin', 'new.lfu', *v5f, '--revision', 2, '--min-version', 2)
    tool('package', 'new.bin', 'new0.lfu', *v5f, '--revision', 2)
    tool('package', 'v3.bin', 'v3.lfu', *v5f, '--revision', 3)
    tool('package', 'new.bin', 'v1.lfu', *v5f, '--revision', 1)
    tool('package', 'new.bin', 'hwrev.lfu', *v5f, '--revision', 2, '--hw-rev', 7)
    tool('package', 'new.bin', 'product.lfu', '--component', 'v5f', '--release', 'relx.key',
         '--boards', 'boards.csv', '--revision', 2)
    tool('package', 'new.bin', 'other.lfu', '--component', 'v5f', '--release', 'rel.key',
         '--boards', 'boards_b.csv', '--revision', 2)
    tool('package', 'v3.bin', 'stage1.lfu', '--component', 'stage1', '--release', 'rel.key',
         '--boards', 'boards.csv', '--revision', 2)

    # update.h's defaults, which the test builds with: a 160 KB slot, 256 KB of pages
    slot, pages_len = 160 * 1024, 256 * 1024
    # the machine-mode region (part 4): the slot's last 8 KB. The test images
    # are far shorter, so it is erased flash (0xFF) in both the signed window
    # and the installed slot; mmode-sign signs the slot-padded image's window.
    mmode_len = 0x2000
    mmode_off = slot - mmode_len                # its offset in the slot image
    mmode_addr = 0x30000 + slot - mmode_len     # UPD_V5F_ADDR + the offset (STAGE1_FLASH_BASE 0)
    mmode = ['--mmode-off', mmode_off, '--mmode-addr', mmode_addr, '--mmode-len', mmode_len]

    def mmode_sign(v5f_bin, out):
        with open(v5f_bin, 'rb') as f:
            body = f.read()
        with open('slotpad.bin', 'wb') as f:
            f.write(body + b'\xff' * (slot - len(body)))
        tool('mmode-sign', 'slotpad.bin', out, '--root', 'root.key', '--product', PRODUCT, *mmode)

    pages = image(pages_len, 11)
    with open('pages.bin', 'wb') as f:
        f.write(pages)
    with open('app.bin', 'wb') as f:
        f.write(images['new.bin'] + b'\xff' * (slot - len(images['new.bin'])) + pages)
    app = ['--component', 'app', '--release', 'rel.key', '--boards', 'boards.csv',
           '--pages', 'pages.bin', '--slot', slot]
    mmode_sign('new.bin', 'mm_app.sig')
    tool('package', 'new.bin', 'app.lfu', *app, '--revision', 4, '--mmode-sig', 'mm_app.sig')
    tool('package', 'new.bin', 'appshort.lfu', *app, '--pages-len', 192 * 1024, '--revision', 4,
         '--mmode-sig', 'mm_app.sig')
    pages5 = image(pages_len, 13)
    with open('pages5.bin', 'wb') as f:
        f.write(pages5)
    with open('app5.bin', 'wb') as f:
        f.write(images['v3.bin'] + b'\xff' * (slot - len(images['v3.bin'])) + pages5)
    app5 = ['--component', 'app', '--release', 'rel.key', '--boards', 'boards.csv',
            '--pages', 'pages5.bin', '--slot', slot]
    mmode_sign('v3.bin', 'mm_app5.sig')
    tool('package', 'v3.bin', 'app5.lfu', *app5, '--revision', 5, '--mmode-sig', 'mm_app5.sig')
    # a package whose machine-mode region is signed for different bytes: the
    # window set to zeros, so stage 1's flash hash (0xFF) won't match it
    with open('slotbad.bin', 'wb') as f:
        f.write(images['new.bin'] + b'\xff' * (slot - mmode_len - len(images['new.bin']))
                + b'\x00' * mmode_len)
    tool('mmode-sign', 'slotbad.bin', 'mm_bad.sig', '--root', 'root.key', '--product', PRODUCT,
         *mmode)
    tool('package', 'new.bin', 'appbadmm.lfu', *app, '--revision', 4, '--mmode-sig', 'mm_bad.sig')


if __name__ == '__main__':
    main()
