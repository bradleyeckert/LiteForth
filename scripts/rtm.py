#!/usr/bin/env python3
"""The Requirements Traceability Matrix (doc/quality/README.md).

    rtm.py               write doc/quality/rtm.md from the requirement files
    rtm.py --check       exit 1 if a link is broken, a rule is broken, or
                         rtm.md isn't what this would write (as CI runs it)
    rtm.py --strict      also exit 1 for gaps (an item not verified at its
                         level, a need with no system requirement, ...)
    rtm.py --gaps        print the gaps and nothing else

The items live in doc/quality/needs.md (UN), system.md (SYS),
architecture.md (ARC) and modules.md (MOD), each as

    ### SYS-UPD-03: A title
    What it requires, in one paragraph or more.

    - **Parent:** UN-07, UN-08
    - **Verified by:** unit:update::test_rollback, integ:stage1sim::s_rollback, board:B4
    - **Status:** verified

and for MOD items also **Code:** and **Design:** (paths). Every link is
checked against the repository: a test suite (and a text in it), a
board-test entry in doc/admin/board-tests.md, a file, a symbol in a file.
Standard library only.
"""
import argparse
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
QDIR = os.path.join(ROOT, 'doc', 'quality')
FILES = ['needs.md', 'system.md', 'architecture.md', 'modules.md']
OUT = os.path.join(QDIR, 'rtm.md')
SUITES = os.path.join(ROOT, 'src', 'target', 'desktop', 'unit_tests')
BOARD_TESTS = os.path.join(ROOT, 'doc', 'admin', 'board-tests.md')

LEVELS = ['UN', 'SYS', 'ARC', 'MOD']
LEVEL_NAME = {'UN': 'Stakeholder need', 'SYS': 'System requirement',
              'ARC': 'Architecture element', 'MOD': 'Module'}
PARENT_LEVEL = {'SYS': 'UN', 'ARC': 'SYS', 'MOD': 'ARC'}
# The kinds of evidence that count at each level of the V (README.md)
# (inspection and CI checks count where a test can't show it: README.md)
COUNTS = {'UN': {'board', 'demo'},
          'SYS': {'sys', 'integ', 'board', 'insp', 'ci'},
          'ARC': {'integ', 'sys', 'board', 'insp', 'ci'},
          'MOD': {'unit'}}
KINDS = {'unit', 'integ', 'sys', 'board', 'demo', 'insp', 'ci'}
STATUSES = {'verified', 'partial', 'planned'}

HEAD = re.compile(r'^### ((UN|SYS|ARC|MOD)-[A-Z0-9]+(?:-[A-Z0-9]+)*): (.+?)\s*$')
FIELD = re.compile(r'^- \*\*([A-Za-z ]+):\*\* *(.*)$')


class Item:
    def __init__(self, ident, level, title, path, line):
        self.id, self.level, self.title = ident, level, title
        self.path, self.line = path, line
        self.text, self.fields = [], {}
        self.children = []

    def refs(self, field):
        v = self.fields.get(field, '').strip()
        if not v or v.lower() in ('none', '-'):
            return []
        return [r.strip() for r in v.split(',') if r.strip()]

    @property
    def status(self):
        return self.fields.get('Status', '').strip().lower()

    def where(self):
        return f'{os.path.relpath(self.path, ROOT)}:{self.line}'


def parse(qdir=QDIR):
    items, errors = {}, []
    for name in FILES:
        path = os.path.join(qdir, name)
        try:
            lines = open(path, encoding='utf-8').read().split('\n')
        except OSError as e:
            errors.append(f'{name}: {e}')
            continue
        cur = None
        for i, line in enumerate(lines, 1):
            m = HEAD.match(line)
            if m:
                ident, level, title = m.group(1), m.group(2), m.group(3)
                cur = Item(ident, level, title, path, i)
                if ident in items:
                    errors.append(f'{cur.where()}: {ident} is defined twice '
                                  f'(also {items[ident].where()})')
                items[ident] = cur
                continue
            if line.startswith('#'):
                cur = None
                continue
            if cur is None:
                continue
            f = FIELD.match(line)
            if f:
                cur.fields[f.group(1)] = f.group(2)
            elif line.strip() and not cur.fields:
                cur.text.append(line.strip())
    return items, errors


