#!/usr/bin/env python3
"""scripts/rtm.py must refuse each kind of broken item: copies of
doc/quality's item files, each broken one way, checked with --dir/--out."""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..', '..'))
QDIR = os.path.join(ROOT, 'doc', 'quality')
WORK = os.path.join(HERE, 'work')
RTM = [sys.executable, os.path.join(ROOT, 'scripts', 'rtm.py')]
FILES = ['needs.md', 'system.md', 'architecture.md', 'modules.md']

failures = checks = 0


def fresh():
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    for f in FILES:
        shutil.copy(os.path.join(QDIR, f), WORK)


def edit(name, old, new):
    path = os.path.join(WORK, name)
    text = open(path, encoding='utf-8').read()
    assert old in text, (name, old)
    open(path, 'w', encoding='utf-8').write(text.replace(old, new, 1))


def run(*args):
    r = subprocess.run(RTM + ['--dir', WORK, '--out', os.path.join(WORK, 'rtm.md'), *args],
                       capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def expect(what, change, message):
    global failures, checks
    fresh()
    change()
    code, out = run()
    checks += 1
    if code != 1 or message not in out:
        failures += 1
        print(f'FAIL: {what}: exit {code}, wanted "{message}"\n{out}')


def main():
    global failures, checks
    fresh()
    code, out = run()
    checks += 1
    if code != 0:
        failures += 1
        print(f'FAIL: the real items, copied: {out}')
    code, out = run('--check')
    checks += 1
    if code != 0:
        failures += 1
        print(f'FAIL: --check right after writing: {out}')
    edit('system.md', 'Interpret and compile from the terminal', 'Interpret and compile')
    code, out = run('--check')
    checks += 1
    if code != 1 or 'out of date' not in out:
        failures += 1
        print(f'FAIL: a changed item makes rtm.md out of date: {out}')

    expect('a suite that doesn\'t exist',
           lambda: edit('system.md', 'unit:update::test_rollback', 'unit:nosuchsuite'),
           'nosuchsuite doesn\'t exist')
    expect('a text not in its suite',
           lambda: edit('system.md', 'unit:update::test_rollback', 'unit:update::test_rolback'),
           '"test_rolback" isn\'t in update')
    expect('a board test that doesn\'t exist',
           lambda: edit('system.md', 'board:B4,', 'board:B999,'),
           'no B999 in doc/admin/board-tests.md')
    expect('a superseded board test cited as evidence',
           lambda: edit('system.md', 'board:B4,', 'board:B12,'),
           'B12 is superseded')
    expect('an unknown kind of evidence',
           lambda: edit('system.md', 'board:B4,', 'hope:B4,'),
           'unknown kind')
    expect('a parent that doesn\'t exist',
           lambda: edit('system.md', '- **Parent:** UN-07\n', '- **Parent:** UN-99\n'),
           'parent UN-99 doesn\'t exist')
    expect('a parent at the wrong level',
           lambda: edit('modules.md', '- **Parent:** ARC-VM\n', '- **Parent:** SYS-VM-01\n'),
           'is a SYS, not a ARC')
    expect('a duplicate ID',
           lambda: edit('system.md', '### SYS-LANG-02:', '### SYS-LANG-01:'),
           'is defined twice')
    expect('a bad status',
           lambda: edit('system.md', '- **Status:** verified', '- **Status:** done'),
           'status "done"')
    expect('a module\'s code that doesn\'t exist',
           lambda: edit('modules.md', 'src/vm_rv32.S', 'src/vm_rv64.S'),
           'src/vm_rv64.S doesn\'t exist')
    expect('a symbol not in its code',
           lambda: edit('modules.md', 'src/vm.c::vmExec', 'src/vm.c::vmExecute2'),
           '"vmExecute2" isn\'t in src/vm.c')
    expect('a design heading that doesn\'t exist',
           lambda: edit('modules.md', 'doc/popthehood.md#VM memory', 'doc/popthehood.md#No such part'),
           'no heading "No such part"')
    expect('a need with a parent',
           lambda: edit('needs.md', '- **Verified by:** board:B24, board:B26\n',
                        '- **Parent:** UN-02\n- **Verified by:** board:B24, board:B26\n'),
           'a need has no parent')
    expect('a requirement with no parent',
           lambda: edit('system.md', '- **Parent:** UN-01, UN-02\n', ''),
           'has no parent')

    fresh()
    run()
    code, out = run('--strict')
    checks += 1
    if code != 1 or 'gap(s)' not in out:
        failures += 1
        print(f'FAIL: --strict fails on the gaps there are: {out}')

    if failures:
        print(f'rtm tests: {failures} of {checks} checks failed')
        return 1
    print(f'rtm tests passed ({checks} checks)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
