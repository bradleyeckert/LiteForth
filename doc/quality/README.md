# Quality: the V-model and the traceability matrix

LiteForth on the CH32H417 is security-relevant firmware: the evidence for
a cybersecurity technical file (the EU Cyber Resilience Act, IEC 62443-4-1;
[security-plan.md](../admin/security-plan.md) section 2) has to show what
the product must do, how it's built to do it, and how each part was
checked. This folder organizes development as a V-model and keeps a
Requirements Traceability Matrix (RTM) that a script checks against the
repository at every `make test`.

## 1. The V

```
  Stakeholder needs (UN) ............................ Validation
  needs.md                                            board tests on a real board,
     \                                                demonstrations
      System requirements (SYS) .................. System tests
      system.md                                    lf on the desktop, the simulated
         \                                         board, board tests
          Architecture (ARC) .................. Integration tests
          architecture.md                       components together: stage1sim,
             \                                  blocksim, datalog, sealed, ...
              Modules (MOD) ................ Unit tests
              modules.md                     one module: unit_tests/<suite>
                 \                          /
                  Implementation: src/, forth/, scripts/
```

Down the left side, each level refines the one above and names it as its
**Parent**. Across the V, each item names the evidence that verifies it at
its own level (**Verified by**). Up the right side, the tests run from the
bottom: unit tests first, then integration, system, and validation on the
board.

| Level | What it says | Where | Verified at its level by |
|---|---|---|---|
| UN, stakeholder need | What someone needs from the product, in their terms | [needs.md](needs.md) | `board:` or `demo:` (validation) |
| SYS, system requirement | What the product as a whole must do, testable | [system.md](system.md) | `sys:`, `integ:` or `board:`; `insp:` or `ci:` where a test can't show it |
| ARC, architecture element | A component: what it's responsible for and its interfaces | [architecture.md](architecture.md) | `integ:`, `sys:` or `board:`; `insp:` or `ci:` where a test can't show it |
| MOD, module | The code that implements part of a component, and its design | [modules.md](modules.md) | `unit:` |

