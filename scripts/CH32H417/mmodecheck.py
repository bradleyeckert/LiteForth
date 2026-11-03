#!/usr/bin/env python3
"""Checks that the V5F's machine-mode code calls only machine-mode code.

The V5F runs its trap and interrupt handlers from flash (.mmode, run in
place) and WCH's flash routines from a copy made just before each use
(.ramfunc), so user mode, which can write ITCM and DTCM, can't change what
machine mode runs (V5F/User/mmode.h). That only holds if nothing there
calls or jumps into ITCM: a helper such as memcpy, or -msave-restore's
__riscv_save_N. This disassembles both sections and fails on any direct
call or jump that lands outside them, and on any vector table entry that
doesn't point into .mmode. Indirect calls (jalr through a register loaded
from memory) can't be checked here; machine mode's code has none today.

Usage: mmodecheck.py ELF   (objdump and nm from $RISCV_PREFIX)
Exit status: 0 clean, 1 a call or vector leaves machine mode's code.
"""
import os
import re
import subprocess
import sys

PREFIX = os.environ.get('RISCV_PREFIX', 'riscv-none-elf-')


def run(tool, *args):
    return subprocess.run([PREFIX + tool, *args], check=True, capture_output=True,
                          text=True).stdout


def main(elf):
    syms = {}
    for line in run('nm', elf).splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    need = ('_mmode_start', '_mmode_end', '_ramfunc_vma', '_ramfunc_end')
    if any(n not in syms for n in need):
        print('mmodecheck: no .mmode/.ramfunc symbols in ' + elf)
        return 1
    ranges = [(syms['_mmode_start'], syms['_mmode_end']),
              (syms['_ramfunc_vma'], syms['_ramfunc_end'])]

    def inside(a):
        return any(lo <= a < hi for lo, hi in ranges)

    bad = []
    for sec in ('.mmode', '.ramfunc'):
        text = run('objdump', '-d', '-j', sec, elf)
        fn = '?'
        for line in text.splitlines():
            m = re.match(r'^([0-9a-f]+) <(.+)>:$', line)
            if m:
                fn = m.group(2)
                continue
            # direct jumps and calls: jal/j/c.j/c.jal and branches, and the
            # auipc+jalr pairs objdump annotates with "# addr <sym>"
            m = re.search(r'\t(c\.)?(jal|j|beq|bne|blt|bge|bltu|bgeu|beqz|bnez|blez|bgez|'
                          r'bltz|bgtz|jalr)\b.*?\b([0-9a-f]+) <([^>]+)>', line)
            if m:
                target = int(m.group(3), 16)
                if not inside(target):
                    bad.append(f'{sec}: {fn} -> {m.group(4)} ({target:08x})')

    # the vector table: mode 3, a table of handler addresses, at _vector_base.
    # (It is no longer at .mmode's start: the wake entry _start is first, so
    # the V5F wakes into .mmode and runs the bring-up in place -- part 4.)
    # It ends where the first symbol after it starts.
    start = syms.get('_vector_base', syms['_mmode_start'])
    end = min((v for v in syms.values() if start < v < syms['_mmode_end']), default=start)
    data = run('objdump', '-s', '-j', '.mmode', '--start-address', hex(start),
               '--stop-address', hex(end), elf)
    words = []
    for line in data.splitlines():
        m = re.match(r'^ ([0-9a-f]+) ((?:[0-9a-f]{8} ?){1,4})', line)
        if m:
            for w in m.group(2).split():
                words.append(int.from_bytes(bytes.fromhex(w), 'little'))
    if len(words) < 16:
        bad.append('no vector table found at .mmode\'s start')
    for i, w in enumerate(words):
        if i == 0 or w == 0:
            continue                    # the reset vector (start-up code), reserved entries
        if not inside(w & ~1):
            bad.append(f'vector {i}: {w:08x} is outside machine mode\'s code')
    if bad:
        print('mmodecheck: machine mode reaches code user mode can write:')
        for b in sorted(set(bad)):
            print('  ' + b)
        return 1
    print(f'mmodecheck: machine mode\'s code ({syms["_mmode_end"] - syms["_mmode_start"]} bytes '
          f'in flash, {syms["_ramfunc_end"] - syms["_ramfunc_vma"]} copied to ITCM) '
          f'calls only itself; {len(words)} vectors checked')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1]))
