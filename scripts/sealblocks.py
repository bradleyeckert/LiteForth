#!/usr/bin/env python3
"""Sealed block files: encrypted, signed lfblocks.fb4 (doc/encrypted-blocks.md).

    sealblocks.py root-key ROOT                    make root key pair ROOT.key, ROOT.pub
    sealblocks.py release-key ROOT.key SERIAL OUT --product P
                                                   make a release key, certified by the root
    sealblocks.py board-key OUT [--uid HEX] [--insecure]
                                                   make a board key pair (for testing; a real
                                                   board makes its own) and print its boards.csv line;
                                                   --insecure: the key an INSECURE build derives
                                                   from that UID
    sealblocks.py lf-board OUT --board-key B --root ROOT.pub --product P
                  [--min-revision N] [--min-serial N] [--sealed-only]
                                                   write a board file for the desktop lf (-K)
    sealblocks.py root-header ROOT.pub --product P [--sealed-only] [-o FILE]
                                                   write sealed_root.h (the root key and product
                                                   ID that a CH32H417 build reads sealed files with;
                                                   --sealed-only: SEALED_ONLY, no plain files)
    sealblocks.py seal PLAIN SEALED --release R --boards CSV --revision N [--cipher aes|sm4]
    sealblocks.py package IMAGE PACKAGE --component v5f|stage1 --release R --boards CSV
                  --revision N [--hw-rev N] [--min-version N] [--cipher aes|sm4]
                                                   make a firmware update package from a .bin
                                                   (doc/firmware-updates.md section 5)
    sealblocks.py check SEALED --root ROOT.pub     check everything that needs no board key
    sealblocks.py open SEALED PLAIN --root ROOT.pub --board-key B
                  [--min-revision N] [--min-serial N] [--product P]
                                                   check and decrypt, as a board would; for a
                                                   package, PLAIN gets the image itself

Key files are JSON. *.key files hold private keys: keep the root's offline.
boards.csv: one line per board, `UID,public key,name`, UID and key in hex
(the UID is the 8 bytes at ESIG_UNIID1, as they lie in memory). Lines that
are empty or start with # are skipped.

Needs the `cryptography` package (pip install cryptography); BLAKE2s comes
from hashlib. Exit status: 0 ok, 1 a check failed or a key or file was
refused, 2 a file couldn't be read or written (argparse also uses 2).
"""
import argparse
import csv
import hashlib
import json
import os
import struct
import sys

try:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import serialization
    from cryptography.hazmat.primitives.asymmetric.ed25519 import (
        Ed25519PrivateKey, Ed25519PublicKey)
    from cryptography.hazmat.primitives.asymmetric.x25519 import (
        X25519PrivateKey, X25519PublicKey)
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
except ImportError:
    sys.exit("sealblocks.py needs the cryptography package: "
             "pip install cryptography (on Windows: py -m pip install cryptography)")

BLOCK = 4096
SIGNATURE = b'LITEFORTH'
SEALED_MARK = b'S'
PACKAGE_MARK = b'F'             # a firmware update package (doc/firmware-updates.md)
FORMAT_VERSION = 1
CIPHERS = {'aes': 1, 'sm4': 2}
CIPHER_NAMES = {v: k for k, v in CIPHERS.items()}
ENTRY = 16                      # bytes per block hash
PER_TABLE = BLOCK // ENTRY      # 256 block hashes per table block
TABLE_HASHES = 192              # offset of the table hashes in block 0
RECIPIENT = 24                  # UID (8) + wrapped CEK (16)
SIG_AT = BLOCK - 64             # the release key's signature
IMAGE_HASH_AT = SIG_AT - 32     # a package's plaintext image hash
COMPONENTS = {'v5f': 1, 'stage1': 2}
COMPONENT_NAMES = {v: k for k, v in COMPONENTS.items()}
COMPONENT_LIMIT = {1: 160 * 1024, 2: 68 * 1024}     # the slots in firmware-updates.md section 2
PAD = 0xFF                      # erased flash


class SealError(Exception):
    """A sealed file or key that fails a check."""


def b2s(data, size, key=b'', salt=b'', person=b''):
    return hashlib.blake2s(data, digest_size=size, key=key, salt=salt,
                           person=person).digest()


def block_hash(n, encrypted):
    """The hash table entry for encrypted content block n."""
    return b2s(encrypted, 16, salt=n.to_bytes(8, 'little'), person=b'LFblock')


