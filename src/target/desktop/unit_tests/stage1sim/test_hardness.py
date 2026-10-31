#!/usr/bin/env python3
"""Checks scripts/admin/hardness.py's bad-input battery: every package its
make_bad() produces from a good one is rejected by the reference verifier
(sealblocks.verify, the same checks the board's sealed_verify_package makes),
and the good package passes. So the lab's hardness runner is known to throw
packages a correct RoT refuses, without a board.

It also asserts hardness.py's copy of the block-0 offsets matches
sealblocks.py, so a format change can't silently aim the corruptions at the
wrong bytes.

Reuses test_stage1.gen() for the keys and a good package. Needs the
cryptography package (as the rest of stage1sim does); the board-interaction
side of hardness.py is exercised by running it on a real board (a
board-tests.md entry), not here. Run by the stage1sim makefile."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import test_stage1 as t  # noqa: E402

sys.path.insert(0, os.path.join(t.ROOT, 'scripts'))
sys.path.insert(0, os.path.join(t.ROOT, 'scripts', 'admin'))
import sealblocks as sb  # noqa: E402
import hardness  # noqa: E402

checks = 0
failures = 0


def check(cond, what):
    global checks, failures
    checks += 1
    if not cond:
        failures += 1
        print(f'  FAIL: {what}')
    return cond


def main():
    # The offsets hardness.py corrupts must be the ones sealblocks.py signs.
    check(hardness.BLOCK == sb.BLOCK, 'BLOCK matches sealblocks.py')
    check(hardness.SIG_AT == sb.SIG_AT, 'SIG_AT matches sealblocks.py')
    check(hardness.IMAGE_HASH_AT == sb.IMAGE_HASH_AT, 'IMAGE_HASH_AT matches sealblocks.py')
    check(hardness.MMODE_SIG_AT == sb.MMODE_SIG_AT, 'MMODE_SIG_AT matches sealblocks.py')

    t.gen()
    root_pub = sb.root_public(os.path.join(t.WORK, 'root.pub'))
    good = t.PACKAGES['v1']
    with open(good, 'rb') as f:
        good_data = f.read()

    # The good package passes the reference verifier (the control).
    try:
        sb.verify(good_data, root_pub)
        check(True, 'the good package passes sealblocks.verify (control)')
    except sb.SealError as e:
        check(False, f'the good package passes sealblocks.verify (control): raised {e}')

    # Every corrupted package is rejected, and make_bad actually changed it.
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        bad = hardness.make_bad(good, d)
        names = {b[0] for b in bad}
        check(names == {c[0] for c in hardness.CORRUPTIONS},
              'make_bad wrote one package per corruption')
        for name, what, where, path in bad:
            with open(path, 'rb') as f:
                data = f.read()
            check(data != good_data, f'{name}: the package was actually changed')
            try:
                sb.verify(data, root_pub)
                check(False, f'{name}: sealblocks.verify accepted it (should refuse)')
            except sb.SealError as e:
                check(True, f'{name}: refused ({e})')
            except Exception as e:                      # noqa: BLE001
                # a malformed length/struct is still a refusal, not acceptance
                check(True, f'{name}: refused ({type(e).__name__}: {e})')

    if not failures:
        print(f'hardness: the bad-input battery is refused by the reference verifier '
              f'({len(hardness.CORRUPTIONS)} corruptions)')
    print(f'hardness: {checks} checks, {failures} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