The detailed design of each module is the document already beside the
code (the module's **Design** links): `doc/admin/firmware-updates.md`,
`doc/admin/data-logs.md`, `doc/exfat.md`, the header comments, and so on.
This folder doesn't repeat them; it ties them together.

## 2. The items

Every item is a heading and a few fields:

```
### SYS-UPD-03: Updates install on trial and roll back
A new image starts on trial and must confirm within N boots, or ...

- **Parent:** UN-07
- **Verified by:** unit:update::test_rollback, integ:stage1sim::s_rollback, board:B4
- **Status:** verified
```

- **IDs** never change and are never reused. `UN-nn`; `SYS-AREA-nn` (areas
  below); `ARC-NAME`; `MOD-NAME`. A retired item stays, with status
  `planned` and a line saying it was dropped, or is deleted and its ID
  isn't used again.
- **Parent:** the items one level up that this one refines (UN items have
  none). Comma-separated.
- **Verified by:** the evidence, comma-separated, each `kind:target`:

| Kind | Level | Target | Checked by `rtm.py` |
|---|---|---|---|
| `unit:` | unit test | a suite (a directory of `src/target/desktop/unit_tests/`) or a path, optionally `::text` | the suite or path exists, and the text occurs in it (a test function, a check's message) |
| `integ:` | integration test | as `unit:` | as `unit:` |
| `sys:` | system test (the whole `lf`, or the simulated board) | as `unit:` | as `unit:` |
| `board:` | system test or validation on a real board | a [board-tests.md](../admin/board-tests.md) entry, `B25` | the entry exists |
| `demo:` | validation by demonstration | a path (a script, a document of the demo) `::text` | as `unit:` |
| `insp:` | inspection or analysis | a path `::text` (the document or code inspected) | as `unit:` |
| `ci:` | a check CI runs on every PR | a path `::text` (the workflow, the checker) | as `unit:` |

  The `::text` should name the actual check: `unit:update::test_signed`,
  not just `unit:update`. Where a suite has no named cases (the
  script-driven ones), the text is a line of its expected output.
- **Status:** `verified` (the evidence exists and passes in `make test`
  or was run on the board and logged), `partial` (some of it is verified:
  a **Gap:** field says what isn't), or `planned` (not built yet: the
  requirement is agreed, so the design and tests can be made against it).
- **Code** and **Design** (modules only): the source files and the design
  documents (a heading after `#` is checked).

System requirement areas: `LANG` the Forth system, `VM` the virtual
machine, `STO` storage, `TERM` the terminal, `UPD` firmware updates,
`SEC` protection, `LOG` data logs, `DISP` the display, `TIME` time and
power, `TOOL` the PC tools, `QA` the development process itself.

## 3. The matrix

`scripts/rtm.py` reads the four files and writes [rtm.md](rtm.md):

- needs to system requirements, with how each need is validated;
- system requirements, with their architecture and verification;
- architecture elements and modules, with code, design and tests;
- the reverse view: every test suite and every board-test entry, and
  what it verifies, so a test nobody traces is visible;
- the gaps: items not verified at their level, items nothing below traces
  to, partial and planned items, untraced suites and board tests.

It fails (`--check`, which `unit_tests/rtm` runs in `make test` and so in
CI) on:

- a broken link: a suite, path, text, board-test entry or heading that
  doesn't exist;
- an unknown or wrong-level parent, a duplicate ID, an item without a
  statement or status;
- an `rtm.md` that isn't what the script would write now.

Gaps don't fail `--check`; they're listed in `rtm.md` for the release
review. `--strict` fails on them too, and `--gaps` lists them.

```
python3 scripts/rtm.py            # rewrite rtm.md after changing an item
python3 scripts/rtm.py --gaps     # what isn't covered
python3 scripts/rtm.py --check    # what CI runs
```

## 4. The process

### A change

1. **Requirement first.** Find the items the change touches. A new
   capability gets its UN or SYS item before the code, with status
   `planned`. A change of behavior changes the item's text. If no
   requirement covers a change, either one is missing or the change isn't
   needed.
2. **Design.** The module's design document says how; an architecture
   change updates [architecture.md](architecture.md) and the design notes
   in `CLAUDE.md`.
3. **Code and tests together**, from the bottom of the V: unit tests for
   the module, then integration or system tests for the requirement.
   Every test names the behavior it checks, so the item can point at it
   (`::test_signed`).
4. **Trace.** Add the evidence to the items' **Verified by**, set their
   **Status**, run `scripts/rtm.py`, and commit `rtm.md` with the change.
5. **Board.** A change to what runs on the CH32H417 is checked with
   `scripts/ch32build.sh` before it goes to the board; a change to
   security, updates or storage on the board gets a run logged in
   [board-tests.md](../admin/board-tests.md) (hashes, steps, expected,
   output word for word) and referenced as `board:Bnn`.
6. **Review.** The pull request lists the items it touches (the template
   asks). A change to a security mechanism (keys, the update path, the
   PMP, the boot check, a cryptographic format) gets an independent
   review: someone, or a separate agent, who reads the code without the
   author's conclusions and looks for defects. Its findings are fixed with
   a test each, a test that fails without the fix.

### A bug

A failing test first, at the lowest level that shows it. Then ask why the
existing tests missed it: if a requirement was missing or vague, fix the
item too. If the bug got to a board, its fix gets a board run.

### A release (validation)

The right side's top: does the product meet the needs?

1. `make test` and CI pass; `rtm.py --check` passes.
2. `rtm.py --gaps` is reviewed: each gap is either closed or accepted for
   this release, and the accepted ones go in the release notes.
3. Every need (UN) that the release changes is validated on a board at
   the production level (`SECURITY_LEVEL` 2, locked down), logged in
   `board-tests.md`, and referenced from the need.
4. The SBOM is current (`unit_tests/sbom`), and the release's images,
   packages and their hashes are recorded.
5. The release is packaged and signed as
   [updates.md](../admin/updates.md) section 4 says.

## 5. Mapping to the standards

| Practice (IEC 62443-4-1) | Here |
|---|---|
| SR: security requirements | `SEC`, `UPD` and `LOG` system requirements, from the threat model in [security-plan.md](../admin/security-plan.md) section 4 |
| SD: secure by design | [architecture.md](architecture.md), the design documents the modules name, defense in depth: PMP, Guarded DMA, the boot check |
| SI: secure implementation | `CLAUDE.md`'s rules, warning-free builds, `mmodecheck.py`, coding rules, independent reviews |
| SVV: verification and validation | This V: unit, integration and system tests in `make test`, board tests, the RTM |
| SM-9, SM-10: third-party components | [sbom.md](../admin/sbom.md) and `unit_tests/sbom` |
| DM: defect management | The bug process above; issues and PRs on GitHub |

The CRA's technical documentation asks for the design, the risk
assessment, and the test reports: the design documents, the threat model,
and `rtm.md` with the evidence it links.
