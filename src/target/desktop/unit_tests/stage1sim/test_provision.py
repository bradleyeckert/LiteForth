#!/usr/bin/env python3
"""Runs scripts/admin/provision.py (doc/admin/provisioning.md) on simulated
level 2 boards (sim_strict) with a fake wlink, as test_lockdown.py does
for lockdown.py: a station with a signed release, a batch, a card, pass 1
over boards that pass and boards it must refuse, the vendor's review and
package, and pass 2. One pair of ptys stands for the station's ports; a
"board" is a folder (flash, option bytes, card, UID) that the rig runs the
simulation in, and connecting the next one restarts it there. Needs
pyserial and the cryptography package."""
import json
import os
import pty
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import tty

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_stage1 as t  # noqa: E402
import test_lockdown as tl  # noqa: E402

PROVISION = os.path.join(t.ROOT, 'scripts', 'admin', 'provision.py')
STATION = os.path.join(t.WORK, 'station')
UIDS = {'a': '0a0b0c0d0e0f1011', 'b': '2122232425262728', 'c': '1112131415161718',
        'd': '3132333435363738', 'e': '4142434445464748', 'f': '5152535455565758',
        'g': '6162636465666768'}

# the fake wlink of test_lockdown.py, with each board's UID from its folder
CONNECTED = os.path.join(t.WORK, 'connected')     # the folder of the board connected now
FAKE_WLINK = tl.FAKE_WLINK.replace(
    'board = os.environ["FAKE_BOARD"]', 'board = open(%r).read().strip()' % CONNECTED).replace(
    'print("Chip ESIG: FlashSize(960KB) UID(01-02-03-04-05-06-07-08)")',
    'pu = os.path.join(board, "probe_uid")\n'
    '    u = open(pu if os.path.exists(pu) else os.path.join(board, "uid")).read().strip()\n'
    '    print("Chip ESIG: FlashSize(960KB) UID(" + "-".join(u[i:i + 2] for i in range(0, 16, 2)) + ")")')


class Abort(Exception):
    """provision.py didn't say what it should have: the rest can't follow"""


def check(cond, what, got=''):
    if not t.check(cond, what):
        print('   got:', str(got)[-3000:])
    return cond


class Rig:
    """The station's two ports, and whichever board is connected."""

    def __init__(self):
        self.usb_m, self.usb_s = pty.openpty()
        self.dbg_m, self.dbg_s = pty.openpty()
        for fd in (self.usb_m, self.usb_s, self.dbg_m, self.dbg_s):
            tty.setraw(fd)
        self.dir = None
        self.p = None
        self.lock = threading.Lock()
        self.stop = False
        threading.Thread(target=self.watch, daemon=True).start()

    def start(self):
        uid = open(os.path.join(self.dir, 'uid')).read().strip()
        seed = open(os.path.join(self.dir, 'seed')).read().strip()     # its RNG
        env = dict(os.environ, SIM_UID=uid, SIM_SETUP_MS='300', SIM_SEED=seed)
        self.p = subprocess.Popen([os.path.join(t.WORK, 'sim_strict')], cwd=self.dir, env=env,
                                  stdin=self.usb_m, stdout=self.usb_m, stderr=self.dbg_m)
        with open(os.path.join(self.dir, 'sim.pid'), 'w') as f:
            f.write(str(self.p.pid))

    def watch(self):
        while not self.stop:
            with self.lock:
                if self.p and self.p.poll() is not None and \
                        not os.path.exists(os.path.join(self.dir, 'halted')):
                    self.start()
            time.sleep(0.02)

    def connect(self, d):
        """Unplug the board connected, plug in the one in folder d"""
        with self.lock:
            if self.p:
                self.p.kill()
                self.p.wait()
            self.dir = d
            with open(CONNECTED, 'w') as f:
                f.write(d)
            if os.path.exists(os.path.join(d, 'halted')):
                os.remove(os.path.join(d, 'halted'))
            with open(os.path.join(d, 'shared.bin'), 'wb') as f:
                f.write(bytes(0x8000))          # power-up: SRAM forgets
            self.start()

    def close(self):
        self.stop = True
        if self.p:
            self.p.kill()


