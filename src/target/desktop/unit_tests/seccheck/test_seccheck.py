#!/usr/bin/env python3
"""Exercises scripts/ESP32C5/seccheck.py, the ESP32-C5 security-config gate,
on fixtures (a compliant config, an insecure one, a development one) and on
the committed production overlay. Runs in `make test` / CI; needs python3."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..', '..'))
SECCHECK = os.path.join(ROOT, 'scripts', 'ESP32C5', 'seccheck.py')
FIX = os.path.join(HERE, 'fixtures')
OVERLAY = os.path.join(ROOT, 'src', 'target', 'ESP32C5', 'sdkconfig.secure.defaults')

fails = 0


def run(args):
    r = subprocess.run([sys.executable, SECCHECK, *args],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def check(cond, what):
    global fails
    print(f'  {"ok  " if cond else "FAIL"} {what}')
    if not cond:
        fails += 1


# 1. a compliant config passes in production
rc, out = run([os.path.join(FIX, 'good.sdkconfig')])
check(rc == 0, 'compliant config: production passes')
check('baseline met' in out, 'compliant config: says baseline met')

# 2. an insecure config fails in production...
rc, out = run([os.path.join(FIX, 'bad.sdkconfig')])
check(rc != 0, 'insecure config: production fails')
check('Secure Boot enabled' in out and 'FAIL' in out, 'insecure config: flags missing secure boot')
check('Development mode' in out, 'insecure config: flags flash-enc development mode')
check('allow JTAG' in out.lower() or 'allow_jtag' in out.lower() or 'JTAG' in out,
      'insecure config: flags the allow-JTAG escape')

# 3. ...and still fails in dev, because insecure escapes are never allowed
rc, out = run(['--profile', 'dev', os.path.join(FIX, 'bad.sdkconfig')])
check(rc != 0, 'insecure config: dev profile still fails (escapes never OK)')

# 4. a development build passes under --profile dev...
rc, out = run(['--profile', 'dev', os.path.join(FIX, 'dev.sdkconfig')])
check(rc == 0, 'dev config: dev profile passes')
check('not yet' in out, 'dev config: hardware controls reported as not-yet')

# 5. ...but the same dev build fails the production gate
rc, out = run([os.path.join(FIX, 'dev.sdkconfig')])
check(rc != 0, 'dev config: production profile fails (hardware controls absent)')

# 6. the committed production overlay itself satisfies the production baseline
rc, out = run([OVERLAY])
check(rc == 0, 'sdkconfig.secure.defaults: satisfies the production baseline')

# 7. --list works
rc, out = run(['--list'])
check(rc == 0 and 'Secure Boot enabled' in out, '--list prints the baseline')

print(f'\nseccheck tests: {"passed" if not fails else str(fails)+" FAILED"}')
sys.exit(1 if fails else 0)
