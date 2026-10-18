#!/usr/bin/env python3
"""YMODEM sender for testing LiteForth's `ymodem` word.

Usage: sender.py path/to/lf work_dir

Runs lf in work_dir (stdio mode, -o 3), types commands, and plays the
sender's side of several transfers, some with deliberate faults. Prints
lf's text output (without the protocol's C/NAK/ACK bytes) and notes on what
the sender saw, for run.sh to compare with expected.txt.
"""
import os
import select
import subprocess
import sys
import time

SOH, STX, EOT, ACK, NAK, CAN, SUB, C = 1, 2, 4, 6, 0x15, 0x18, 0x1A, 0x43


def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
            crc &= 0xFFFF
    return crc


def packet(n, chunk):
    head = SOH if len(chunk) == 128 else STX
    return bytes([head, n & 0xFF, 255 - (n & 0xFF)]) + chunk + crc16(chunk).to_bytes(2, "big")


def header(name, data, length=True):
    info = name.encode() + b"\0" + (str(len(data)).encode() + b" 0" if length else b"")
    return packet(0, info + bytes(128 - len(info)))


def packets(data, size):
    data = data + bytes([SUB]) * (-len(data) % size)
    for i in range(0, len(data), size):
        yield packet(i // size + 1, data[i:i + size])


class Lf:
    def __init__(self, lf, cwd, args=()):
        self.p = subprocess.Popen([lf, "-o", "3"] + list(args), cwd=cwd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.fd = self.p.stdout.fileno()
        self.text = bytearray()                 # lf's output, without protocol bytes

    def write(self, b):
        self.p.stdin.write(b)
        self.p.stdin.flush()

    def read(self, timeout):
        """One byte of output, or None."""
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return None
        b = os.read(self.fd, 1)
        return b[0] if b else None

    def expect(self, wanted, timeout=5.0):
        """Reads until one of the wanted control bytes; text goes to self.text."""
        end = time.time() + timeout
        while time.time() < end:
            c = self.read(end - time.time())
            if c is None:
                break
            if c in wanted:
                return c
            self.text.append(c)
        return None

    def drain(self, timeout=0.5):
        """Reads until lf is quiet; text goes to self.text."""
        while True:
            c = self.read(timeout)
            if c is None:
                return
            if c not in (ACK, NAK, CAN):
                self.text.append(c)

    def show(self):
        """Prints and clears the text so far, without CRs."""
        print(self.text.decode("utf-8", "replace").replace("\r", ""), end="")
        self.text.clear()


def send_file(lf, name, data, size, corrupt, repeat, cancel_at, length):
    """One file of a batch; returns False if it stopped."""
    lf.write(header(name, data, length))
    if lf.expect({ACK, NAK, CAN}) != ACK:
        print("sender: header not ACKed")
        return False
    if lf.expect({C}) != C:
        print("sender: no C after the header")
        return False
    for k, pkt in enumerate(packets(data, size)):
        if k == cancel_at:
            lf.write(bytes([CAN, CAN]))
            return False
        if k == corrupt:                        # bad CRC first: expect NAK
            bad = pkt[:-1] + bytes([pkt[-1] ^ 1])
            lf.write(bad)
            if lf.expect({ACK, NAK}) != NAK:
                print("sender: corrupt packet not NAKed")
                return False
            print("sender: corrupt packet NAKed")
        lf.write(pkt)
        if lf.expect({ACK, NAK, CAN}) != ACK:
            print("sender: packet", k + 1, "not ACKed")
            return False
        if k == repeat:                         # the same packet again: ACK, ignored
            lf.write(pkt)
            if lf.expect({ACK, NAK, CAN}) != ACK:
                print("sender: repeated packet not ACKed")
                return False
            print("sender: repeated packet ACKed")
    lf.write(bytes([EOT]))
    if lf.expect({ACK, NAK}) != NAK:
        print("sender: first EOT not NAKed")
        return False
    lf.write(bytes([EOT]))
    if lf.expect({ACK, NAK}) != ACK:
        print("sender: second EOT not ACKed")
        return False
    return True


def transfer(lf, files, block=0, size=128, corrupt=None, repeat=None, cancel_at=None,
             length=True):
    """Sends files [(name, data), ...] after `block ymodem`."""
    lf.write(b"%d ymodem\n" % block)
    if lf.expect({C}) != C:
        print("sender: no C to start")
        return
    for i, (name, data) in enumerate(files):
        if i and lf.expect({C}) != C:
            print("sender: no C for the next file")
            return
        if not send_file(lf, name, data, size, corrupt if i == 0 else None,
                         repeat if i == 0 else None, cancel_at, length):
            lf.drain(1.5)
            return
    if lf.expect({C}) != C:
        print("sender: no C for the end of the batch")
        return
    lf.write(packet(0, bytes(128)))             # the empty header: no more files
    if lf.expect({ACK, NAK}) != ACK:
        print("sender: end of batch not ACKed")
    lf.drain(0.5)


SOURCE = (b"\xef\xbb\xbf( loaded with ymodem: text before the first marker is ignored )\r\n"
          b"( BLOCK 3 )\r\n"
          b"( Test block three )\r\n"
          b".( three ) cr\r\n"
          b"( BLOCK IGNORE )\r\n"
          b".( ignored ) cr\r\n"
          b"( BLOCK++ )\r\n"
          b".( four, after three ) cr  ( a comment )\r\n"
          + b"( the line below is exactly 128 bytes )\r\n" + b"x" * 128 + b"\r\n"
          + b"( BLOCK 10 )\r\n"
          + b"".join(b"( row %2d )\r\n" % i for i in range(32))
          + b"( BLOCK++ )\r\n"
          b".( eleven, last line without a newline ) cr")


def same_as_putbin(work, files, first):
    """Writes the files with putbin.py into a copy of the block file as it
    was before, and says whether the blocks match what lf wrote."""
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../../..")
    ref = os.path.join(work, "ref.fb4")
    with open(ref, "wb") as f:
        f.write(b"LITEFORTH" + b" " * (64 * 4096 - 9))
    block = first
    for name, data in files:
        path = os.path.join(work, name)
        with open(path, "wb") as f:
            f.write(data)
        out = subprocess.run([sys.executable, os.path.join(root, "scripts", "putbin.py"),
                              path, str(block), ref], capture_output=True, text=True).stdout
        block = int(out.split("next free block ")[1].split()[0])
    with open(ref, "rb") as f:
        want = f.read()[first * 4096:block * 4096]
    with open(os.path.join(work, "lfblocks.fb4"), "rb") as f:
        got = f.read()[first * 4096:block * 4096]
    print("binary blocks %d-%d %s putbin.py's" % (first, block - 1,
          "are the same as" if got == want else "DIFFER from"))


def install_tests(lf, work):
    """-1 ymodem: whole block files into the 64-block file's space."""
    with open(os.path.join(work, "lfblocks.fb4"), "rb") as f:
        image = bytearray(f.read())
    row = b".( five, installed ) cr"
    image[5 * 4096:6 * 4096] = row + b" " * (4096 - len(row))
    image = bytes(image)
    print("-- install a whole block file (block 5 changed)")
    transfer(lf, [("new.fb4", image)], block=-1, size=1024, corrupt=3)
    lf.show()
    with open(os.path.join(work, "lfblocks.fb4"), "rb") as f:
        print("the file %s the image" % ("is" if f.read() == image else "ISN'T"))
    lf.write(b"capacity . 5 load\n")
    lf.drain(0.5)
    lf.show()
    for title, data in (
            ("bigger than the space", image + b" " * 4096),
            ("not whole blocks", image[:8193]),
            ("no signature", b"NOTFORTH" + image[8:8192])):
        print("-- refused: " + title)
        transfer(lf, [("bad.fb4", data)], block=-1, size=1024)
        lf.show()
    with open(os.path.join(work, "lfblocks.fb4"), "rb") as f:
        print("the file %s the image" % ("is still" if f.read() == image else "ISN'T"))
    print("-- the sender cancels after block 1")
    transfer(lf, [("new.fb4", image)], block=-1, size=1024, cancel_at=6)
    lf.show()
    lf.write(b"capacity . cr\n")
    lf.drain(0.5)
    lf.show()
    with open(os.path.join(work, "lfblocks.fb4"), "rb") as f:
        print("block 0: " + f.read(4096).decode().rstrip())
    print("-- two files: the second isn't written")
    transfer(lf, [("new.fb4", image), ("more.fb4", image)], block=-1, size=1024)
    lf.show()
    with open(os.path.join(work, "lfblocks.fb4"), "rb") as f:
        print("the file %s the image" % ("is" if f.read() == image else "ISN'T"))


def main():
    lf = Lf(sys.argv[1], sys.argv[2])
    lf.drain(0.5)
    lf.text.clear()                             # the banner
    print("-- 128-byte packets, a corrupt one and a repeated one")
    transfer(lf, [("src.f", SOURCE)], corrupt=2, repeat=4)
    lf.show()
    print("-- 1K packets, the same file")
    transfer(lf, [("src.f", SOURCE)], size=1024)
    lf.show()
    print("-- loading what came")
    lf.write(b"3 load 11 load\n")
    lf.drain(0.5)
    lf.show()
    print("-- two source files in one batch")
    transfer(lf, [("a.f", b"( BLOCK 12 )\n.( twelve ) cr\n"),
                  ("b.f", b"( BLOCK 13 )\n.( thirteen ) cr\n")], size=1024)
    lf.show()
    print("-- a line longer than 128 bytes")
    transfer(lf, [("long.f", b"( BLOCK 20 )\n.( twenty ) cr\n( BLOCK 21 )\n" + b"y" * 129
                  + b"\n( BLOCK 22 )\n.( never ) cr\n")])
    lf.show()
    print("-- the largest block number, past the end of the storage")
    transfer(lf, [("far.f", b"( BLOCK 4294967295 )\n.( too far ) cr\n")])
    lf.show()
    print("-- block 0")
    transfer(lf, [("zero.f", b"( BLOCK 0 )\nLITEFORTH\n")])
    lf.show()
    print("-- ( BLOCK++ ) first")
    transfer(lf, [("where.f", b"( BLOCK++ )\n.( where? ) cr\n")])
    lf.show()
    print("-- binary files from block 40: Ctrl+X runs, SUBs at the end, exact lengths")
    one = bytes((i * 7) & 255 for i in range(5000)) + bytes([CAN] * 5) + bytes([SUB] * 3)
    two = bytes([SUB]) * 100 + b"end" + bytes([SUB])
    transfer(lf, [("one.bin", one), ("two.bin", two)], block=40, size=1024, corrupt=1)
    lf.show()
    same_as_putbin(sys.argv[2], [("one.bin", one), ("two.bin", two)], 40)
    print("-- a binary file that doesn't fit")
    transfer(lf, [("big.bin", bytes(5 * 4096))], block=60, size=1024)
    lf.show()
    print("-- a binary file with no length in its header")
    transfer(lf, [("nolen.bin", b"abc")], block=50, length=False)
    lf.show()
    print("-- an XMODEM sender (no header)")
    lf.write(b"0 ymodem\n")
    lf.expect({C})
    lf.write(next(packets(b"( BLOCK 30 )\n", 128)))
    lf.drain(1.5)
    lf.show()
    print("-- the sender cancels")
    transfer(lf, [("cancel.f", b"( BLOCK 30 )\n" + b".( thirty ) cr\n" * 20)], cancel_at=1)
    lf.show()
    install_tests(lf, sys.argv[2])
    lf.write(b"bye\n")
    lf.drain(1.0)
    lf.p.wait(timeout=5)
    lf.show()


if __name__ == "__main__":
    main()