def new_board(name, uid, dirty=False, seed=None, probe_uid=None):
    b = t.Board('prov-' + name)
    if probe_uid:                               # what the probe says the UID is
        with open(os.path.join(b.dir, 'probe_uid'), 'w') as f:
            f.write(probe_uid)
    with open(os.path.join(b.dir, 'uid'), 'w') as f:
        f.write(uid)
    with open(os.path.join(b.dir, 'seed'), 'w') as f:
        f.write(seed or uid[:8])
    if dirty:                                   # something in the V3F's image area
        with open(os.path.join(b.dir, 'flash.bin'), 'r+b') as f:
            f.write(b'\x6f\x00\x00\x00' * 64)
    return b.dir


class Run:
    """provision.py as the operator runs it, answering its questions"""

    def __init__(self, rig, *args):
        self.rig = rig
        env = dict(os.environ, PROVISION_OPERATOR='test')
        self.env = env
        self.args = args
        self.lines = []
        self.q = queue.Queue()
        self.p = None

    def go(self, extra=()):
        cmd = [sys.executable, PROVISION, *self.args, '--station', STATION,
               '--wlink', os.path.join(t.WORK, 'fakewlink'),
               '--cli', os.ttyname(self.rig.usb_s), '--debug', os.ttyname(self.rig.dbg_s), *extra]
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, bufsize=1, env=self.env)
        threading.Thread(target=self.read, daemon=True).start()
        return self

    def read(self):
        for line in self.p.stdout:
            self.lines.append(line.rstrip('\n'))
            self.q.put(line)

    def wait(self, pattern, timeout=90):
        rx = re.compile(pattern)
        end = time.time() + timeout
        while time.time() < end:
            try:
                line = self.q.get(timeout=0.2)
            except queue.Empty:
                if self.p.poll() is not None and self.q.empty():
                    break
                continue
            if rx.search(line):
                return line
        print(f'   (no line matching {pattern!r}; the last lines:)')
        print('\n'.join(self.lines[-40:]))
        self.p.kill()
        t.check(False, f'provision.py {self.args[0]} said {pattern!r}')
        raise Abort(pattern)

    def board(self, d):
        """At "Connect the next board": the board's folder is this one now"""
        self.wait(r'Connect the next board')
        self.rig.connect(d)
        time.sleep(0.8)                          # its first boot, as when plugged in
        self.p.stdin.write('\n')
        self.p.stdin.flush()

    def outcome(self, want):
        """The board's outcome: want is 'stopped', or the UID it must end with"""
        line = self.wait(r'This board stopped|Board [0-9a-f]{16} (enrolled|done)')
        ok = ('stopped' in line) if want == 'stopped' else (f'Board {want} ' in line)
        if not check(ok, f'provision.py {self.args[0]}: board {want}', line):
            self.p.kill()
            raise Abort(line)

    def answer(self, pattern, text):
        line = self.wait(pattern)
        self.p.stdin.write(text + '\n')
        self.p.stdin.flush()
        return line

    def end(self):
        self.wait(r'Connect the next board')
        self.p.stdin.write('q\n')
        self.p.stdin.flush()
        self.p.wait(timeout=60)
        time.sleep(0.2)
        return self.p.returncode, '\n'.join(self.lines)


def provision(*args):
    r = subprocess.run([sys.executable, PROVISION, *args], capture_output=True, text=True,
                       env=dict(os.environ, PROVISION_OPERATOR='test'))
    return r.returncode, r.stdout + r.stderr


def sealblocks(*args):
    r = subprocess.run([sys.executable, os.path.join(t.ROOT, 'scripts', 'admin', 'sealblocks.py'),
                        *[str(x) for x in args]], capture_output=True, text=True, cwd=t.WORK)
    if r.returncode:
        sys.exit(f'sealblocks {args}: {r.stdout}{r.stderr}')
    return r.stdout


def wlink_log(d):
    p = os.path.join(d, 'wlink.log')
    if not os.path.exists(p):
        return []
    lines = open(p).read().splitlines()
    os.remove(p)
    return lines


