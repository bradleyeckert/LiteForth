# CLAUDE.md

Notes for Claude on working in this repository. See README.md and doc/ for
the language itself.

## What this is

LiteForth is a Forth system for cheap 32-bit MCUs (target: CH32H417, also
STM32H743). Forth runs on a simulated 16-bit-instruction Forth CPU (`vm.c`);
the text interpreter, compiler and dictionary are in C. The desktop build
(`bin/lf`) is the development/test host: it talks over stdio or a serial port
and simulates flash and block storage with files in the working directory.

## Build and test

```
make            # builds bin/lf (gcc -Wall -Wextra -O2), incremental via build/*.d
make test       # scripts/primitives.f, then every src/target/desktop/unit_tests/*/ suite
make clean      # leaves the tracked bin/*.bin images alone
```

- Keep the build free of warnings. Clang (macOS) is stricter than gcc on some
  things, e.g. `FALLTHROUGH` must be followed by `;`.
- A new unit test suite is picked up automatically: add a directory under
  `src/target/desktop/unit_tests/` with a `makefile` that has a `test` target.
  Unity is vendored in `unit_tests/unity/` (don't edit it).
- `unit_tests/load/` is a script-driven test: `run.sh` builds a scratch
  `lfblocks.bin` in a temp dir with `dd`, runs `bin/lf -o 3`, and diffs the
  output (minus the banner line) against `expected.txt`. On failure it leaves
  `got.txt`. Use the same pattern for other interpreter-level tests.
- CI (`.github/workflows/c-cpp.yml`) runs `make` and `make test`, but only on
  pushes and PRs to `main`, so work on other branches is untested until a PR.

## Running lf by hand

```
printf '0 open-flash\n: foo 42 . ;\nfoo\nbye\n' | ./bin/lf -o 3
```

- `-o N` sets `g_lf_sys_options` (flags in `vm.h`): 1 no `ok>`, 2 no stack
  display, 4 quit on first error (validation), 8 no flash file, 16 no block
  file, 32 ignore CR, 64 verbose echo. `make test` uses `-o 31`; use `-o 3`
  when a test must keep going after errors.
- Compiling colon definitions needs `0 open-flash` first (as `scripts/go.f`
  does); without it `:` fails with ior -20.
- lf reads and creates `lfblocks.bin` / `lfflash.bin` in the current directory.
  Run experiments in the scratchpad, not the repo root, so the tracked images
  aren't modified.
- Block files: 4 KB blocks (not 1 KB), block n at byte offset n*4096, block 0
  holds the `LITEFORTHBLK ...` header. Only `SYSTEM_BLOCKS` (2) buffers exist,
  so any 3-deep nesting of loads evicts buffers.

## Source map

| File | Role |
|---|---|
| `src/forth.c/.h` | Text interpreter (`lfInterpret`, `interpretSource`), `QUIT`, parsing, the built-in dictionary table, `load`, `-->`, `.` etc. |
| `src/comp.c/.h` | Compiler: `lfCompileWord`, `lfExecuteWord`, literals, code slots |
| `src/vm.c/.h`, `vm_labels.h` | The simulated CPU (`vmRun`), registers, memory pages, sys option flags |
| `src/api0.c/.h` | `API0fns[]`: C functions callable from Forth via the API0 instruction |
| `src/tools.c/.h` | Optional tools (`see`, `dump`, disassembler, test words), gated by `FAT_FORTH` |
| `src/utils.c/.h` | Number output, error message table, misc helpers |
| `src/lfblocks.c/.h` | Block buffer management (LRU, `lfAssignBlock`) |
| `src/memalloc.c/.h` | LIFO memory pool |
| `src/errcodes.h` | Forth `ior` codes (standard negative throw codes) |
| `src/target/desktop/` | Host `main.c`, `options.h` (all tunables), file-backed flash and blocks, serial I/O |
| `src/target/STM32H743/` | MCU target code (not built by the makefile) |
| `scripts/go.f` | Boot code: defines the basic Forth lexicon on top of the primitives |
| `scripts/primitives.f` | Regression script run by `make test` |

## Conventions

- Every function prototype lives in the header of its own C file, with a doc
  comment. Keep docs accurate when behavior changes.
- Forth words implemented in C are `int lfAPI_name(void)`: they take and return
  values on the VM data stack (`vmPop`/`vmPush`) and return an `ior`
  (0 = ok, else a code from `errcodes.h`).
- Adding a C word: append the function to `API0fns[]` in `api0.c` and add a
  dictionary entry in `forth.c` (`{ LINK(n), "name", API0(index), ...}`). The
  `API0(index)` must equal the function's position in `API0fns[]`; don't
  reorder that table.
- Tunables (`MAX_LOAD_NESTING`, `SYSTEM_BLOCKS`, `SCREEN_COLUMNS`, ...) are in
  `src/target/desktop/options.h`.
- File encodings are mixed. Preserve them byte-for-byte when editing:
  `forth.c` and `go.f` are UTF-8 with a BOM, `forth.h` is Windows-1252 (en
  dashes in comments). Read/write with latin-1 in scripts, or use the Edit tool.
- `.gitattributes` forces `*.h` to C and marks Unity vendored for GitHub's
  language stats (some headers mention `__cplusplus`).

## Design notes

- **LOAD is recursive.** `lfInterpret` sets up the terminal source and calls
  `interpretSource`, which interprets until `>IN` reaches the end. `lfAPI_load`
  saves the caller's source, length, `>IN` and `BLK` in C locals, calls
  `interpretSource` on the block, then restores them and re-assigns the
  caller's block buffer (nested loads may have evicted it). Depth is limited by
  `MAX_LOAD_NESTING`. `-->` replaces the current block in place (tail call), so
  chains don't consume nesting.
- **Error traces** print as the error unwinds: each `load` prints its line
  (innermost first) and `lfInterpret` prints the `Terminal` line last.
- **vmRun is re-entrant when calling a word** (`vmRun(0, 0, addr)`): it saves
  `PC` and restores it after a normal return, so a word can call `load`, which
  runs more words. Single-instruction and step modes don't touch `PC`.
- The interpreter's token buffer is static on purpose (keeps recursion cheap
  on MCU stacks); a level never reads it after executing a word.

## Git notes

- Main branch is `main`; larger work goes on a branch (e.g. `cleanup`) and in
  via PR.
- A shallow clone only tracks `main`. To see other branches:
  `git ls-remote --heads origin`, then
  `git fetch origin <branch>:refs/remotes/origin/<branch>`.
