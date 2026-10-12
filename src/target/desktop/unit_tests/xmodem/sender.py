#!/usr/bin/env python3
"""XMODEM (checksum) sender for testing LiteForth's `xmodem` word.

Usage: sender.py path/to/lf work_dir

Runs lf in work_dir (stdio mode, -o 3), types commands, and plays the
sender's side of several transfers, some with deliberate faults. Prints
lf's text output (without the protocol's NAK/ACK bytes) and notes on what
the sender saw, for run.sh to compare with expected.txt.
"""
import os
import select
import subprocess
import sys
import time

SOH, STX, EOT, ACK, NAK, CAN, SUB = 1, 2, 4, 6, 0x15, 0x18, 0x1A


class Lf:
    def __init__(self, lf, cwd):
        self.p = subprocess.Popen([lf, "-o", "3"], cwd=cwd, stdin=subprocess.PIPE,
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


def packets(data, size):
    data = data + bytes([SUB]) * (-len(data) % size)
    for i in range(0, len(data), size):
        chunk = data[i:i + size]
        n = (i // size + 1) & 0xFF
        head = SOH if size == 128 else STX
        yield bytes([head, n, 255 - n]) + chunk + bytes([sum(chunk) & 0xFF])


def transfer(lf, data, size=128, corrupt=None, repeat=None, cancel_at=None):
    lf.write(b"xmodem\n")
    if lf.expect({NAK}) != NAK:
        print("sender: no NAK to start")
        return
    for k, pkt in enumerate(packets(data, size)):
        if k == cancel_at:
            lf.write(bytes([CAN, CAN]))
            lf.drain(1.5)
            return
        if k == corrupt:                        # bad checksum first: expect NAK
            bad = pkt[:-1] + bytes([(pkt[-1] + 1) & 0xFF])
            lf.write(bad)
            if lf.expect({ACK, NAK}) != NAK:
                print("sender: corrupt packet not NAKed")
                return
            print("sender: corrupt packet NAKed")
        lf.write(pkt)
        if lf.expect({ACK, NAK, CAN}) != ACK:
            print("sender: packet", k + 1, "not ACKed")
            return
        if k == repeat:                         # the same packet again: ACK, ignored
            lf.write(pkt)
            if lf.expect({ACK, NAK, CAN}) != ACK:
                print("sender: repeated packet not ACKed")
                return
            print("sender: repeated packet ACKed")
    lf.write(bytes([EOT]))
    if lf.expect({ACK, NAK}) != ACK:
        print("sender: EOT not ACKed")
    lf.drain(0.5)


SOURCE = (b"\xef\xbb\xbf( loaded with xmodem: text before the first marker is ignored )\r\n"
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


def main():
    lf = Lf(sys.argv[1], sys.argv[2])
    lf.drain(0.5)
    lf.text.clear()                             # the banner
    print("-- 128-byte packets, a corrupt one and a repeated one")
    transfer(lf, SOURCE, corrupt=2, repeat=4)
    lf.show()
    print("-- 1K packets, the same file")
    transfer(lf, SOURCE, size=1024)
    lf.show()
    print("-- loading what came")
    lf.write(b"3 load 11 load\n")
    lf.drain(0.5)
    lf.show()
    print("-- a line longer than 128 bytes")
    transfer(lf, b"( BLOCK 20 )\n.( twenty ) cr\n( BLOCK 21 )\n" + b"y" * 129 + b"\n( BLOCK 22 )\n.( never ) cr\n")
    lf.show()
    print("-- block 0")
    transfer(lf, b"( BLOCK 0 )\nLITEFORTH\n")
    lf.show()
    print("-- ( BLOCK++ ) first")
    transfer(lf, b"( BLOCK++ )\n.( where? ) cr\n")
    lf.show()
    print("-- the sender cancels")
    transfer(lf, b"( BLOCK 30 )\n" + b".( thirty ) cr\n" * 20, cancel_at=1)
    lf.show()
    lf.write(b"bye\n")
    lf.drain(1.0)
    lf.p.wait(timeout=5)
    lf.show()


if __name__ == "__main__":
    main()
