#!/usr/bin/env python3
"""Hardware-in-the-loop test for the V3F's CDC TX-timeout (TIM7).

The edge case (cdc_bridge.c): a host opens the CDC port but stops reading.
The V5F keeps writing; its tx ring (4 KB) fills; serial_putc then spins
waiting for room. TIM7's 100 us interrupt counts the wait, and once a
packet has waited longer than CDC_TX_TIMEOUT (1000 ticks = 100 ms) with
the port open, the bridge discards the tx ring so the V5F never waits
forever (doc/admin/firmware-updates.md section 8; board test B30 moved
this tick from TIM2 to TIM7).

How this exercises it: `dump` floods the terminal. We run a big dump twice.

  Baseline: read continuously, timing it and counting the bytes. Nothing
  is discarded, so we receive the whole dump -- call it B_full.

  Stall: send the same dump, then stop reading for a few seconds before
  draining the rest. If TIM7's timeout fires, the bridge discards ring
  after ring while we aren't reading, so most of the dump is thrown away
  and we receive far fewer bytes -- B_stall << B_full. If the timeout did
  NOT fire, the V5F would simply block with the ring full and lose
  nothing; resuming the read would then deliver the whole dump intact
  (B_stall ~ B_full). So the loss, (B_full - B_stall) / B_full, is the
  evidence: a large loss means the discard -- and therefore TIM7's
  timeout -- did its job. (Liveness alone doesn't discriminate: resuming
  the read unblocks the V5F either way, so the board answers afterwards
  in both cases. We check it as a sanity test, not as the criterion.)

Run it on the machine the board is plugged into, with nothing else holding
the CDC port (close lfmcp / a terminal first):

    python3 scripts/admin/cdc_txtimeout.py            # autodetect the port
    python3 scripts/admin/cdc_txtimeout.py --port COM17
    python3 scripts/admin/cdc_txtimeout.py --cells 32768 --stall 3

Needs pyserial. Read-only on the board (`dump` reads VM memory); it leaves
the terminal at a clean prompt. The board must be at the `ok>` prompt
(not running an app that owns the terminal) when the test starts.
"""

import argparse
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is needed: pip install pyserial")

CLI_VID, CLI_PID = 0x1A86, 0xFE0C       # LiteForth's CDC port (the V3F)
PROMPT = b"ok>"


def find_port():
    for p in list_ports.comports():
        if p.vid == CLI_VID and p.pid == CLI_PID:
            return p.device
    return None


def drain(s, quiet=0.3, limit=4.0):
    """Read and throw away until the line goes quiet. Returns the bytes."""
    got = bytearray()
    last = time.monotonic()
    start = last
    while time.monotonic() - last < quiet and time.monotonic() - start < limit:
        chunk = s.read(4096)
        if chunk:
            got += chunk
            last = time.monotonic()
    return bytes(got)


def prompt(s, timeout=5.0):
    """Nudge the board and wait for an ok>. True if it answered."""
    s.reset_input_buffer()
    s.write(b"\r")
    s.flush()
    deadline = time.monotonic() + timeout
    seen = bytearray()
    while time.monotonic() < deadline:
        seen += s.read(256)
        if PROMPT in seen:
            drain(s)
            return True
    return False


def read_until_prompt(s, timeout):
    """Read, counting every byte, until an ok> arrives or time runs out.
    Returns (byte_count, saw_prompt, seconds)."""
    t0 = time.monotonic()
    deadline = t0 + timeout
    total = 0
    tail = b""                          # keep a little, to spot the prompt
    saw = False
    while time.monotonic() < deadline:
        chunk = s.read(8192)
        if not chunk:
            continue
        total += len(chunk)
        tail = (tail + chunk)[-64:]
        if PROMPT in tail:
            saw = True
            break
    return total, saw, time.monotonic() - t0


def dump_cmd(cells):
    return b"decimal 0 %d dump\r" % cells


def main():
    ap = argparse.ArgumentParser(description="V3F CDC TX-timeout (TIM7) HIL test")
    ap.add_argument("--port", help="serial port (default: autodetect 1A86:FE0C)")
    ap.add_argument("--cells", type=int, default=16384,
                    help="dump size in cells (default 16384 ~= 256 KB of output)")
    ap.add_argument("--stall", type=float, default=2.0,
                    help="seconds to stop reading, > 0.1 s timeout (default 2)")
    ap.add_argument("--loss", type=float, default=0.5,
                    help="min fraction of bytes that must be lost to pass (default 0.5)")
    a = ap.parse_args()

    port = a.port or find_port()
    if not port:
        sys.exit("no CDC port found (1A86:FE0C); name one with --port")

    print(f"port {port}, dump {a.cells} cells, stall {a.stall}s")
    with serial.Serial(port, 115200, timeout=0.1) as s:
        s.dtr = True                    # the bridge's host_open; pyserial raises it anyway
        time.sleep(0.2)
        if not prompt(s):
            sys.exit("no ok> from the board: is it at the prompt, and is the port free?")

        # Baseline: read the whole dump as it comes.
        s.reset_input_buffer()
        s.write(dump_cmd(a.cells))
        s.flush()
        b_full, ok_full, t_full = read_until_prompt(s, timeout=60)
        if not ok_full:
            sys.exit(f"baseline dump didn't finish (read {b_full} bytes); "
                     "try a smaller --cells")
        print(f"baseline: {b_full} bytes in {t_full:.2f}s, reached ok>")
        drain(s)

        # Stall: send the dump, then don't read for a while.
        if not prompt(s):
            sys.exit("lost the prompt before the stall phase")
        s.reset_input_buffer()
        s.write(dump_cmd(a.cells))
        s.flush()
        time.sleep(a.stall)             # port open (DTR), host not reading
        b_stall, ok_stall, t_stall = read_until_prompt(s, timeout=60)
        print(f"stall:    {b_stall} bytes "
              f"({'reached' if ok_stall else 'no'} ok> after {a.stall}s of not reading)")

        # Liveness sanity check: the board answers afterwards.
        drain(s)
        s.reset_input_buffer()
        s.write(b"decimal 424242 .\r")
        s.flush()
        alive_deadline = time.monotonic() + 5
        seen = bytearray()
        while time.monotonic() < alive_deadline:
            seen += s.read(256)
            if b"424242" in seen and PROMPT in seen:
                break
        alive = b"424242" in seen and PROMPT in seen
        drain(s)

        loss = (b_full - b_stall) / b_full if b_full else 0.0
        print(f"loss:     {loss*100:.1f}% of the baseline bytes "
              f"were discarded during the stall")
        print(f"alive:    board {'answered' if alive else 'did NOT answer'} after the stall")

        ok = ok_stall and alive and loss >= a.loss
        if ok:
            print(f"\nPASS: the bridge discarded the tx ring while unread "
                  f"(>= {a.loss*100:.0f}% lost) and the V5F kept going -- "
                  f"TIM7's TX timeout works.")
        else:
            print("\nFAIL:")
            if not ok_stall:
                print("  - the dump never finished after the stall")
            if not alive:
                print("  - the board didn't answer afterwards (it may be wedged; "
                      "Ctrl+X x3 resets it)")
            if loss < a.loss:
                print(f"  - too little was discarded ({loss*100:.1f}% < "
                      f"{a.loss*100:.0f}%): the ring may not have filled "
                      f"(raise --cells or --stall), or the timeout didn't fire")
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
