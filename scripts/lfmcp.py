#!/usr/bin/env python3
"""MCP server that gives an AI assistant LiteForth's terminal on a serial port.

Usage: lfmcp.py PORT [-b BAUD]        (COM17, /dev/ttyACM0, ...)

It speaks the Model Context Protocol over stdin/stdout (newline-delimited
JSON-RPC), so an MCP client such as the Claude desktop app can start it and
call its tools:

  forth          send lines to LiteForth and return what it printed, each
                 line (shown as `> line`) waiting for the `ok>` prompt
  forth_read     return what the board printed on its own (a running app)
  forth_restart  send Ctrl+X three times: on the CH32H417, restart the V5F
                 without starting the app

The port is opened for each call and closed after, so a terminal (kwai) can
use it in between, but not at the same time: Windows lets one program open
a COM port. Opening raises DTR, which the CH32H417's USB bridge needs before
it sends anything; output the board made meanwhile (up to 4 KB) comes with
the next call. No packages needed: Win32 through ctypes, or termios.
"""
import json
import os
import re
import sys
import time

ANSI = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")
PROMPT = "ok>"


# ---------------------------------------------------------------------------
# Serial port, opened per call
# ---------------------------------------------------------------------------

if os.name == "nt":
    import ctypes
    from ctypes import wintypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateFileW.restype = wintypes.HANDLE
    k32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD,
                                wintypes.HANDLE]
    INVALID = wintypes.HANDLE(-1).value

    class COMMTIMEOUTS(ctypes.Structure):
        _fields_ = [(n, wintypes.DWORD) for n in (
            "ReadIntervalTimeout", "ReadTotalTimeoutMultiplier",
            "ReadTotalTimeoutConstant", "WriteTotalTimeoutMultiplier",
            "WriteTotalTimeoutConstant")]

    class Port:
        def __init__(self, name, baud):
            path = name if name.startswith("\\\\.\\") else "\\\\.\\" + name
            h = k32.CreateFileW(path, 0xC0000000, 0, None, 3, 0, None)  # read/write, OPEN_EXISTING
            if h == INVALID or h is None:
                err = ctypes.get_last_error()
                why = {2: "not there", 5: "in use by another program (kwai?)"}.get(err, f"error {err}")
                raise OSError(f"can't open {name}: {why}")
            self.h = h
            dcb = ctypes.create_string_buffer(28)           # DCB: the port's settings,
            ctypes.c_uint32.from_buffer(dcb).value = 28     # with the baud rate changed
            if k32.GetCommState(h, dcb) and \
                    k32.BuildCommDCBW(f"baud={baud} parity=N data=8 stop=1 dtr=on rts=on", dcb):
                k32.SetCommState(h, dcb)
            t = COMMTIMEOUTS(0xFFFFFFFF, 0, 0, 0, 2000)     # reads return at once
            k32.SetCommTimeouts(h, ctypes.byref(t))
            k32.EscapeCommFunction(h, 5)                    # SETDTR
            k32.EscapeCommFunction(h, 3)                    # SETRTS

        def write(self, data):
            n = wintypes.DWORD()
            if not k32.WriteFile(self.h, data, len(data), ctypes.byref(n), None) or n.value != len(data):
                raise OSError("write failed: the port went away?")

        def read(self):
            buf = ctypes.create_string_buffer(4096)
            n = wintypes.DWORD()
            if not k32.ReadFile(self.h, buf, 4096, ctypes.byref(n), None):
                raise OSError("read failed: the port went away?")
            return buf.raw[:n.value]

        def close(self):
            k32.CloseHandle(self.h)