def table_hash(t, table):
    """The hash of table block t, kept in block 0."""
    return b2s(table, 32, salt=t.to_bytes(8, 'little'), person=b'LFtable')


def image_hash(image):
    """A package's plaintext image hash, kept in block 0 at IMAGE_HASH_AT."""
    return b2s(image, 32, person=b'LFimage')


def cek_check(cek, nonce):
    return b2s(nonce, 8, key=cek, person=b'LFcheck')


def kek(shared, eph_pub, board_pub, revision, product):
    """The key that wraps the CEK for one board."""
    if shared == bytes(32):
        raise SealError('X25519 gave an all-zero shared secret')
    return b2s(eph_pub + board_pub, 16, key=shared,
               salt=struct.pack('<II', revision, product), person=b'LFwrap')


def crypt(cipher, cek, nonce, n, data):
    """Encrypts or decrypts content block n (CTR: the same operation)."""
    alg = algorithms.AES(cek) if cipher == 1 else algorithms.SM4(cek)
    ctr = nonce + (n * 256).to_bytes(8, 'big')
    return Cipher(alg, modes.CTR(ctr)).encryptor().update(data)


def raw_public(key):
    """The raw public key bytes of a public or private key."""
    if hasattr(key, 'public_key'):
        key = key.public_key()
    return key.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def raw_private(key):
    return key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                             serialization.NoEncryption())


def cert_message(product, serial, release_pub):
    return struct.pack('<I', product) + struct.pack('<I', serial) + release_pub


# Key files ---------------------------------------------------------------

def write_json(path, obj, force):
    if os.path.exists(path) and not force:
        raise SealError(f'{path} exists (use --force to replace it)')
    with open(path, 'w') as f:
        json.dump(obj, f, indent=1)
        f.write('\n')


def read_json(path, kind):
    try:
        with open(path) as f:
            obj = json.load(f)
    except (OSError, ValueError) as e:
        raise SealError(f'{path}: {e}')
    if obj.get('type') != kind:
        raise SealError(f'{path} is not a {kind} key file')
    return obj


def hexbytes(obj, field, size, path):
    try:
        b = bytes.fromhex(obj[field])
    except (KeyError, ValueError, TypeError):
        b = b''
    if len(b) != size:
        raise SealError(f'{path}: bad "{field}"')
    return b


def root_public(path):
    """The root public key from a root .pub or .key file."""
    obj = read_json(path, 'root')
    return Ed25519PublicKey.from_public_bytes(hexbytes(obj, 'public', 32, path))


def read_boards(path):
    boards, seen = [], set()
    try:
        with open(path, newline='') as f:
            rows = list(csv.reader(f))
    except OSError as e:
        raise SealError(str(e))
    for i, row in enumerate(rows, 1):
        if not row or not row[0].strip() or row[0].lstrip().startswith('#'):
            continue
        try:
            uid = bytes.fromhex(row[0].strip())
            pub = bytes.fromhex(row[1].strip())
        except (IndexError, ValueError):
            uid = pub = b''
        if len(uid) != 8 or len(pub) != 32:
            raise SealError(f'{path} line {i}: need UID (16 hex digits), public key (64)')
        if uid in seen:
            raise SealError(f'{path} line {i}: UID {uid.hex()} is listed twice')
        seen.add(uid)
        boards.append((uid, pub))
    if not boards:
        raise SealError(f'{path} lists no boards')
    return boards


# Block 0 -----------------------------------------------------------------