# Evidence --------------------------------------------------------------

_text_cache = {}


def files_under(path):
    if os.path.isfile(path):
        return [path]
    out = []
    for d, dirs, names in os.walk(path):
        dirs[:] = [x for x in dirs if x not in ('work', '__pycache__', 'reports')]
        for n in names:
            p = os.path.join(d, n)
            if os.path.getsize(p) < 2_000_000 and not n.endswith(('.img', '.bin', '.fb4', '.o')):
                out.append(p)
    return out


def contains(path, text):
    for p in files_under(path):
        if p not in _text_cache:
            try:
                with open(p, 'rb') as f:
                    data = f.read()
                _text_cache[p] = data.decode('utf-8', 'replace') if b'\0' not in data else ''
            except OSError:
                _text_cache[p] = ''
        if text in _text_cache[p]:
            return True
    return False


def board_entries():
    try:
        text = open(BOARD_TESTS, encoding='utf-8').read()
    except OSError:
        return {}
    return {m.group(1): m.group(2).strip()
            for m in re.finditer(r'^### (B\d+[a-z]?)\. (.+?)(?: \(\d+ \w+ \d+\))?$', text, re.M)}


BOARD = board_entries()


def target_path(t):
    """A suite name (a directory of unit_tests) or a repository path"""
    if '/' not in t and '.' not in t:
        return os.path.join(SUITES, t)
    return os.path.join(ROOT, t)


def check_path_ref(spec):
    """PATH or PATH::TEXT (TEXT must occur in it); returns an error or None"""
    path, _, text = spec.partition('::')
    full = target_path(path)
    if not os.path.exists(full):
        return f'{path} doesn\'t exist'
    if text and not contains(full, text):
        return f'"{text}" isn\'t in {path}'
    return None


def check_ref(ref):
    kind, sep, spec = ref.partition(':')
    if not sep or kind not in KINDS:
        return f'"{ref}": unknown kind (use {", ".join(sorted(KINDS))})'
    if kind == 'board':
        return None if spec in BOARD else f'"{ref}": no {spec} in doc/admin/board-tests.md'
    err = check_path_ref(spec)
    return f'"{ref}": {err}' if err else None


def check_doc_ref(spec):
    """PATH or PATH#heading text; the heading must exist"""
    path, _, head = spec.partition('#')
    full = os.path.join(ROOT, path)
    if not os.path.exists(full):
        return f'{path} doesn\'t exist'
    if head:
        heads = [line.lstrip('#').strip() for line in open(full, encoding='utf-8')
                 if line.startswith('#')]
        if not any(head.lower() in h.lower() for h in heads):
            return f'no heading "{head}" in {path}'
    return None


# Rules -----------------------------------------------------------------

def analyse(items):
    errors, gaps = [], []
    for it in items.values():
        if it.status not in STATUSES:
            errors.append(f'{it.where()}: {it.id}: status "{it.status}" (use '
                          f'{", ".join(sorted(STATUSES))})')
        if not it.text:
            errors.append(f'{it.where()}: {it.id} has no statement')
        parents = it.refs('Parent')
        if it.level == 'UN':
            if parents:
                errors.append(f'{it.where()}: {it.id}: a need has no parent')
            parents = []
        elif not parents:
            errors.append(f'{it.where()}: {it.id} has no parent')
        for p in parents:
            if p not in items:
                errors.append(f'{it.where()}: {it.id}: parent {p} doesn\'t exist')
            elif items[p].level != PARENT_LEVEL[it.level]:
                errors.append(f'{it.where()}: {it.id}: parent {p} is a {items[p].level}, '
                              f'not a {PARENT_LEVEL[it.level]}')
            else:
                items[p].children.append(it.id)
        for ref in it.refs('Verified by'):
            err = check_ref(ref)
            if err:
                errors.append(f'{it.where()}: {it.id}: {err}')
        for field in ('Code', 'Design'):
            for ref in it.refs(field):
                err = check_path_ref(ref) if field == 'Code' else check_doc_ref(ref)
                if err:
                    errors.append(f'{it.where()}: {it.id}: {field}: {err}')
        if it.level == 'MOD' and not it.refs('Code'):
            errors.append(f'{it.where()}: {it.id}: a module names its code')
    for it in items.values():
        kinds = {r.partition(':')[0] for r in it.refs('Verified by')}
        if it.status == 'planned':
            gaps.append((it.id, 'planned: not built yet'))
            continue
        if not kinds & COUNTS[it.level]:
            gaps.append((it.id, f'no {"/".join(sorted(COUNTS[it.level]))} evidence for a '
                                f'{LEVEL_NAME[it.level].lower()}'))
        elif it.status == 'partial':
            gaps.append((it.id, 'partial: ' + (it.fields.get('Gap') or 'see its text')))
        if it.level != 'MOD' and not it.children:
            below = {'UN': 'system requirement', 'SYS': 'architecture element',
                     'ARC': 'module'}[it.level]
            gaps.append((it.id, f'no {below} traces to it'))
    return errors, gaps


