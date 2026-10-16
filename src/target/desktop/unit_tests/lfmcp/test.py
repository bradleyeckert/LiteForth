"""Drives scripts/lfmcp.py with MCP requests against bin/lf over ptys.

Usage: test.py path/to/lf path/to/lfmcp.py   (in a scratch directory)
"""
import json
import os
import pty
import select
import subprocess
import sys
import threading
import time
import tty

lf, lfmcp = (os.path.abspath(a) for a in sys.argv[1:3])
lf_m, lf_s = pty.openpty()              # lf's terminal
b_m, b_s = pty.openpty()                # the "board" port lfmcp.py opens
tty.setraw(lf_s)
tty.setraw(b_s)
port = os.ttyname(b_s)
board = subprocess.Popen([lf, "-o", "32"], stdin=lf_s, stdout=lf_s, stderr=lf_s)


def bridge():
    while True:
        r, _, _ = select.select([lf_m, b_m], [], [], 0.05)
        for fd in r:
            try:
                data = os.read(fd, 4096)
            except OSError:
                continue
            other = b_m if fd == lf_m else lf_m
            while data:
                data = data[os.write(other, data):]


threading.Thread(target=bridge, daemon=True).start()
time.sleep(0.5)
srv = subprocess.Popen([sys.executable, lfmcp, port], stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, text=True, encoding="utf-8")
ids = 0
failures = 0


def rpc(method, params=None, notify=False):
    global ids
    msg = {"jsonrpc": "2.0", "method": method, "params": params or {}}
    if not notify:
        ids += 1
        msg["id"] = ids
    srv.stdin.write(json.dumps(msg) + "\n")
    srv.stdin.flush()
    return None if notify else json.loads(srv.stdout.readline())


def check(what, ok, got):
    global failures
    if not ok:
        failures += 1
        print(f"FAILED: {what}: {got!r}")


def tool(name, **args):
    r = rpc("tools/call", {"name": name, "arguments": args})["result"]
    return r["content"][0]["text"], r["isError"]


r = rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                       "clientInfo": {"name": "test"}})["result"]
check("initialize", r["protocolVersion"] == "2025-06-18" and "tools" in r["capabilities"], r)
rpc("notifications/initialized", notify=True)
names = [t["name"] for t in rpc("tools/list")["result"]["tools"]]
check("tools/list", names == ["forth", "forth_read", "forth_restart"], names)

text, err = tool("forth", text="1 2 + .")
check("one line", not err and text == "> 1 2 + .\n3 ok>\n", text)
text, err = tool("forth", text="hex 10 .\ndecimal .( 学如不及 )")
check("two lines, UTF-8", text == "> hex 10 .\n10H ok>\n> decimal .( 学如不及 )\n学如不及 ok>\n", text)
text, err = tool("forth", text="nosuchword")
check("an error", "nosuchword ?" in text and text.rstrip().endswith("ok>"), text)
text, err = tool("forth_read", seconds=0.3)
check("forth_read", not err and text == "[nothing]", text)
text, err = tool("nosuchtool")
check("unknown tool", err, text)
r = rpc("bogus/method")
check("unknown method", r.get("error", {}).get("code") == -32601, r)

srv.stdin.close()
srv.wait(5)
board.kill()
os.close(b_s)                           # the port vanishes: a call says so
srv = subprocess.Popen([sys.executable, lfmcp, "/nonexistent/port"], stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE, text=True, encoding="utf-8")
rpc("initialize", {"protocolVersion": "2025-06-18"})
text, err = tool("forth", text="1 .")
check("missing port", err and "can't open" in text, text)
srv.stdin.close()
srv.wait(5)
print("lfmcp tests passed" if failures == 0 else f"lfmcp tests FAILED ({failures})")
sys.exit(1 if failures else 0)