def parse_header(b0, file_blocks):
    """Checks block 0's fields (not its signatures) and returns them."""
    if b0[:9] != SIGNATURE or b0[9:10] not in (SEALED_MARK, PACKAGE_MARK):
        raise SealError('not a sealed block file or package '
                        '(block 0 must start with LITEFORTHS or LITEFORTHF)')
    package = b0[9:10] == PACKAGE_MARK
    end = IMAGE_HASH_AT if package else SIG_AT
    version, cipher = b0[10], b0[11]
    flags, product, revision, c = struct.unpack_from('<IIII', b0, 12)
    t, r = struct.unpack_from('<HH', b0, 28)
    if version != FORMAT_VERSION:
        raise SealError(f'format version {version}, expected {FORMAT_VERSION}')
    if cipher not in CIPHER_NAMES:
        raise SealError(f'unknown cipher {cipher}')
    if flags:
        raise SealError(f'unknown flags {flags:#x}')
    if c < 2 or t != (c + PER_TABLE - 1) // PER_TABLE:
        raise SealError(f'{c} blocks need {(c + PER_TABLE - 1) // PER_TABLE} '
                        f'hash table blocks, not {t}')
    if TABLE_HASHES + 32 * t + RECIPIENT * r > end or r == 0:
        raise SealError(f'{t} table hashes and {r} recipients don\'t fit block 0')
    if c + t > file_blocks:
        raise SealError(f'the file has {file_blocks} blocks, the header needs {c + t}')
    rcp = TABLE_HASHES + 32 * t
    desc = None
    if package:
        component, dflags, hw_rev, length, min_version = struct.unpack_from('<BBHII', b0, 180)
        if component not in COMPONENT_NAMES:
            raise SealError(f'unknown component {component}')
        if dflags:
            raise SealError(f'unknown image flags {dflags:#x}')
        if not (c - 2) * BLOCK < length <= (c - 1) * BLOCK:
            raise SealError(f'an image of {length} bytes doesn\'t fill {c - 1} content blocks')
        if length > COMPONENT_LIMIT[component]:
            raise SealError(f'a {COMPONENT_NAMES[component]} image of {length} bytes is larger '
                            f'than its {COMPONENT_LIMIT[component]}-byte slot')
        desc = {'component': component, 'hw_rev': hw_rev, 'length': length,
                'min_version': min_version, 'image_hash': b0[IMAGE_HASH_AT:SIG_AT]}
    return {
        'package': desc, 'end': end,
        'cipher': cipher, 'product': product, 'revision': revision, 'c': c, 't': t,
        'nonce': b0[32:40], 'eph': b0[40:72], 'check': b0[72:80],
        'serial': struct.unpack_from('<I', b0, 80)[0], 'release': b0[84:116],
        'cert': b0[116:180],
        'tables': [b0[TABLE_HASHES + 32 * i:TABLE_HASHES + 32 * i + 32] for i in range(t)],
        'recipients': [(b0[rcp + RECIPIENT * i:rcp + RECIPIENT * i + 8],
                        b0[rcp + RECIPIENT * i + 8:rcp + RECIPIENT * (i + 1)])
                       for i in range(r)],
    }


