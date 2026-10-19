#!/usr/bin/env python3
"""Plays a YMODEM sender against src/ymrecv.c (through drv.c) and checks
what it wrote, in which order, and what it said. Usage: test_ymrecv.py drv"""
import os
import select
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'ymodem'))
from sender import packet, header, packets, SOH, STX, EOT, ACK, NAK, CAN, C  # noqa: E402

DRV = sys.argv[1]
failures = 0


def pkg(blocks, seed=1):
    body = bytes((i * seed + (i >> 9)) & 255 for i in range(blocks * 4096 - 10))
    return b"LITEFORTHF" + body


class Rx:
    def __init__(self, room=8, start_ms=5000):
        self.tmp = tempfile.mkdtemp()
        self.out = os.path.join(self.tmp, "out.bin")
        self.p = subprocess.Popen([DRV, self.out, str(room), str(start_ms)],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)

    def send(self, b):
        self.p.stdin.write(b)
        self.p.stdin.flush()

    def read(self, timeout=3.0):
        r, _, _ = select.select([self.p.stdout], [], [], timeout)
        if not r:
            return None
        b = os.read(self.p.stdout.fileno(), 1)
        return b[0] if b else None

    def expect(self, want, timeout=3.0):
        """Reads until want (a byte or set of bytes); returns what came."""
        want = {want} if isinstance(want, int) else want
        while True:
            c = self.read(timeout)
            if c is None or c in want:
                return c

    def finish(self):
        self.p.stdin.close()
        err = self.p.stderr.read().decode()
        self.p.wait(timeout=20)
        data = open(self.out, "rb").read() if os.path.exists(self.out) else b""
        writes = [int(l.split()[1]) for l in err.splitlines() if l.startswith("write ")]
        result = [l for l in err.splitlines() if l.startswith("result")]
        return data, writes, (result[0] if result else err)


def check(cond, what):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", what)


def send_file(rx, name, data, size=1024, length=None, faults=()):
    """The whole sender: header, data, EOT, end of batch. faults: 'bad2'
    damages packet 2 once, 'dup3' sends packet 3 twice."""
    check(rx.expect(C) == C, f"{name}: a C to start")
    info = name.encode() + b"\0" + str(len(data) if length is None else length).encode()
    rx.send(packet(0, info + bytes(128 - len(info))))
    r = rx.expect({ACK, CAN})
    if r != ACK:
        return r
    r = rx.expect({C, CAN})             # a refusal comes here, as CAN CAN
    if r != C:
        return r
    for i, p in enumerate(packets(data, size), 1):
        if f"bad{i}" in faults:
            rx.send(p[:50] + bytes([p[50] ^ 1]) + p[51:])
            check(rx.expect({NAK, ACK}) == NAK, f"{name}: damaged packet {i} NAKed")
        rx.send(p)
        r = rx.expect({ACK, NAK, CAN})
        if r != ACK:
            return r
        if f"dup{i}" in faults:
            rx.send(p)
            check(rx.expect({ACK, NAK, CAN}) == ACK, f"{name}: repeated packet {i} ACKed")
    rx.send(bytes([EOT]))
    r = rx.expect({ACK, NAK, CAN})
    if r == CAN:
        return r
    check(r == NAK, f"{name}: first EOT NAKed")
    rx.send(bytes([EOT]))
    check(rx.expect({ACK, NAK}) == ACK, f"{name}: second EOT ACKed")
    check(rx.expect(C) == C, f"{name}: a C for the next file")
    rx.send(packet(0, bytes(128)))
    return rx.expect({ACK, CAN})


def case_good(size, faults=()):
    data = pkg(3)
    rx = Rx()
    r = send_file(rx, "p.lfu", data, size, faults=faults)
    got, writes, res = rx.finish()
    check(r == ACK, f"good {size} {faults}: the batch ends with ACK ({r})")
    check(got == data, f"good {size} {faults}: the blocks hold the file")
    check(writes == [1, 2, 0], f"good {size} {faults}: block 0 last ({writes})")
    check("result 0, size 12288: ymodem: p.lfu: 12288 bytes received" in res, res)


def case_refused(name, data, room, length, want, writes_want):
    rx = Rx(room)
    r = send_file(rx, name, data, 1024, length)
    got, writes, res = rx.finish()
    check(r == CAN, f"{name}: cancelled ({r})")
    check(writes == writes_want, f"{name}: writes {writes}")
    check(want in res, f"{name}: {res}")


case_good(1024)
case_good(128)
case_good(1024, ("bad2", "dup3"))
case_refused("odd.lfu", pkg(2)[:-100], 8, None, "isn't whole 4 KB blocks", [])
case_refused("big.lfu", pkg(3), 2, None, "is too big", [])
case_refused("magic.lfu", b"LITEFORTHX" + pkg(2)[10:], 8, None,
             "isn't a firmware update package", [])
case_refused("short.lfu", pkg(2), 8, 3 * 4096, "ended early", [1])

# the sender cancels part way: block 0 never written
rx = Rx()
rx.expect(C)
data = pkg(3)
rx.send(header("c.lfu", data))
rx.expect(ACK)
rx.expect(C)
for i, p in enumerate(packets(data, 1024), 1):
    if i == 10:                     # after 9K: block 1 written, block 0 held
        break
    rx.send(p)
    rx.expect(ACK)
rx.send(bytes([CAN, CAN]))
got, writes, res = rx.finish()
check(writes == [1] and "the sender cancelled" in res, f"cancel: {writes} {res}")

# nobody sends
rx = Rx(8, 1500)
got, writes, res = rx.finish()
check(writes == [] and "no sender started" in res, f"no sender: {res}")

if failures:
    print(f"ymrecv tests: {failures} failed")
    sys.exit(1)
print("ymrecv tests passed")
