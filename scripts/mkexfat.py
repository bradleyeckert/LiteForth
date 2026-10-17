#!/usr/bin/env python3
"""Builds small exFAT card images for testing LiteForth's exFAT reader.

    mkexfat.py OUT [--size-mb N] [--cluster BYTES] [--no-mbr]
               [--label TEXT] [--dirty] [--root-frag] [--file SPEC ...]

OUT is a disk image: by default an MBR with one exFAT partition (type 07)
from sector 2048, as SD cards have; --no-mbr puts the volume at sector 0.
--root-frag spreads the root directory over clusters that aren't
neighbors. The image is sparse where it can be.

Each --file SPEC is NAME[=SOURCE][,OPTION...]: SOURCE is a file to copy;
without one, the data is made up: block n (4 KB) is filled with the
32-bit value n, and block 0 starts with LITEFORTH (`nosig` leaves it out).
Options:

    size=N      bytes of made-up data (default 16 blocks)
    frag        put a free cluster between each of its clusters (FAT chain)
    fat         contiguous, but described by the FAT, without NoFatChain
    valid=N     ValidDataLength (default: the whole length)
    pad=N       N deleted entries before its entry set, to move it in the
                directory (a set can then cross sectors and clusters)
    deleted     also put a deleted copy of the entry set before it
    badsum      spoil its SetChecksum
    dir         mark it as a directory

The layout follows Microsoft's exFAT specification: main and backup boot
regions with the boot checksum, a FAT, the allocation bitmap, an up-case
table, and the root directory (bitmap, up-case and optional label entries,
then the files). It is meant for unit tests, not for formatting cards.
"""
import argparse
import struct
import sys

SECTOR = 512
SIGNATURE = b"LITEFORTH"


def sum16(data, s=0, skip=()):
    for i, b in enumerate(data):
        if i in skip:
            continue
        s = (((s & 1) << 15) | (s >> 1)) + b
        s &= 0xFFFF
    return s


def sum32(data, s=0, skip=()):
    for i, b in enumerate(data):
        if i in skip:
            continue
        s = (((s & 1) << 31) | (s >> 1)) + b
        s &= 0xFFFFFFFF
    return s


def upcase_table():
    """A compressed up-case table: identity except a-z -> A-Z."""
    words = [0xFFFF, ord('a')]                      # 0..0x60 map to themselves
    words += [ord('A') + i for i in range(26)]      # a..z
    words += [0xFFFF, 0x10000 - (ord('z') + 1)]     # the rest map to themselves
    return struct.pack("<%dH" % len(words), *words)


def ascii_upper(ch):
    """What the up-case table above does: a-z to A-Z, nothing else"""
    return ch.upper() if 'a' <= ch <= 'z' else ch


def name_hash(name):
    s = 0
    for ch in name:
        c = ord(ascii_upper(ch))
        s = sum16(bytes([c & 0xFF, c >> 8]), s)
    return s


