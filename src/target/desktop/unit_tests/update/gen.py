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
    pages = image(pages_len, 11)
    with open('pages.bin', 'wb') as f:
        f.write(pages)
    with open('app.bin', 'wb') as f:
        f.write(images['new.bin'] + b'\xff' * (slot - len(images['new.bin'])) + pages)
    app = ['--component', 'app', '--release', 'rel.key', '--boards', 'boards.csv',
           '--pages', 'pages.bin', '--slot', slot]
    tool('package', 'new.bin', 'app.lfu', *app, '--revision', 4)
    tool('package', 'new.bin', 'appshort.lfu', *app, '--pages-len', 192 * 1024, '--revision', 4)


if __name__ == '__main__':
    main()