# The matrix ------------------------------------------------------------

def link(ident, items):
    it = items[ident]
    return f'[{ident}]({os.path.basename(it.path)}#{anchor(it)})'


def anchor(it):
    t = f'{it.id}: {it.title}'.lower()
    t = re.sub(r'[^\w\- ]', '', t)
    return t.replace(' ', '-')


def fmt_refs(refs):
    out = []
    for r in refs:
        kind, _, spec = r.partition(':')
        if kind == 'board':
            out.append(f'{spec}')
        else:
            out.append(f'{kind}:`{spec}`')
    return '<br>'.join(out) if out else '-'


def suite_of(ref):
    kind, _, spec = ref.partition(':')
    if kind == 'board':
        return None
    path = spec.partition('::')[0]
    return path if '/' not in path and '.' not in path else None


def render(items, gaps):
    by = {lv: sorted((i for i in items.values() if i.level == lv), key=lambda i: i.id)
          for lv in LEVELS}
    gap_ids = {g[0] for g in gaps}
    L = ['# Requirements Traceability Matrix', '',
         'Written by `scripts/rtm.py` from [needs.md](needs.md), [system.md](system.md), '
         '[architecture.md](architecture.md) and [modules.md](modules.md); don\'t edit it '
         'by hand. [README.md](README.md) explains the V-model, the levels and the kinds '
         'of evidence. `make test` fails if this file is out of date or a link in it '
         'is broken.', '']
    L += ['## Summary', '', '| Level | Items | Verified | Partial | Planned | With gaps |',
          '|---|---|---|---|---|---|']
    for lv in LEVELS:
        xs = by[lv]
        n = {s: sum(1 for i in xs if i.status == s) for s in STATUSES}
        L.append(f'| {LEVEL_NAME[lv]}s ({lv}) | {len(xs)} | {n["verified"]} | {n["partial"]} | '
                 f'{n["planned"]} | {sum(1 for i in xs if i.id in gap_ids)} |')
    L.append('')

    L += ['## Needs to system requirements', '',
          'Each stakeholder need, how it\'s validated, and the system requirements that '
          'refine it.', '', '| Need | Validated by | System requirements | Status |',
          '|---|---|---|---|']
    for u in by['UN']:
        kids = ', '.join(link(c, items) for c in sorted(u.children)) or '**none**'
        L.append(f'| {link(u.id, items)} {u.title} | {fmt_refs(u.refs("Verified by"))} | '
                 f'{kids} | {u.status} |')
    L.append('')

    L += ['## System requirements', '',
          'Left side: the need above, the architecture below. Right side: the system and '
          'integration tests (and board tests) that verify it.', '',
          '| Requirement | Need | Architecture | Verified by | Status |',
          '|---|---|---|---|---|']
    for s in by['SYS']:
        L.append(f'| {link(s.id, items)} {s.title} | '
                 f'{", ".join(link(p, items) for p in s.refs("Parent"))} | '
                 f'{", ".join(link(c, items) for c in sorted(s.children)) or "**none**"} | '
                 f'{fmt_refs(s.refs("Verified by"))} | {s.status} |')
    L.append('')

    L += ['## Architecture', '', '| Element | Satisfies | Modules | Integration evidence | Status |',
          '|---|---|---|---|---|']
    for a in by['ARC']:
        L.append(f'| {link(a.id, items)} {a.title} | '
                 f'{", ".join(link(p, items) for p in a.refs("Parent"))} | '
                 f'{", ".join(link(c, items) for c in sorted(a.children)) or "**none**"} | '
                 f'{fmt_refs(a.refs("Verified by"))} | {a.status} |')
    L.append('')

    L += ['## Modules', '', '| Module | Element | Code | Design | Unit tests | Status |',
          '|---|---|---|---|---|---|']
    for m in by['MOD']:
        code = '<br>'.join(f'`{c}`' for c in m.refs('Code'))
        design = '<br>'.join(f'`{d}`' for d in m.refs('Design')) or '-'
        L.append(f'| {link(m.id, items)} {m.title} | '
                 f'{", ".join(link(p, items) for p in m.refs("Parent"))} | {code} | {design} | '
                 f'{fmt_refs(m.refs("Verified by"))} | {m.status} |')
    L.append('')

    # Reverse: evidence to items
    suites = sorted(d for d in os.listdir(SUITES)
                    if os.path.isdir(os.path.join(SUITES, d)) and d != 'unity')
    uses = {}
    boards = {}
    for it in items.values():
        for r in it.refs('Verified by'):
            s = suite_of(r)
            if s:
                uses.setdefault(s, set()).add(it.id)
            if r.startswith('board:'):
                boards.setdefault(r[6:], set()).add(it.id)
    L += ['## Test suites to items', '',
          'Every suite under `src/target/desktop/unit_tests/` (run by `make test`) and '
          'what it verifies. A suite nothing names is listed as a gap.', '',
          '| Suite | Verifies |', '|---|---|']
    for s in suites:
        ids = ', '.join(link(i, items) for i in sorted(uses.get(s, ()))) or '**nothing traced**'
        L.append(f'| `{s}` | {ids} |')
    L.append('')
    L += ['## Board tests to items', '',
          'Every entry of [board-tests.md](../admin/board-tests.md), the hardware '
          'evidence, and what it verifies or validates.', '', '| Entry | Verifies |', '|---|---|']
    for b in sorted(BOARD, key=lambda x: (int(re.match(r'\d+', x[1:]).group()), x)):
        ids = ', '.join(link(i, items) for i in sorted(boards.get(b, ()))) or '**nothing traced**'
        L.append(f'| {b}. {BOARD[b]} | {ids} |')
    L.append('')

    L += ['## Gaps', '',
          "What isn't verified at its level yet, or isn't traced. Planned items are "
          "here until they're built.", '']
    for ident, why in sorted(gaps):
        L.append(f'- {link(ident, items)}: {why}')
    for s in suites:
        if s not in uses:
            L.append(f'- test suite `{s}`: traces to no item')
    for b in BOARD:
        if b not in boards:
            L.append(f'- board test {b}: traces to no item')
    L.append('')
    return '\n'.join(L)