def timestamp(year=2026, month=10, day=16, hour=12, minute=0, second=0):
    return ((year - 1980) << 25) | (month << 21) | (day << 16) | (hour << 11) | (minute << 5) | (second // 2)


def entry_set(name, first_cluster, length, valid, nofatchain, directory=False, badsum=False):
    """The 85, C0, C1... entries for a file, as bytes."""
    units = name.encode("utf-16-le")
    n = len(units) // 2                             # UTF-16 code units
    assert 0 < n <= 255, name
    names = (n + 14) // 15
    ts = timestamp()
    attrs = 0x10 if directory else 0x20
    f = bytearray(32)
    f[0] = 0x85
    f[1] = 1 + names
    struct.pack_into("<HHHIII", f, 2, 0, attrs, 0, ts, ts, ts)
    f[22] = f[23] = f[24] = 0x80                    # UTC
    s = bytearray(32)
    s[0] = 0xC0
    s[1] = 0x01 | (0x02 if nofatchain else 0)       # AllocationPossible, NoFatChain
    s[3] = n
    struct.pack_into("<H", s, 4, name_hash(name))
    struct.pack_into("<Q", s, 8, valid)
    struct.pack_into("<I", s, 20, first_cluster)
    struct.pack_into("<Q", s, 24, length)
    entries = [f, s]
    for i in range(names):
        e = bytearray(32)
        e[0] = 0xC1
        part = units[i * 30:(i + 1) * 30]
        e[2:2 + len(part)] = part
        entries.append(e)
    blob = b"".join(entries)
    chk = sum16(blob, skip=(2, 3))
    if badsum:
        chk ^= 0x5A5A
    struct.pack_into("<H", entries[0], 2, chk)
    return b"".join(entries)


def deleted(blob):
    """The same entries, marked not in use."""
    out = bytearray(blob)
    for i in range(0, len(out), 32):
        out[i] &= 0x7F
    return bytes(out)


def made_up(size, sig=True):
    data = bytearray(size)
    for blk in range((size + 4095) // 4096):
        word = struct.pack("<I", blk)
        end = min(size, (blk + 1) * 4096)
        for i in range(blk * 4096, end - 3, 4):
            data[i:i + 4] = word
    if sig:
        data[0:len(SIGNATURE)] = SIGNATURE
    return bytes(data[:size])


class Image:
    def __init__(self, args):
        self.cluster = args.cluster
        assert self.cluster & (self.cluster - 1) == 0 and self.cluster >= SECTOR
        self.spc_shift = (self.cluster // SECTOR).bit_length() - 1
        self.spc = 1 << self.spc_shift
        self.start = 0 if args.no_mbr else 2048
        self.sectors = args.size_mb * 2048
        self.fat_offset = 128
        # clusters, then the FAT that describes them, then the heap
        guess = self.sectors // self.spc
        self.fat_length = ((guess + 2) * 4 + SECTOR - 1) // SECTOR
        heap = self.fat_offset + self.fat_length
        heap = (heap + self.spc - 1) // self.spc * self.spc
        self.heap = heap
        self.clusters = (self.sectors - heap) // self.spc
        self.fat = [0] * (self.clusters + 2)
        self.fat[0] = 0xFFFFFFF8
        self.fat[1] = 0xFFFFFFFF
        self.used = set()
        self.next_free = 2
        self.writes = []                            # (volume byte offset, data)

    def alloc(self, n, frag=False):
        """n clusters: contiguous, or with a free cluster between each"""
        got = []
        c = self.next_free
        for _ in range(n):
            if c - 2 >= self.clusters:
                sys.exit("mkexfat: image too small")
            got.append(c)
            self.used.add(c)
            c += 2 if frag else 1
        self.next_free = c
        return got

    def chain(self, clusters):
        for a, b in zip(clusters, clusters[1:]):
            self.fat[a] = b
        if clusters:
            self.fat[clusters[-1]] = 0xFFFFFFFF

    def put(self, clusters, data):
        for i, c in enumerate(clusters):
            piece = data[i * self.cluster:(i + 1) * self.cluster]
            if piece:
                self.writes.append((self.cluster_offset(c), piece))

    def cluster_offset(self, c):
        return (self.heap + (c - 2) * self.spc) * SECTOR

    def boot_region(self, root, dirty):
        bs = bytearray(SECTOR)
        bs[0:3] = b"\xEB\x76\x90"
        bs[3:11] = b"EXFAT   "
        struct.pack_into("<QQIIIIIIH", bs, 64, self.start, self.sectors, self.fat_offset,
                         self.fat_length, self.heap, self.clusters, root, 0x1234ABCD, 0x0100)
        struct.pack_into("<H", bs, 106, 2 if dirty else 0)
        bs[108] = 9
        bs[109] = self.spc_shift
        bs[110] = 1
        bs[111] = 0x80
        bs[112] = 0xFF
        bs[510:512] = b"\x55\xAA"
        sectors = [bytes(bs)]
        for _ in range(8):                          # extended boot sectors
            ext = bytearray(SECTOR)
            ext[508:512] = b"\x00\x00\x55\xAA"
            sectors.append(bytes(ext))
        sectors.append(bytes(SECTOR))               # OEM parameters
        sectors.append(bytes(SECTOR))               # reserved
        chk = 0
        for i, sec in enumerate(sectors):
            chk = sum32(sec, chk, skip=(106, 107, 112) if i == 0 else ())
        sectors.append(struct.pack("<I", chk) * (SECTOR // 4))
        return b"".join(sectors)


def build(args):
    img = Image(args)
    cl = img.cluster

    # The allocation bitmap and the up-case table come first
    bitmap_bytes = (img.clusters + 7) // 8
    bitmap_cl = img.alloc((bitmap_bytes + cl - 1) // cl)
    img.chain(bitmap_cl)
    upcase = upcase_table()
    upcase_cl = img.alloc((len(upcase) + cl - 1) // cl)
    img.chain(upcase_cl)
    img.put(upcase_cl, upcase)

    # Files
    sets = []
    for spec in args.file:
        parts = spec.split(",")
        name, _, source = parts[0].partition("=")
        opts = {}
        for o in parts[1:]:
            k, _, v = o.partition("=")
            opts[k] = v
        if source:
            data = open(source, "rb").read()
        else:
            data = made_up(int(opts.get("size", 16 * 4096)), "nosig" not in opts)
        n = (len(data) + cl - 1) // cl
        frag = "frag" in opts
        clusters = img.alloc(n, frag) if n else []
        nofatchain = not frag and "fat" not in opts
        if not nofatchain:
            img.chain(clusters)
        img.put(clusters, data)
        valid = int(opts.get("valid", len(data)))
        blob = entry_set(name, clusters[0] if clusters else 0, len(data), valid,
                         nofatchain and n > 0, "dir" in opts, "badsum" in opts)
        pre = b""
        if "deleted" in opts:
            pre += deleted(blob)
        pre += deleted(bytes([0x85] + [0] * 31)) * int(opts.get("pad", 0))
        sets.append(pre + blob)

    # The root directory
    root = bytearray()
    if args.label:
        assert len(args.label) <= 11, "a volume label has at most 11 characters"
        lab = bytearray(32)
        lab[0] = 0x83
        lab[1] = len(args.label)
        u = args.label.encode("utf-16-le")
        lab[2:2 + len(u)] = u
        root += lab
    bm = bytearray(32)
    bm[0] = 0x81
    struct.pack_into("<IQ", bm, 20, bitmap_cl[0], bitmap_bytes)
    root += bm
    uc = bytearray(32)
    uc[0] = 0x82
    struct.pack_into("<I", uc, 4, sum32(upcase))
    struct.pack_into("<IQ", uc, 20, upcase_cl[0], len(upcase))
    root += uc
    for s in sets:
        root += s
    root_n = max(1, (len(root) + 32 + cl - 1) // cl)  # room for the end entry
    root_cl = img.alloc(root_n, args.root_frag)
    img.chain(root_cl)
    img.put(root_cl, bytes(root) + bytes(root_n * cl - len(root)))

    # The bitmap of what's in use
    bits = bytearray(bitmap_bytes)
    for c in img.used:
        bits[(c - 2) // 8] |= 1 << ((c - 2) % 8)
    img.put(bitmap_cl, bytes(bits))

    total = img.start + img.sectors

    with open(args.out, "wb") as out:
        out.truncate(total * SECTOR)
        if not args.no_mbr:
            mbr = bytearray(SECTOR)
            struct.pack_into("<B3sB3sII", mbr, 446, 0, b"\0\0\0", 0x07, b"\0\0\0",
                             img.start, img.sectors)
            mbr[510:512] = b"\x55\xAA"
            out.seek(0)
            out.write(mbr)
        vol = img.start * SECTOR
        boot = img.boot_region(root_cl[0], args.dirty)
        out.seek(vol)
        out.write(boot)
        out.write(boot)                             # backup boot region
        fat = struct.pack("<%dI" % len(img.fat), *img.fat)
        out.seek(vol + img.fat_offset * SECTOR)
        out.write(fat)
        for off, data in img.writes:
            out.seek(vol + off)
            out.write(data)
    return img


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("out")
    p.add_argument("--size-mb", type=int, default=8)
    p.add_argument("--cluster", type=int, default=4096)
    p.add_argument("--no-mbr", action="store_true")
    p.add_argument("--label", default="")
    p.add_argument("--dirty", action="store_true")
    p.add_argument("--root-frag", action="store_true")
    p.add_argument("--file", action="append", default=[])
    args = p.parse_args()
    img = build(args)
    print(f"{args.out}: exFAT at sector {img.start}, {img.clusters} clusters of {img.cluster} bytes, "
          f"heap at volume sector {img.heap}")


if __name__ == "__main__":
    main()