def verify(data, root_pub, min_serial=0, min_revision=0, product=None):
    """Everything a board checks before unwrapping: block 0's fields, the
    certificate, the signature, the minimums, and every hash. Returns the
    parsed header."""
    if len(data) % BLOCK or len(data) < 2 * BLOCK:
        raise SealError('the file isn\'t a whole number of 4 KB blocks')
    b0 = data[:BLOCK]
    h = parse_header(b0, len(data) // BLOCK)
    if product is not None and h['product'] != product:
        raise SealError(f'made for product {h["product"]:#x}, not {product:#x}')
    try:
        root_pub.verify(h['cert'], cert_message(h['product'], h['serial'], h['release']))
    except InvalidSignature:
        raise SealError('the release key\'s certificate doesn\'t match the root key')
    if h['serial'] < min_serial:
        raise SealError(f'release key serial {h["serial"]} is below {min_serial}')
    try:
        Ed25519PublicKey.from_public_bytes(h['release']).verify(b0[SIG_AT:], b0[:SIG_AT])
    except InvalidSignature:
        raise SealError('block 0\'s signature doesn\'t match the release key')
    if any(b0[TABLE_HASHES + 32 * h['t'] + RECIPIENT * len(h['recipients']):h['end']]) \
            or (h['package'] is None and any(b0[180:192])):
        raise SealError('block 0 has data in its reserved bytes')
    if h['revision'] < min_revision:
        raise SealError(f'revision {h["revision"]} is below {min_revision}')
    for t in range(h['t']):
        table = data[(h['c'] + t) * BLOCK:(h['c'] + t + 1) * BLOCK]
        if table_hash(t, table) != h['tables'][t]:
            raise SealError(f'hash table block {h["c"] + t} doesn\'t match block 0')
    for n in range(1, h['c']):
        t, i = divmod(n, PER_TABLE)
        table = data[(h['c'] + t) * BLOCK:(h['c'] + t + 1) * BLOCK]
        if block_hash(n, data[n * BLOCK:(n + 1) * BLOCK]) != table[i * ENTRY:(i + 1) * ENTRY]:
            raise SealError(f'block {n} doesn\'t match its hash')
    return h


def unwrap(h, board):
    """This board's CEK from block 0, checked against the CEK check."""
    uid = bytes.fromhex(board['uid'])
    wrapped = [w for u, w in h['recipients'] if u == uid]
    if not wrapped:
        raise SealError(f'board {uid.hex()} is not a recipient')
    priv = X25519PrivateKey.from_private_bytes(bytes.fromhex(board['private']))
    try:
        shared = priv.exchange(X25519PublicKey.from_public_bytes(h['eph']))
    except ValueError:
        raise SealError('X25519 refused the ephemeral key')
    k = kek(shared, h['eph'], raw_public(priv.public_key()), h['revision'], h['product'])
    cek = bytes(a ^ b for a, b in zip(wrapped[0], k))
    if cek_check(cek, h['nonce']) != h['check']:
        raise SealError('the unwrapped key fails the CEK check')
    return cek


# Commands ----------------------------------------------------------------

def cmd_root_key(a):
    k = Ed25519PrivateKey.generate()
    pub = raw_public(k).hex()
    write_json(a.out + '.key', {'type': 'root', 'private': raw_private(k).hex(),
                                'public': pub}, a.force)
    write_json(a.out + '.pub', {'type': 'root', 'public': pub}, a.force)
    print(f'{a.out}.key: keep it offline. {a.out}.pub: public key {pub}')


def cmd_release_key(a):
    root = read_json(a.root, 'root')
    root_priv = Ed25519PrivateKey.from_private_bytes(hexbytes(root, 'private', 32, a.root))
    k = Ed25519PrivateKey.generate()
    pub = raw_public(k)
    cert = root_priv.sign(cert_message(a.product, a.serial, pub))
    write_json(a.out, {'type': 'release', 'product': a.product, 'serial': a.serial,
                       'private': raw_private(k).hex(), 'public': pub.hex(),
                       'cert': cert.hex()}, a.force)
    print(f'{a.out}: release key serial {a.serial} for product {a.product:#x}')


def insecure_key(uid):
    """The private key of a board built with INSECURE (V5F/User/board.c):
    BLAKE2s of its UID, personalization LFinsec1."""
    return hashlib.blake2s(uid, digest_size=32, person=b'LFinsec1').digest()


def cmd_board_key(a):
    uid = bytes.fromhex(a.uid) if a.uid else os.urandom(8)
    if len(uid) != 8:
        raise SealError('--uid takes 16 hex digits')
    if a.insecure:                      # as a CH32H417 built with INSECURE derives it
        if not a.uid:
            raise SealError('--insecure needs the board\'s --uid')
        k = X25519PrivateKey.from_private_bytes(insecure_key(uid))
    else:
        k = X25519PrivateKey.generate()
    pub = raw_public(k).hex()
    write_json(a.out, {'type': 'board', 'uid': uid.hex(), 'private': raw_private(k).hex(),
                       'public': pub}, a.force)
    print(f'{uid.hex()},{pub},{os.path.basename(a.out)}')


def cmd_lf_board(a):
    board = read_json(a.board_key, 'board')
    uid = hexbytes(board, 'uid', 8, a.board_key)
    priv = hexbytes(board, 'private', 32, a.board_key)
    root = read_json(a.root, 'root')
    pub = hexbytes(root, 'public', 32, a.root)
    if os.path.exists(a.out) and not a.force:
        raise SealError(f'{a.out} exists (use --force to replace it)')
    with open(a.out, 'w') as f:
        f.write('# lf -K board file: what a board keeps for sealed block files\n'
                f'uid {uid.hex()}\nprivate {priv.hex()}\nroot {pub.hex()}\n'
                f'product {a.product:#x}\nmin-revision {a.min_revision}\n'
                f'min-serial {a.min_serial}\n'
                + ('sealed-only 1\n' if a.sealed_only else ''))
    print(f'{a.out}: board {uid.hex()} for lf -K')


def cmd_root_header(a):
    pub = hexbytes(read_json(a.root, 'root'), 'public', 32, a.root)
    rows = ',\n'.join('    ' + ', '.join('0x%02x' % b for b in pub[i:i + 8])
                      for i in range(0, 32, 8))
    text = ('/* sealed_root.h, written by scripts/sealblocks.py root-header from %s:\n'
            '   the root public key and product ID this build reads sealed block\n'
            '   files with (doc/encrypted-blocks.md, V5F/User/board.h). */\n'
            '#ifndef SEALED_ROOT_H\n#define SEALED_ROOT_H\n\n'
            '#define SEALED_ROOT_KEY { \\\n%s }\n\n'
            '#define SEALED_PRODUCT 0x%08xu\n\n%s#endif /* SEALED_ROOT_H */\n'
            % (os.path.basename(a.root), rows.replace('\n', ' \\\n'), a.product,
               '/* Only sealed block files: plain ones are refused */\n'
               '#define SEALED_ONLY 1\n\n' if a.sealed_only else ''))
    if a.out:
        if os.path.exists(a.out) and not a.force:
            raise SealError(f'{a.out} exists (use --force to replace it)')
        with open(a.out, 'w', newline='\n') as f:
            f.write(text)
        print(f'{a.out}: root key {pub.hex()[:16]}..., product {a.product:#x}'
              + (', sealed files only' if a.sealed_only else ''))
    else:
        sys.stdout.write(text)


def sealed_bytes(blocks, mark, a, desc=b'', ihash=b''):
    """A sealed file: block 0, then blocks (content blocks 1..C-1, 4 KB
    each, plain) encrypted, then the hash tables. mark is SEALED_MARK or
    PACKAGE_MARK; a package also gets desc (12 bytes at 180) and ihash
    (its image hash, at IMAGE_HASH_AT). a: the command's arguments
    (release, boards, revision, cipher)."""
    rel = read_json(a.release, 'release')
    rel_priv = Ed25519PrivateKey.from_private_bytes(hexbytes(rel, 'private', 32, a.release))
    rel_pub = raw_public(rel_priv)
    cert = hexbytes(rel, 'cert', 64, a.release)
    product, serial = int(rel['product']), int(rel['serial'])
    boards = read_boards(a.boards)
    c = len(blocks) + 1
    t = (c + PER_TABLE - 1) // PER_TABLE
    end = IMAGE_HASH_AT if mark == PACKAGE_MARK else SIG_AT
    if TABLE_HASHES + 32 * t + RECIPIENT * len(boards) > end:
        raise SealError(f'{len(boards)} boards and {t} table blocks don\'t fit block 0')
    cipher = CIPHERS[a.cipher]
    cek, nonce = os.urandom(16), os.urandom(8)
    eph = X25519PrivateKey.generate()
    eph_pub = raw_public(eph)

    content = []
    tables = [bytearray(BLOCK) for _ in range(t)]
    for n in range(1, c):
        enc = crypt(cipher, cek, nonce, n, blocks[n - 1])
        content.append(enc)
        i, j = divmod(n, PER_TABLE)
        tables[i][j * ENTRY:(j + 1) * ENTRY] = block_hash(n, enc)

    b0 = bytearray(BLOCK)
    b0[0:10] = SIGNATURE + mark
    b0[10], b0[11] = FORMAT_VERSION, cipher
    struct.pack_into('<IIIIHH', b0, 12, 0, product, a.revision, c, t, len(boards))
    b0[32:40] = nonce
    b0[40:72] = eph_pub
    b0[72:80] = cek_check(cek, nonce)
    struct.pack_into('<I', b0, 80, serial)
    b0[84:116] = rel_pub
    b0[116:180] = cert
    b0[180:180 + len(desc)] = desc
    if ihash:
        b0[IMAGE_HASH_AT:SIG_AT] = ihash
    for i in range(t):
        b0[TABLE_HASHES + 32 * i:TABLE_HASHES + 32 * i + 32] = table_hash(i, bytes(tables[i]))
    at = TABLE_HASHES + 32 * t
    for uid, pub in boards:
        shared = eph.exchange(X25519PublicKey.from_public_bytes(pub))
        k = kek(shared, eph_pub, pub, a.revision, product)
        b0[at:at + 8] = uid
        b0[at + 8:at + RECIPIENT] = bytes(x ^ y for x, y in zip(cek, k))
        at += RECIPIENT
    b0[SIG_AT:] = rel_priv.sign(bytes(b0[:SIG_AT]))
    return bytes(b0) + b''.join(content) + b''.join(bytes(x) for x in tables), t, len(boards)


def cmd_seal(a):
    with open(a.plain, 'rb') as f:
        plain = f.read()
    if plain[:9] != SIGNATURE or plain[9:10] in (SEALED_MARK, PACKAGE_MARK):
        raise SealError(f'{a.plain} is not a plain block file')
    if len(plain) % BLOCK:
        raise SealError(f'{a.plain} isn\'t a whole number of 4 KB blocks')
    c = len(plain) // BLOCK
    if c < 2:
        raise SealError(f'{a.plain} has no content blocks')
    data, t, nb = sealed_bytes([plain[n * BLOCK:(n + 1) * BLOCK] for n in range(1, c)],
                               SEALED_MARK, a)
    with open(a.sealed, 'wb') as f:
        f.write(data)
    print(f'{a.sealed}: {c - 1} content blocks, {t} hash table block(s), '
          f'{nb} board(s), revision {a.revision}, {a.cipher.upper()}')


def cmd_package(a):
    with open(a.image, 'rb') as f:
        image = f.read()
    component = COMPONENTS[a.component]
    if not image:
        raise SealError(f'{a.image} is empty')
    if image[:9] == SIGNATURE:
        raise SealError(f'{a.image} is a block file, not a firmware image')
    if len(image) > COMPONENT_LIMIT[component]:
        raise SealError(f'{a.image}: {len(image)} bytes, larger than the {a.component} slot '
                        f'({COMPONENT_LIMIT[component]} bytes)')
    padded = image + bytes([PAD]) * (-len(image) % BLOCK)
    blocks = [padded[i:i + BLOCK] for i in range(0, len(padded), BLOCK)]
    desc = struct.pack('<BBHII', component, 0, a.hw_rev, len(image), a.min_version)
    data, t, nb = sealed_bytes(blocks, PACKAGE_MARK, a, desc, image_hash(image))
    with open(a.package, 'wb') as f:
        f.write(data)
    print(f'{a.package}: {a.component} image, {len(image)} bytes in {len(blocks)} block(s), '
          f'version {a.revision}, {nb} board(s), {a.cipher.upper()}'
          + (f', new minimum {a.min_version}' if a.min_version else ''))


def describe(h):
    """A package's description, for check and open."""
    d = h['package']
    return (f'{COMPONENT_NAMES[d["component"]]} image, {d["length"]} bytes, version '
            f'{h["revision"]}, hardware revision {d["hw_rev"] or "any"}'
            + (f', new minimum {d["min_version"]}' if d['min_version'] else ''))


def cmd_check(a):
    with open(a.sealed, 'rb') as f:
        data = f.read()
    h = verify(data, root_public(a.root))
    print(f'{a.sealed}: ok. Product {h["product"]:#x}, revision {h["revision"]}, '
          f'release key {h["serial"]}, {CIPHER_NAMES[h["cipher"]].upper()}, '
          f'{h["c"] - 1} content blocks, boards: '
          + ' '.join(u.hex() for u, _ in h['recipients']))
    if h['package']:
        print(f'{a.sealed}: a firmware package: {describe(h)}')


def cmd_open(a):
    with open(a.sealed, 'rb') as f:
        data = f.read()
    h = verify(data, root_public(a.root), a.min_serial, a.min_revision, a.product)
    cek = unwrap(h, read_json(a.board_key, 'board'))
    if h['package']:
        d = h['package']
        image = b''.join(crypt(h['cipher'], cek, h['nonce'], n, data[n * BLOCK:(n + 1) * BLOCK])
                         for n in range(1, h['c']))
        if any(b != PAD for b in image[d['length']:]):
            raise SealError('the image\'s padding isn\'t erased flash (0xFF)')
        image = image[:d['length']]
        if image_hash(image) != d['image_hash']:
            raise SealError('the decrypted image doesn\'t match its hash in block 0')
        with open(a.plain, 'wb') as f:
            f.write(image)
        print(f'{a.plain}: {describe(h)}')
        return
    b0 = (SIGNATURE + b'BLK opened from a sealed file, revision %d' % h['revision'])
    out = [b0.ljust(BLOCK, b' ')]
    for n in range(1, h['c']):
        out.append(crypt(h['cipher'], cek, h['nonce'], n, data[n * BLOCK:(n + 1) * BLOCK]))
    with open(a.plain, 'wb') as f:
        f.write(b''.join(out))
    print(f'{a.plain}: {h["c"] - 1} content blocks, revision {h["revision"]}')


def main(argv=None):
    p = argparse.ArgumentParser(description='Sealed (encrypted, signed) LiteForth block files',
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=__doc__.split('\n\n', 1)[1])
    num = lambda s: int(s, 0)
    sub = p.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('root-key', help='make a root key pair')
    s.add_argument('out', help='file name without extension')
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('release-key', help='make a release key certified by the root')
    s.add_argument('root', help='the root .key file')
    s.add_argument('serial', type=num)
    s.add_argument('out')
    s.add_argument('--product', type=num, required=True)
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('board-key', help='make a board key pair, for testing')
    s.add_argument('out')
    s.add_argument('--uid', help='16 hex digits (default: random)')
    s.add_argument('--insecure', action='store_true',
                   help="the key a CH32H417 built with INSECURE derives from --uid")
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('lf-board', help='write a board file for the desktop lf (-K)')
    s.add_argument('out')
    s.add_argument('--board-key', required=True)
    s.add_argument('--root', required=True, help='the root .pub file')
    s.add_argument('--product', type=num, required=True)
    s.add_argument('--min-revision', type=num, default=0)
    s.add_argument('--min-serial', type=num, default=0)
    s.add_argument('--sealed-only', action='store_true',
                   help='refuse plain block files, as a SEALED_ONLY build does')
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('root-header', help='write sealed_root.h for a CH32H417 build')
    s.add_argument('root', help='the root .pub file')
    s.add_argument('--product', type=num, required=True)
    s.add_argument('--sealed-only', action='store_true',
                   help='define SEALED_ONLY: the board refuses plain block files')
    s.add_argument('-o', dest='out', help='the file (default: standard output)')
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('seal', help='make a sealed file from a plain one')
    s.add_argument('plain')
    s.add_argument('sealed')
    s.add_argument('--release', required=True, help='release key file')
    s.add_argument('--boards', required=True, help='boards.csv')
    s.add_argument('--revision', type=num, required=True)
    s.add_argument('--cipher', choices=sorted(CIPHERS), default='aes')
    s = sub.add_parser('package', help='make a firmware update package from a .bin')
    s.add_argument('image')
    s.add_argument('package')
    s.add_argument('--component', choices=sorted(COMPONENTS), required=True)
    s.add_argument('--release', required=True, help='release key file')
    s.add_argument('--boards', required=True, help='boards.csv')
    s.add_argument('--revision', type=num, required=True, help="the image's version")
    s.add_argument('--hw-rev', type=num, default=0, help='hardware revision (default 0: any)')
    s.add_argument('--min-version', type=num, default=0,
                   help='raise the board\'s minimum version to this on install (default 0: leave it)')
    s.add_argument('--cipher', choices=sorted(CIPHERS), default='aes')
    s = sub.add_parser('check', help='check a sealed file (no board key needed)')
    s.add_argument('sealed')
    s.add_argument('--root', required=True, help='the root .pub file')
    s = sub.add_parser('open', help='check and decrypt a sealed file, as a board would')
    s.add_argument('sealed')
    s.add_argument('plain')
    s.add_argument('--root', required=True)
    s.add_argument('--board-key', required=True)
    s.add_argument('--min-revision', type=num, default=0)
    s.add_argument('--min-serial', type=num, default=0)
    s.add_argument('--product', type=num)
    a = p.parse_args(argv)
    if getattr(a, 'serial', 0) is not None and not 0 <= getattr(a, 'serial', 0) < 2**32:
        p.error('serial must fit in 32 bits')
    if getattr(a, 'revision', 0) is not None and not 0 <= getattr(a, 'revision', 0) < 2**32:
        p.error('revision must fit in 32 bits')
    if getattr(a, 'product', None) is not None and not 0 <= a.product < 2**32:
        p.error('product must fit in 32 bits')
    if not 0 <= getattr(a, 'hw_rev', 0) < 2**16:
        p.error('hw-rev must fit in 16 bits')
    if not 0 <= getattr(a, 'min_version', 0) < 2**32:
        p.error('min-version must fit in 32 bits')
    try:
        {'root-key': cmd_root_key, 'release-key': cmd_release_key, 'board-key': cmd_board_key,
         'lf-board': cmd_lf_board, 'root-header': cmd_root_header,
         'seal': cmd_seal, 'package': cmd_package, 'check': cmd_check,
         'open': cmd_open}[a.cmd](a)
    except SealError as e:
        print(f'sealblocks: {e}', file=sys.stderr)
        return 1
    except OSError as e:
        print(f'sealblocks: {e}', file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
