"""Drives scripts/lfmcp.py with MCP requests against bin/lf over ptys, and
its wlink tool against a fake wlink that logs its arguments.

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

# A fake wlink: logs each call's arguments to wlink.log, prints a line, and
# answers `dump ... -o FILE` with the option bytes in ob.txt ("OBR WPR")
FAKE = os.path.abspath("fakewlink")
with open(FAKE, "w") as f:
    f.write(f"""#!{sys.executable}
import sys
args = sys.argv[1:]
with open("wlink.log", "a") as log:
    log.write(" ".join(args) + "\\n")
if args[:1] == ["dump"] and "-o" in args:
    obr, wpr = (int(x, 16) for x in open("ob.txt").read().split())
    with open(args[args.index("-o") + 1], "wb") as out:
        out.write(obr.to_bytes(4, "little") + wpr.to_bytes(4, "little"))
print("fake wlink: " + " ".join(args))
sys.exit(3 if args[:1] == ["status"] and len(args) > 1 else 0)
""")
os.chmod(FAKE, 0o755)


def wlink_log():
    try:
        with open("wlink.log") as f:
            lines = f.read().splitlines()
        os.remove("wlink.log")
        return lines
    except FileNotFoundError:
        return []


srv = subprocess.Popen([sys.executable, lfmcp, port, "--wlink", FAKE], stdin=subprocess.PIPE,
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
check("tools/list", names == ["forth", "forth_read", "forth_restart", "wlink"], names)

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

# wlink: the allowed commands pass through; nothing that locks or wipes
text, err = tool("wlink", command="status")
check("wlink status", not err and text == "fake wlink: status" and wlink_log() == ["status"], text)
text, err = tool("wlink", command="reset")
check("wlink reset", not err and wlink_log() == ["reset"], text)
text, err = tool("wlink", command="dump", address="0x08000000", length=16)
check("wlink dump", not err and wlink_log() == ["dump 0x8000000 16"], text)
text, err = tool("wlink", command="dump", address=0, length=5000)
check("wlink dump too long", err and wlink_log() == [], text)
for bad in ("erase", "protect", "unprotect", "write-mem", "write-reg", "halt", "set-power"):
    text, err = tool("wlink", command=bad)
    check(f"wlink {bad} refused", err and "isn't allowed" in text and wlink_log() == [], text)
with open("fw.bin", "wb") as f:
    f.write(b"\x13" * 64)
for obr, wpr, why in (("000003FE", "FFFFFFFF", "read protection is on"),
                      ("000003FC", "FFFFFF00", "write-protected")):
    with open("ob.txt", "w") as f:
        f.write(obr + " " + wpr)
    text, err = tool("wlink", command="flash", path=os.path.abspath("fw.bin"))
    log = wlink_log()
    check(f"wlink flash refused: {why}", err and why in text and len(log) == 1
          and log[0].startswith("dump 0x4002201c 8 -o "), (text, log))
with open("ob.txt", "w") as f:
    f.write("000003FC FFFFFFFF")
text, err = tool("wlink", command="flash", path=os.path.abspath("fw.bin"), address="0x08010000")
log = wlink_log()
check("wlink flash", not err and "not protected" in text and len(log) == 2
      and log[1] == "flash --address 0x8010000 " + os.path.abspath("fw.bin")
      and not any("-e" in l.split() or "--erase" in l for l in log), (text, log))
text, err = tool("wlink", command="flash", path="nosuch.bin")
check("wlink flash: no such file", err and wlink_log() == [], text)
text, err = tool("wlink", command="flash", path=os.path.abspath("ob.txt"))
check("wlink flash: not firmware", err and wlink_log() == [], text)

r = rpc("bogus/method")
check("unknown method", r.get("error", {}).get("code") == -32601, r)

srv.stdin.close()
srv.wait(5)

box = os.path.abspath("box")            # the same tools through files
mb = subprocess.Popen([sys.executable, lfmcp, port, "--mailbox", box],
                      stdout=subprocess.DEVNULL)
time.sleep(0.5)
with open(os.path.join(box, "r1.json"), "w", encoding="utf-8") as f:
    json.dump({"tool": "forth", "arguments": {"text": "6 7 + ."}}, f)
with open(os.path.join(box, "r2.json"), "w", encoding="utf-8") as f:
    f.write("not json")
os.utime(os.path.join(box, "r2.json"), (time.time() - 10, time.time() - 10))
answers = {}
for _ in range(100):
    for n in ("r1", "r2"):
        p = os.path.join(box, n + ".txt")
        if n not in answers and os.path.exists(p):
            answers[n] = open(p, encoding="utf-8").read()
    if len(answers) == 2:
        break
    time.sleep(0.1)
mb.kill()
check("mailbox answer", answers.get("r1") == "> 6 7 + .\n13 ok>\n", answers.get("r1"))
check("mailbox bad request", (answers.get("r2") or "").startswith("[error]"), answers.get("r2"))
check("mailbox deletes requests", not [n for n in os.listdir(box) if n.endswith(".json")],
      os.listdir(box))
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
