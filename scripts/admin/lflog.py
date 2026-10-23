#!/usr/bin/env python3
"""Encrypted data logs from a board's microSD card (doc/admin/data-logs.md).

    lflog.py server-key OUT [--key-id N]          make the server's key pair OUT.key,
                                                   OUT.pub (X25519); keep OUT.key on
                                                   the server, and backed up: losing it
                                                   loses every log made for it
    lflog.py header OUT.pub [-o FILE]             write log_server.h, the public key
                                                   and key ID a board's build logs to
    lflog.py open LOG --server-key K.key --boards CSV [-o OUT] [--app-header FILE]
                                                   check a log and decrypt it: names the
                                                   board that wrote it (its UID, which
                                                   only the server can read), checks every
                                                   record's tag, and writes the sample
                                                   blocks, 128 bytes each, to OUT

open reports whether the log was closed. One that wasn't (the power went,
and the board closed it at its next start) ends at its last whole record;
nothing proves that's where it really ended, but every record before it is
checked.

Keys are JSON, like sealblocks.py's. boards.csv is the same file
sealblocks.py packages for: `UID,public key,name`, from each board's
`enroll`. Needs the `cryptography` package. Exit status: 0 the log checks
out, 1 something in it doesn't (or a key is wrong), 2 a file couldn't be
read or written.
"""
import argparse
import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sealblocks as sb  # noqa: E402  (key files, boards.csv)

try:
    from cryptography.exceptions import InvalidSignature
    from cryptography.hazmat.primitives import poly1305
    from cryptography.hazmat.primitives.asymmetric.x25519 import (
        X25519PrivateKey, X25519PublicKey)
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
except ImportError:
    sys.exit('lflog.py needs the cryptography package: pip install cryptography')

RECORD = 32768
SAMPLE = 128
SAMPLES = 255
PAYLOAD = 64
APP_HEADER = 1024
H_APP = 128
H_TAG = H_APP + APP_HEADER
R_TAG = PAYLOAD + SAMPLES * SAMPLE
MAGIC = b'LFLOG001'


def b2s(data, key=b'', person=b'', size=32):
    return hashlib.blake2s(data, digest_size=size, key=key, person=person).digest()


def x25519(priv, pub):
    return X25519PrivateKey.from_private_bytes(priv).exchange(
        X25519PublicKey.from_public_bytes(pub))


def ctr(key, nonce, unit, data):
    """AES-128-CTR from counter nonce || unit (64-bit big-endian)"""
    c = Cipher(algorithms.AES(key), modes.CTR(nonce + struct.pack('>Q', unit))).encryptor()
    return c.update(data) + c.finalize()


class LogError(Exception):
    pass


def keys_for(block0, s, S, boards):
    """The identity and the log's keys from block 0, or LogError"""
    if len(block0) < RECORD or block0[:8] != MAGIC:
        raise LogError('not a data log (no whole block 0 starting LFLOG001)')
    if (struct.unpack('<I', block0[-16:-12])[0] != 0 or block0[-12:-4] != block0[16:24]
            or block0[-4:] != b'LFLE'):
        raise LogError('block 0 is torn: the board never finished writing the header')
    if block0[8] != 1 or block0[9] != 1:
        raise LogError(f'unknown format {block0[8]} or cipher {block0[9]}')
    E = block0[24:56]
    try:
        es = x25519(s, E)
    except ValueError:
        raise LogError('bad one-off key E')
    k_id = b2s(E + S, key=es, person=b'LFlogid')
    pad = b2s(b'', key=k_id, person=b'LFlogidx')
    ident = bytes(a ^ b for a, b in zip(block0[56:88], pad))
    uid = ident[:8]
    pub = boards.get(uid)
    if pub is None:
        raise LogError(f'written by board {uid.hex()}, which boards.csv doesn\'t list '
                       '(or not for this server key)')
    k_auth = b2s(E + S, key=x25519(s, pub), person=b'LFlogbrd')
    master = b2s(es + E + S, key=k_auth, person=b'LFlogkey')
    k_enc = b2s(b'', key=master, person=b'LFlogenc', size=16)
    nonce = b2s(b'', key=master, person=b'LFlognon', size=8)
    k_hdr = b2s(b'', key=master, person=b'LFloghdr')
    if b2s(block0[:H_TAG], key=k_hdr, person=b'LFloghtg', size=16) != block0[H_TAG:H_TAG + 16]:
        raise LogError(f'the header\'s tag doesn\'t match: not written by board {uid.hex()}, '
                       'or changed')
    start, firmware, level = struct.unpack('<QIB', ident[8:21])
    dev = X25519PrivateKey.from_private_bytes(sb.insecure_key(uid)).public_key() \
        .public_bytes_raw() == pub
    app = ctr(k_enc, nonce, 0, block0[H_APP:H_APP + APP_HEADER])
    return {'uid': uid, 'start': start, 'firmware': firmware, 'level': level,
            'key_id': struct.unpack('<I', block0[12:16])[0], 'file_id': block0[16:24],
            'k_enc': k_enc, 'nonce': nonce, 'app': app, 'dev': dev}