def station():
    if os.path.isdir(STATION):
        shutil.rmtree(STATION)
    rel = os.path.join(STATION, 'release')
    os.makedirs(rel)
    with open(os.path.join(rel, 'LiteForth_V3F.bin'), 'wb') as f:
        f.write(bytes((i * 7 + 1) & 255 for i in range(20000)))
    shutil.copy(os.path.join(t.ROOT, 'bin', 'lfblocks.fb4'), rel)
    shutil.copy(os.path.join(t.WORK, 'root.pub'), rel)
    sealblocks('manifest', rel, '--release', 'rel.key', '--revision', 1,
               '--stage-size', 96 * 4096, '--backup-size', 96 * 4096)
    with open(os.path.join(t.WORK, 'other.key')) as f:   # a board enrolled before
        k = json.load(f)
    with open(os.path.join(STATION, 'boards.csv'), 'w') as f:
        f.write(f'# enrolled before\n{k["uid"]},{k["public"]},old\n')


def offline_tests():
    """The commands that need no board"""
    s = ('--station', STATION)
    code, out = provision('station-check', *s, '--wlink', os.path.join(t.WORK, 'fakewlink'))
    check(code == 0 and 'release: version 1' in out and 'boards.csv: 1 board' in out,
          'provision station-check: the release checks out', out)
    v3f = os.path.join(STATION, 'release', 'LiteForth_V3F.bin')
    good = open(v3f, 'rb').read()
    with open(v3f, 'r+b') as f:
        f.write(b'\x00')
    code, out = provision('station-check', *s)
    check(code == 1 and "isn't the file the manifest names" in out,
          'provision station-check: a changed image stops it', out)
    code, out = provision('batch-new', 'b1', *s)
    check(code == 1 and "doesn't check out" in out and not os.path.isdir(
        os.path.join(STATION, 'batches', 'b1')), 'provision batch-new: not on a changed release', out)
    with open(v3f, 'wb') as f:
        f.write(good)
    code, out = provision('batch-new', 'b1', *s)
    check(code == 0 and 'release version 1' in out, 'provision batch-new', out)
    code, out = provision('batch-new', 'b1', *s)
    check(code == 1 and 'exists' in out, 'provision batch-new: not twice', out)

    card = os.path.join(t.WORK, 'card')
    shutil.rmtree(card, ignore_errors=True)
    os.makedirs(card)
    code, out = provision('card', card, *s)
    check(code == 1 and 'not exFAT' in out, 'provision card: only exFAT', out)
    code, out = provision('card', card, *s, '--any-fs')
    sizes = {n: os.path.getsize(os.path.join(card, n)) for n in os.listdir(card)}
    check(code == 0 and sizes == {'lfblocks.fb4': os.path.getsize(os.path.join(
        t.ROOT, 'bin', 'lfblocks.fb4')), 'lfstage.bin': 96 * 4096, 'lfbackup.bin': 96 * 4096}
        and open(os.path.join(card, 'lfblocks.fb4'), 'rb').read() == open(os.path.join(
            t.ROOT, 'bin', 'lfblocks.fb4'), 'rb').read(),
        'provision card: the block file and the blank files, at the manifest\'s sizes',
        (out, sizes))
    code, out = provision('card', card, *s, '--any-fs')
    check(code == 1 and "isn't empty" in out, 'provision card: not on a card with files', out)