def main(argv=None):
    p = argparse.ArgumentParser(description='The Requirements Traceability Matrix')
    p.add_argument('--check', action='store_true')
    p.add_argument('--strict', action='store_true')
    p.add_argument('--gaps', action='store_true')
    p.add_argument('--dir', default=QDIR, help='where the item files are (tests)')
    p.add_argument('--out', default=OUT, help='the matrix to write or check (tests)')
    a = p.parse_args(argv)
    items, errors = parse(a.dir)
    more, gaps = analyse(items)
    errors += more
    for e in errors:
        print(f'rtm: {e}', file=sys.stderr)
    if a.gaps:
        for ident, why in sorted(gaps):
            print(f'{ident}: {why}')
        return 1 if errors else 0
    text = render(items, gaps) if not errors else None
    if errors:
        print(f'rtm: {len(errors)} problem(s)', file=sys.stderr)
        return 1
    if a.check or a.strict:
        try:
            old = open(a.out, encoding='utf-8').read()
        except OSError:
            old = None
        if old != text:
            print('rtm: doc/quality/rtm.md is out of date: run scripts/rtm.py', file=sys.stderr)
            return 1
        if a.strict and gaps:
            print(f'rtm: {len(gaps)} gap(s) (rtm.py --gaps)', file=sys.stderr)
            return 1
        n = len(items)
        print(f'rtm: {n} items, {len(gaps)} gap(s), every link checks out')
        return 0
    with open(a.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print(f'rtm: wrote {os.path.relpath(a.out, ROOT)}: {len(items)} items, {len(gaps)} gap(s)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