def open_record(rec, n, k):
    """(samples, flags, plaintext sample blocks) of record n, or LogError"""
    if (rec[:4] != b'LFLB' or rec[4:12] != k['file_id'] or struct.unpack('<I', rec[12:16])[0] != n
            or struct.unpack('<I', rec[-16:-12])[0] != n or rec[-12:-4] != k['file_id']
            or rec[-4:] != b'LFLE'):
        raise LogError(f'block {n} isn\'t record {n} of this log (its frame)')
    pk = (ctr(k['k_enc'], k['nonce'], n * 2048 + 2046, bytes(32)))
    try:
        poly1305.Poly1305.verify_tag(pk, rec[:R_TAG], rec[R_TAG:R_TAG + 16])
    except InvalidSignature:
        raise LogError(f'record {n} fails its tag: changed, or not from this board')
    samples, flags = rec[16], rec[18]
    if samples > SAMPLES:
        raise LogError(f'record {n} says it holds {samples} sample blocks')
    plain = ctr(k['k_enc'], k['nonce'], n * 2048, rec[PAYLOAD:R_TAG])
    return samples, flags, plain[:samples * SAMPLE]


def cmd_open(a):
    key = sb.read_json(a.server_key, 'server')
    s = sb.hexbytes(key, 'private', 32, a.server_key)
    S = sb.hexbytes(key, 'public', 32, a.server_key)
    boards = {uid: pub for uid, pub in sb.read_boards(a.boards)}
    try:
        f = open(a.log, 'rb')
    except OSError as e:
        raise sb.SealError(str(e))
    with f:
        k = keys_for(f.read(RECORD), s, S, boards)
        if k['key_id'] != key.get('key_id', k['key_id']):
            print(f'note: the log names server key {k["key_id"]}, this one is '
                  f'{key.get("key_id")}')
        print(f'board {k["uid"].hex()}, started {k["start"]} (RTC seconds), firmware '
              f'{k["firmware"]}, security level {k["level"]} (as the log says)')
        if k['dev']:
            print('WARNING: this board\'s key in boards.csv is a development key, made from '
                  'its UID: anyone who knows the UID could have written this log')
        if a.app_header:
            with open(a.app_header, 'wb') as h:
                h.write(k['app'])
        out = open(a.out, 'wb') if a.out else None
        n, last_good, total, closed = 1, 0, 0, False
        problems = []                   # (block, is a frame problem, text)
        while True:
            rec = f.read(RECORD)
            if not rec:
                break
            if len(rec) < RECORD:
                problems.append((n, False, f'{len(rec)} bytes past the last whole block'))
                break
            try:
                samples, flags, plain = open_record(rec, n, k)
            except LogError as e:
                problems.append((n, 'frame' in str(e), str(e)))
                n += 1
                continue
            if closed:
                problems.append((n, False, f'record {n} comes after the last one'))
            if out:
                out.write(plain)
            total += samples
            closed = closed or bool(flags & 1)
            last_good = n
            n += 1
        if out:
            out.close()
    # Blocks past the last good record that aren't this log's at all: a file
    # copied off the card before the board's recovery trimmed it, not damage
    tail = [p for p in problems if p[0] > last_good]
    untrimmed = not closed and tail and all(p[1] for p in tail)
    shown = [p for p in problems if not (untrimmed and p[0] > last_good)]
    for _, _, text in shown:
        print(f'problem: {text}')
    if untrimmed:
        print(f'note: the file goes on {len(tail)} blocks past record {last_good}, holding no '
              "record of this log: it wasn't trimmed (copied before the board's recovery?)")
    print(f'{last_good} records, {total} sample blocks; '
          + ('closed' if closed else 'not closed: it ends at its last whole record, unproven'))
    return 1 if shown else 0


def cmd_server_key(a):
    k = X25519PrivateKey.generate()
    priv = k.private_bytes_raw().hex()
    pub = k.public_key().public_bytes_raw().hex()
    sb.write_json(a.out + '.key', {'type': 'server', 'key_id': a.key_id, 'private': priv,
                                   'public': pub}, a.force)
    sb.write_json(a.out + '.pub', {'type': 'server', 'key_id': a.key_id, 'public': pub},
                  a.force)
    print(f'{a.out}.key: keep it on the server, backed up. {a.out}.pub: key {a.key_id}, {pub}')
    return 0


def cmd_header(a):
    key = sb.read_json(a.pub, 'server')
    pub = sb.hexbytes(key, 'public', 32, a.pub)
    rows = ',\n'.join('    ' + ', '.join('0x%02x' % b for b in pub[i:i + 8])
                      for i in range(0, 32, 8))
    text = ('/* log_server.h, written by scripts/admin/lflog.py header from %s:\n'
            '   the server key data logs are encrypted to (doc/admin/data-logs.md) */\n'
            '#define LOG_SERVER_KEY_ID %du\n'
            '#define LOG_SERVER_KEY { \\\n%s }\n' % (os.path.basename(a.pub), key['key_id'],
                                                  rows.replace('\n', ' \\\n')))
    if a.o:
        with open(a.o, 'w') as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description='Data logs from the microSD card')
    sub = p.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('server-key')
    s.add_argument('out')
    s.add_argument('--key-id', type=int, default=1)
    s.add_argument('--force', action='store_true')
    s = sub.add_parser('header')
    s.add_argument('pub')
    s.add_argument('-o')
    s = sub.add_parser('open')
    s.add_argument('log')
    s.add_argument('--server-key', required=True)
    s.add_argument('--boards', required=True)
    s.add_argument('-o', dest='out')
    s.add_argument('--app-header')
    a = p.parse_args(argv)
    try:
        return {'server-key': cmd_server_key, 'header': cmd_header, 'open': cmd_open}[a.cmd](a)
    except LogError as e:
        print(f'lflog.py: {e}')
        return 1
    except sb.SealError as e:
        print(f'lflog.py: {e}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