def main():
    t.gen()
    wlink = os.path.join(t.WORK, 'fakewlink')
    with open(wlink, 'w') as f:
        f.write(FAKE_WLINK.replace('PYTHON', sys.executable, 1))
    os.chmod(wlink, 0o755)
    station()
    offline_tests()
    batch = os.path.join(STATION, 'batches', 'b1')
    enrolled = os.path.join(batch, 'enrolled.csv')
    a = new_board('a', UIDS['a'])
    b = new_board('b', UIDS['b'])
    c = new_board('c', UIDS['c'])               # its UID is in boards.csv already
    d = new_board('d', UIDS['d'], dirty=True)   # not blank
    e = new_board('e', UIDS['e'], seed=UIDS['a'][:8])   # its RNG gives a's key
    f = new_board('f', UIDS['f'], probe_uid=UIDS['d'])  # the probe on another board
    g = new_board('g', UIDS['g'], probe_uid=bytes.fromhex(UIDS['g'])[::-1].hex())
    rig = Rig()
    try:
        # pass 1, run 1: a board not locked (the answer isn't LOCK), then
        # two it must refuse before flashing anything
        r = Run(rig, 'pass1', '--batch', 'b1').go()
        r.board(a)
        r.answer(r'Type LOCK', 'no')
        r.outcome('stopped')
        log_a = wlink_log(a)
        check(not os.path.exists(os.path.join(a, 'ob.bin')) and any(
            x.startswith('flash -R --address 0x8000000') for x in log_a),
            'pass1: flashed, but not LOCK: not locked', log_a)
        r.board(c)
        r.outcome('stopped')
        r.board(d)
        r.outcome('stopped')
        code, out = r.end()
        check(code == 1 and 'UID 1112131415161718 already enrolled' in out
              and "the chip isn't blank" in out and 'pass 1: 0 board(s) enrolled' in out,
              'pass1: an enrolled UID and a chip that isn\'t blank refused', out)
        check(not any(x.startswith('flash') for x in wlink_log(c) + wlink_log(d)),
              'pass1: nothing flashed on the boards refused', '')
        check(not os.path.exists(enrolled), 'pass1: nothing enrolled yet', '')

        # run 2: board a again (flashed by this batch, not locked: flashed
        # again), then board b; LOCK asked once
        r = Run(rig, 'pass1', '--batch', 'b1').go()
        r.board(a)
        r.answer(r'Type LOCK', 'LOCK')
        r.outcome('0a0b0c0d0e0f1011')
        r.board(b)
        r.outcome('2122232425262728')
        r.board(e)
        r.outcome('stopped')
        r.board(f)
        r.outcome('stopped')
        r.board(g)
        r.outcome('6162636465666768')
        code, out = r.end()
        check(code == 1 and out.count('Type LOCK') == 1 and 'flashing it again' in out
              and 'pass 1: 3 board(s) enrolled' in out,
              'pass1: three boards enrolled, LOCK typed once', out)
        check(f"enroll's UID {UIDS['f']} isn't the probe's {UIDS['d']}" in out,
              'pass1: a UID that isn\'t the probe\'s refused', out)
        check("the probe gives the UID's bytes reversed" in out,
              'pass1: the probe\'s UID in the other byte order taken, and noted', out)
        check("its public key is another board's" in out,
              'pass1: a board whose key repeats another\'s refused (a broken RNG)', out)
        lines = open(enrolled).read().split()
        check([x.split(',')[0] for x in lines] == [UIDS['a'], UIDS['b'], UIDS['g']]
              and all(len(x.split(',')[1]) == 64 for x in lines),
              'pass1: enrolled.csv has both lines', lines)
        rec = open(os.path.join(batch, 'records', UIDS['a'] + '.md')).read()
        check('new key made' in rec and 'Locked: stage 1 found the option bytes' in rec
              and f'**Result:** enrolled, `{lines[0]}`.' in rec and 'operator: test' in rec
              and 'security level 2, installed version 0, recovery' in rec,
              'pass1: the record has both ports\' output and the result', rec[-2000:])
        for x in (a, b, g):
            log = wlink_log(x)
            check(os.path.exists(os.path.join(x, 'ob.bin')) and not any(
                w in y for y in log for w in ('erase', 'protect', ' -e')) and all(
                not y.startswith('dump') or y.split()[1] in ('0x8000000', '0x4002201c')
                for y in log),
                'pass1: locked; wlink never erased, unprotected or read past the V3F\'s image',
                log)

        # the vendor: review, add to boards.csv, package for the batch
        code, out = provision('batch-review', batch, '--boards',
                              os.path.join(STATION, 'boards.csv'))
        check(code == 0 and '3 board(s), each with its record' in out
              and '--boards ' + enrolled in out, 'batch-review: the batch checks out', out)
        code, out = provision('batch-review', batch, '--boards',
                              os.path.join(STATION, 'boards.csv'), '--split', '1')
        check(code == 0 and os.path.exists(os.path.join(batch, 'enrolled-3.csv'))
              and 'batch-3.lfu' in out, 'batch-review: split into packages', out)
        rec_path = os.path.join(batch, 'records', UIDS['b'] + '.md')
        rec_b = open(rec_path).read()
        with open(rec_path, 'w') as f:
            f.write(rec_b.replace('new key made', 'key'))
        code, out = provision('batch-review', batch, '--boards', os.path.join(STATION, 'boards.csv'))
        check(code == 1 and f'{UIDS["b"]}: its record doesn\'t show' in out,
              'batch-review: a record that doesn\'t show the new key refused', out)
        with open(rec_path, 'w') as f:
            f.write(rec_b)
        vendor = os.path.join(t.WORK, 'vendor-boards.csv')
        shutil.copy(os.path.join(STATION, 'boards.csv'), vendor)
        code, out = provision('batch-review', batch, '--boards', vendor, '--append')
        added = open(vendor).read()
        check(code == 0 and UIDS['a'] in added and UIDS['b'] in added and '# batch b1' in added,
              'batch-review --append: the lines added to boards.csv', added)
        code, out = provision('batch-review', batch, '--boards', vendor)
        check(code == 1 and 'already' in out, 'batch-review: not twice', out)

        with open(os.path.join(t.WORK, 'pages.bin'), 'wb') as f:
            f.write(bytes((i * 13 + (i >> 9)) & 255 for i in range(t.PAGES_LEN)))
        pkg = os.path.join(t.WORK, 'batch.lfu')
        sealblocks('package', 'v1.bin', pkg, '--component', 'app', '--pages', 'pages.bin',
                   '--release', 'rel.key', '--boards', enrolled, '--revision', 1)
        pkg2 = os.path.join(t.WORK, 'batch-v2.lfu')
        sealblocks('package', 'v1.bin', pkg2, '--component', 'app', '--pages', 'pages.bin',
                   '--release', 'rel.key', '--boards', enrolled, '--revision', 2)
        test = os.path.join(t.WORK, 'product-test.txt')
        with open(test, 'w') as f:
            f.write('# the product\'s test\n1 2 + . ==> ok>\n')

        r = Run(rig, 'pass2', '--batch', 'b1', '--package', pkg2).go()
        r.p.stdin.write('q\n')                    # if it got as far as asking for a board
        r.p.stdin.flush()
        code = r.p.wait(timeout=60)
        time.sleep(0.2)
        out = '\n'.join(r.lines)
        check(code == 1 and 'version 2, but the release is version 1' in out
              and 'Connect the next board' not in out,
              'pass2: a package of another version refused before any board', out)

        # pass 2: boards a and b get the release; c isn't the batch's
        r = Run(rig, 'pass2', '--batch', 'b1', '--package', pkg).go(['--test', test])
        r.board(a)
        r.outcome('0a0b0c0d0e0f1011')
        r.board(b)
        r.outcome('2122232425262728')
        r.board(c)
        r.outcome('stopped')
        r.board(a)                              # done already: LiteForth, not recovery
        r.outcome('stopped')
        code, out = r.end()
        check(code == 1 and 'pass 2: 2 board(s) done' in out,
              'pass2: two boards done, two refused', out)
        inst = open(os.path.join(batch, 'installed.csv')).read()
        check(UIDS['a'] + ',1,' in inst and UIDS['b'] + ',1,' in inst,
              'pass2: installed.csv has both boards', inst)
        rec = open(os.path.join(batch, 'records', UIDS['a'] + '.md')).read()
        check('pass 2' in rec and '**Result:** version 1 installed and checked.' in rec
              and 'V3F update: version 1 confirmed' in rec and "'ok>', as it should" in rec
              and 'after `reboot`' in rec,
              'pass2: the record has the install, the confirm, the test and the reboot',
              rec[-2500:])
        fails = [n for n in os.listdir(os.path.join(batch, 'records')) if 'pass2-failed' in n]
        check(len(fails) == 2, 'pass2: a report for each board refused', fails)
    except Abort:
        pass                                    # counted as a failure already
    finally:
        rig.close()
    if t.failures:
        print(f'provision.py on the simulated board: {t.failures} checks FAILED')
        return 1
    print('provision.py on the simulated board: passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