else:
    import fcntl
    import select
    import struct
    import termios
    import tty

    class Port:
        def __init__(self, name, baud):
            try:
                self.fd = os.open(name, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            except OSError as e:
                raise OSError(f"can't open {name}: {e.strerror}") from None
            try:
                tty.setraw(self.fd)
                attrs = termios.tcgetattr(self.fd)
                speed = getattr(termios, f"B{baud}", termios.B115200)
                attrs[4] = attrs[5] = speed
                termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
                bits = struct.pack("I", termios.TIOCM_DTR | termios.TIOCM_RTS)
                fcntl.ioctl(self.fd, termios.TIOCMBIS, bits)
            except (termios.error, OSError):
                pass                        # a pty has no modem lines

        def write(self, data):
            while data:
                select.select([], [self.fd], [], 1)
                data = data[os.write(self.fd, data):]

        def read(self):
            r, _, _ = select.select([self.fd], [], [], 0.01)
            if not r:
                return b""
            data = os.read(self.fd, 4096)
            if not data:
                raise OSError("the port went away")
            return data

        def close(self):
            os.close(self.fd)


# ---------------------------------------------------------------------------
# The tools
# ---------------------------------------------------------------------------

class Board:
    def __init__(self, port, baud):
        self.port, self.baud = port, baud

    def _collect(self, p, until, timeout, quiet=0.05):
        """Read until `until(text)` is true and nothing more comes for
        `quiet` seconds, or until timeout. Returns (bytes, done)."""
        out, end, last = b"", time.monotonic() + timeout, time.monotonic()
        while time.monotonic() < end:
            data = p.read()
            if data:
                out += data
                last = time.monotonic()
            elif until(out) and time.monotonic() - last >= quiet:
                return out, True
            else:
                time.sleep(0.005)
        return out, until(out)

    @staticmethod
    def _text(data):
        text = ANSI.sub("", data.decode("utf-8", "replace"))
        return text.replace("\r\n", "\n").replace("\r", "\n")

    def _open(self):
        p = Port(self.port, self.baud)
        earlier, _ = self._collect(p, lambda t: True, 0.15, quiet=0.1)
        return p, earlier

    def forth(self, text, timeout=5.0):
        p, earlier = self._open()
        try:
            out = []
            if earlier.strip():
                out.append("[before this call]\n" + self._text(earlier).rstrip() + "\n[end]\n")
            for line in text.splitlines() or [""]:
                p.write(line.encode("utf-8") + b"\n")
                data, done = self._collect(p, lambda t: t.rstrip().endswith(PROMPT.encode()), timeout)
                out.append("> " + line + "\n" + self._text(data).rstrip() + "\n")
                if not done:
                    out.append(f"[no {PROMPT} within {timeout:g} s"
                               " (still running, waiting for input, or the port went away)]\n")
                    break
            return "".join(out)
        finally:
            p.close()

    def read(self, seconds=1.0):
        p, earlier = self._open()
        try:
            data, _ = self._collect(p, lambda t: False, seconds)
            return self._text(earlier + data) or "[nothing]"
        finally:
            p.close()

    def restart(self):
        p, _ = self._open()
        try:
            p.write(b"\x18\x18\x18")
        finally:
            p.close()
        return "sent Ctrl+X three times; the V5F restarts without starting the app"


TOOLS = [
    {"name": "forth",
     "description": "Send one or more lines to LiteForth's terminal and return what it "
                    "printed, each line shown as '> line' and followed by its output. Each line "
                    "waits for the ok> prompt. Output from before the call is shown first.",
     "inputSchema": {"type": "object", "properties": {
         "text": {"type": "string", "description": "Forth input, one or more lines"},
         "timeout": {"type": "number", "description": "seconds to wait for ok> per line (default 5)"}},
         "required": ["text"]}},
    {"name": "forth_read",
     "description": "Return what the board prints on its own for a while (a running app), "
                    "without sending anything.",
     "inputSchema": {"type": "object", "properties": {
         "seconds": {"type": "number", "description": "how long to listen (default 1)"}}}},
    {"name": "forth_restart",
     "description": "Send Ctrl+X three times. On the CH32H417 this restarts LiteForth "
                    "without starting the app: the way out of a runaway app.",
     "inputSchema": {"type": "object", "properties": {}}},
]


def call(board, name, args):
    if name == "forth":
        return board.forth(args["text"], float(args.get("timeout", 5)))
    if name == "forth_read":
        return board.read(float(args.get("seconds", 1)))
    if name == "forth_restart":
        return board.restart()
    raise KeyError(name)


# ---------------------------------------------------------------------------
# MCP over stdio
# ---------------------------------------------------------------------------

def serve(board):
    out = sys.stdout
    for line in sys.stdin:
        if not line.strip():
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        mid, method = msg.get("id"), msg.get("method")
        if mid is None:                     # a notification
            continue
        params = msg.get("params") or {}
        if method == "initialize":
            result = {"protocolVersion": params.get("protocolVersion", "2024-11-05"),
                      "capabilities": {"tools": {}},
                      "serverInfo": {"name": "liteforth", "version": "1.0"}}
        elif method == "tools/list":
            result = {"tools": TOOLS}
        elif method == "tools/call":
            try:
                text, err = call(board, params.get("name"), params.get("arguments") or {}), False
            except KeyError as e:
                text, err = f"unknown tool or argument: {e}", True
            except OSError as e:
                text, err = str(e), True
            result = {"content": [{"type": "text", "text": text}], "isError": err}
        elif method == "ping":
            result = {}
        else:
            out.write(json.dumps({"jsonrpc": "2.0", "id": mid,
                                  "error": {"code": -32601, "message": f"no method {method}"}}) + "\n")
            out.flush()
            continue
        out.write(json.dumps({"jsonrpc": "2.0", "id": mid, "result": result}) + "\n")
        out.flush()


def main(argv):
    args = argv[1:]
    baud = 115200
    if "-b" in args:
        i = args.index("-b")
        baud = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 1 or args[0].startswith("-"):
        sys.stderr.write(__doc__)
        return 2
    sys.stdout.reconfigure(encoding="utf-8", newline="\n")
    sys.stdin.reconfigure(encoding="utf-8")
    serve(Board(args[0], baud))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
