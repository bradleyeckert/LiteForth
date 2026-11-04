#!/usr/bin/env python3
"""Exercises the ESP32-C5 SOUP tooling: scripts/ESP32C5/soup_sbom.py (the SBOM
generator) and scripts/ESP32C5/cvecheck.py (the CVE monitor). Two parts:

  1. Integration, on the real repository files: the committed soup.spdx.json is
     up to date vs soup.lock.json, and the offline cvecheck of the pinned SOUP
     is clean (deterministic -- no network).
  2. The version matcher, on fixtures: a linked component in an affected range
     fails, a patched version is clean, and an unlinked match is informational
     unless --strict.

Runs in `make test` / CI; needs python3. See doc/ESP32C5/admin/soup.md."""
import io
import os
import subprocess
import sys
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..', '..'))
SCRIPTS = os.path.join(ROOT, 'scripts', 'ESP32C5')
FIX = os.path.join(HERE, 'fixtures')
sys.path.insert(0, SCRIPTS)
import soup_sbom       # noqa: E402
import cvecheck        # noqa: E402

fails = 0


def check(cond, what):
    global fails
    print(f'  {"ok  " if cond else "FAIL"} {what}')
    if not cond:
        fails += 1


def run_cli(script, args):
    r = subprocess.run([sys.executable, os.path.join(SCRIPTS, script), *args],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def run_cve(manifest, advisories, argv):
    """Call cvecheck.main with its file globals pointed at fixtures."""
    old_m, old_a = cvecheck.MANIFEST, cvecheck.ADVISORIES
    cvecheck.MANIFEST, cvecheck.ADVISORIES = manifest, advisories
    try:
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = cvecheck.main(argv)
        return rc, buf.getvalue()
    finally:
        cvecheck.MANIFEST, cvecheck.ADVISORIES = old_m, old_a


# --- 1. Integration on the real files ---------------------------------------
rc, out = run_cli('soup_sbom.py', ['--check'])
check(rc == 0, 'soup.spdx.json is up to date vs the manifest (run soup_sbom.py if not)')

rc, out = run_cli('soup_sbom.py', ['--list'])
check(rc == 0 and 'esp-idf' in out, 'soup_sbom --list prints the components')

rc, out = run_cli('cvecheck.py', [])
check(rc == 0, 'offline cvecheck of the pinned SOUP is clean')
check('no known advisories' in out or 'informational' in out,
      'cvecheck reports no linked advisory matches for the pins')

rc, out = run_cli('cvecheck.py', ['--list'])
check(rc == 0 and 'Monitoring' in out, 'cvecheck --list prints what is monitored')

# --- 2. The matcher, on fixtures --------------------------------------------
check(cvecheck.vtuple('V11.1.0') == (11, 1, 0), 'vtuple strips a leading V')
check(cvecheck.vtuple('3.6.3') < cvecheck.vtuple('3.6.10'), 'vtuple compares numerically, not lexically')

adv_range = {'ranges': [{'introduced': '0', 'fixed': '2.0.0'}]}
check(cvecheck.affected('1.0.0', adv_range), 'affected: version inside [introduced, fixed)')
check(not cvecheck.affected('2.0.0', adv_range), 'not affected: version == fixed (exclusive)')
check(not cvecheck.affected('2.1.0', adv_range), 'not affected: version past fixed')
check(cvecheck.affected('9', {'ranges': [{'introduced': '1'}]}), 'affected: open range (no fixed)')

ml = os.path.join(FIX, 'manifest_linked.json')
mu = os.path.join(FIX, 'manifest_unlinked.json')
ahit = os.path.join(FIX, 'adv_hit.json')
apatched = os.path.join(FIX, 'adv_patched.json')

rc, out = run_cve(ml, ahit, [])
check(rc == 1, 'a linked component in an affected range fails the gate')
check('FAIL' in out and 'CVE-TEST-0001' in out, 'the linked hit is reported as FAIL')
check('info' in out and 'CVE-TEST-0002' in out, 'the unlinked hit is reported as informational')

rc, out = run_cve(ml, apatched, [])
check(rc == 0 and 'no known advisories' in out, 'a version at/above the fix is clean')

rc, out = run_cve(mu, ahit, [])
check(rc == 0, 'an unlinked-only match does not fail by default')
rc, out = run_cve(mu, ahit, ['--strict'])
check(rc == 1, 'an unlinked match fails under --strict')

print(f'\nsoup tests: {"passed" if not fails else str(fails) + " FAILED"}')
sys.exit(1 if fails else 0)
