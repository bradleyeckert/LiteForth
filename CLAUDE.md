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
make test       # scripts/regression.f, then every src/target/desktop/unit_tests/*/ suite
make clean      # leaves the tracked bin/*.bin images alone
```

- The makefile has a cmd.exe branch (`WINCMD`, set when make runs on Windows
  without sh: `$(shell echo %OS%)` gives `Windows_NT`). It builds
  `bin/lf.exe` without `-pthread`, and its `test` runs only `regression.f`.
  Keep both branches in step when changing recipes; check that the Unix one
  is unchanged with `make -n` before and after. Use `make -n WINCMD=1` to see
  the cmd commands.
- Keep the build free of warnings. Clang (macOS) is stricter than gcc on some
  things, e.g. `FALLTHROUGH` must be followed by `;`.
- A new unit test suite is picked up automatically: add a directory under
  `src/target/desktop/unit_tests/` with a `makefile` that has a `test` target.
  Unity is vendored in `unit_tests/unity/` (don't edit it).
- `unit_tests/load/` and `unit_tests/quit/` are script-driven tests: `run.sh`
  runs `bin/lf -o 3` in a temp dir (load also builds a scratch `lfblocks.bin`
  with `dd`) and diffs the output, minus the banner line, against
  `expected.txt`. On failure it leaves `got.txt`. Use the same pattern for
  other interpreter-level tests.
- `unit_tests/api0/` checks the API 0 index order against `expected.txt`.
  After deliberately appending an API 0 function, run `make update` there.
- `scripts/regression.f` holds the VM and dictionary tests as `T{ ... -> ... }T`
  assertions. `make test` runs it with `-o 39` (validation, so the first
  failure stops lf and prints its line number; 32 = ignore CR, so Windows
  CRLF checkouts work and line numbers stay right) in a temp dir on copies of
  `bin/lfflash.bin` and `bin/lfblocks.bin`. It opens flash at the start,
  closes it at the end, and copies in the `go.f` definitions it tests.
- `STACK_CAPACITY` must be a power of 2, at least 32 (checked in `vm.h`).
- For refactors that shouldn't change behavior, save `build/*.o` and `bin/lf`
  first and `cmp` them afterwards; gcc output is reproducible here.
- CI (`.github/workflows/c-cpp.yml`) runs `make` and `make test`, but only on
  pushes and PRs to `main`, so work on other branches is untested until a PR.

## Running lf by hand

```
printf '0 open-flash\n: foo 42 . ;\nfoo\nbye\n' | ./bin/lf -o 3
```

- `-o N` sets `g_lf_sys_options` (flags in `vm.h`): 1 no `ok>`, 2 no stack
  display, 4 quit on first error (validation), 8 boot from flash, 16 app
  running (set by `cold`), 32 ignore CR, 64 verbose echo, 256 boot without
  starting the app (`cold` starts it). Without 32, CRLF input counts every line twice (the CR ends a line
  and the LF makes an empty one), so reported line numbers double. There are no options to skip the flash or block files: lf always
  loads or creates them. `-f file` and `-k file` name the flash and block
  files (defaults `FLASHFILENAME` and `BLOCKFILENAME` in `options.h`);
  `-t port` and `-b baud` select a serial port instead of stdio.
  On Windows, `-k F:` (a drive letter) uses the block partition of that
  removable disk through raw disk access (`rawdisk.c`, needs an
  administrator; layout in `doc/sdcard.md`). `unit_tests/rawdisk` tests the
  partition logic on image files, since the container can't run Windows;
  `rawdisk.c`'s Windows half can only be syntax-checked here (clang
  `-target x86_64-w64-mingw32` with mingw-w64 headers). Use `-o 3` when a test must keep going after errors.
- Compiling colon definitions needs `0 open-flash` first (as `scripts/go.f`
  does); flash is write-protected otherwise and `:` fails with ior -20.
- `open-flash` maps the flash page to a RAM buffer from the memory pool.
  `close-flash` programs the page and frees the buffer; nothing needs
  relocating, because headers in VM memory hold VM values (see the header
  links note below). Without
  a matching `close-flash`, lf exits with code 196 (pool_free fails in main).
- lf reads (or, if missing, creates) `lfblocks.bin` / `lfflash.bin` in the
  current directory, unless `-k` / `-f` name other files. The repo tracks both in `bin/` only; keep them, never
  delete or regenerate them. Running lf in the repo root creates untracked
  copies there, so don't. `lfflash.bin` mimics an MCU's
  flash memory; `lfblocks.bin` will hold source code and other data, and
  code to write flash to blocks is planned. `make test` uses copies of the
  `bin/` images. Run experiments in the scratchpad, not the repo, so the
  tracked images aren't modified.
- Block files: 4 KB blocks (not 1 KB), block n at byte offset n*4096. Block 0
  must start with the signature `LITEFORTH` (9 bytes); without it a block file
  (or SD partition) reports capacity 1, block 0 is readable and nothing is
  writable, and the file is never modified. That's the only block 0 metadata
  for now (`unit_tests/blocks/` checks it). Only `SYSTEM_BLOCKS` (2) buffers exist,
  so any 3-deep nesting of loads evicts buffers.

## Source map

| File | Role |
|---|---|
| `src/forth.c/.h` | Text interpreter (`lfInterpret`, `interpretSource`), `QUIT`, parsing, the built-in dictionary table, `load`, `-->`, `.` etc. |
| `src/comp.c/.h` | Compiler: `lfCompileWord`, `lfExecuteWord`, literals, code slots |
| `src/vm.c/.h`, `vm_labels.h` | The simulated CPU (`vmRun`), registers, memory pages, sys option flags |
| `src/api0_list.h` | The API 0 function list (X-macro): single source of API 0 indices |
| `src/api0.c/.h` | API 0 index enum and `API0fns[]`, both generated from `api0_list.h` |
| `src/tools.c/.h` | Optional tools (`see`, `dump`, disassembler, test words), gated by `FAT_FORTH` |
| `src/utils.c/.h` | Number output, error message table, misc helpers |
| `src/lfblocks.c/.h` | Block buffer management (LRU, `lfAssignBlock`) |
| `src/memalloc.c/.h` | LIFO memory pool |
| `src/errcodes.h` | Forth `ior` codes (standard negative throw codes) |
| `src/target/desktop/` | Host `main.c`, `options.h` (all tunables), file-backed flash and blocks, serial I/O |
| `src/target/STM32H743/` | MCU target code (not built by the makefile) |
| `src/target/CH32H417/` | MCU target, MounRiver projects (not built by the makefile): the V3F core runs USB CDC and bridges it through shared-SRAM rings (`Common/cdc_bridge.c`, `cdc_shared.h`) to the V5F, whose `serial_io.c`, `flash.c` (256K at 0x08030000, reserved in `SRC/Ld/V5F/Link_v5f.ld`), `blocks.c` (a raw type-DA partition on the microSD card via WCH's `sdio.c`; see doc/sdcard.md), `options.h` (4 x 64K flash pages, RAM_PAGE 4), `lftime.c` and `main.c` (runs `lfQuit`) are in `V5F/User/`; `lf_*.c` there each `#include` one `src/*.c`, so a new core source file needs a new wrapper. Ctrl+X three times on the terminal makes the V3F restart the V5F without starting the app (HSEM1, `SYS_OPTION_NO_AUTORUN`). See its README |
| `scripts/go.f` | Boot code: defines the basic Forth lexicon on top of the primitives |
| `scripts/regression.f` | Regression script run by `make test` |

## Conventions

- Every function prototype lives in the header of its own C file, with a doc
  comment. Keep docs accurate when behavior changes.
- Forth words implemented in C are `int lfAPI_name(void)`: they take and return
  values on the VM data stack (`vmPop`/`vmPush`) and return an `ior`
  (0 = ok, else a code from `errcodes.h`).
- Adding a C word (full walkthrough in `doc/add2api.md`): append `X(API_NAME, function)` to the end of `API0_LIST`
  in `src/api0_list.h` (or `API0_TOOLS_LIST` for `FAT_FORTH` tools), add a
  row `{ PREV, "name", API0(API_NAME), /* stack */ flags},` to `forth_heads`
  in `forth.c`, and run `make update` in `unit_tests/api0`. API 0 indices are
  compiled into code saved in flash, so never reorder, remove or insert
  entries in the middle of the list.
- Dictionary rows link to the previous row with `PREV` (counted with
  `__COUNTER__` from `HEADS_BEGIN(table)`); never write link numbers by hand,
  and don't use `__COUNTER__` inside a header table.
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
- **vmExec keeps the VM registers in locals** (`VM_LOAD`/`VM_SAVE`) so the
  compiler can hold them in CPU registers; save around anything that reads
  or changes the globals (API calls) and at every return. Each micro-op does
  its own stack effect. Fetch reads the cell every time (no pair cache) and
  one range check, `pc - cbase < cspan`, covers the page, the executable
  limit and the `0xDEADC0DE` terminator; set `cspan = 0` after anything that
  may remap memory. Micro-op slots are shifted out of `slots`, so trailing
  nops cost nothing. The switches cover every value with an unreachable
  default so the jump tables have no range check. Judge speed changes by
  RV32 instruction counts (clang `--target=riscv32-unknown-elf`), not by
  desktop timings, which are noisy.
- **vmRun is re-entrant when calling a word** (`vmRun(0, 0, addr)`): it saves
  `PC` and restores it after a normal return, so a word can call `load`, which
  runs more words. Single-instruction and step modes don't touch `PC`.
- **QUIT** is a loop over small helpers (`quitReset`, `prompt`,
  `interpretLine`, `checkStackDepth`, `reportError`). After each line it
  checks `sp`, which wraps: upper 4 bits all set means underflow (120-127
  for a 128-cell stack), upper 3 set means overflow (112-119), so a line may
  leave at most 111 items.
  A line too long for TIB is not interpreted at all.
- VM arithmetic: `cy` is the carry out of bit 31 of `+`, or the bit shifted
  out by `2/c` (which rotates right through carry). `2/` is an arithmetic
  shift. `+*` is a multiply step: T:A shifts right, adding N when A is odd.
- `n for ... next` runs n times (R = n..1); `0 for` runs 2^32 times.
- `does>` works right after `create`, not inside a defining word:
  `create arr 3 allot does> ] + exit [` (end with `exit [`, not `;`).
- **The app runs while the terminal is idle.** `cold` resets the VM and sets
  `SYS_OPTION_RUNNING`; `loadTIB` then runs the VM in step mode from its PC
  whenever no input is waiting, until the app executes `break`
  (`ERR_VM_BREAK`). An error or `yeet` in the app (`vmExec` with `steps` nonzero)
  sets X = PC, Y = ior, PC = the yeet handler at cell `VM_YEET_ADDRESS` (2),
  and the app keeps running; it handles its own errors, as on a Forth chip.
  Errors in words the terminal runs (word-call mode) only flow back through
  `interpretSource` to `lfQuit`; they never touch the app's PC. The exception is
  `ERR_VM_TIMEOUT` (`VM_STEP_LIMIT` steps without a `break`): the app is
  stopped and `loadTIB(int* length)` returns it for QUIT to report; otherwise
  `loadTIB` returns 0 or `ERR_TIB_OVERFLOW`. `go.f` puts the app's entry jump at code
  cell 0 and the handler's at cell 2 with `,jump`.
  `serial_ready` never blocks: in stdio mode a reader thread (`serial_io.c`,
  pthreads or Win32) reads fd 0 into a ring buffer, so the app runs while a
  line is being typed. It reads the fd, not the `stdin` stream, because a
  thread blocked in `fgetc` holds the stream lock and `exit()` would hang.
  With piped input the app mostly runs after EOF, when there's nothing to
  read. To test on a tty, use a pty (python `pty.fork`); if you wrap lf in
  `timeout`, use `--foreground` or lf can't read the tty.
- **Header links are tagged** (doc on `s_head` in `forth.h`): a link or
  `s_wid.head` is 0 (end), `(a << 1) | 1` (header in VM memory at cell
  address a), `(n << 2) | 2` (the built-in list of wordlist n), or else a C
  pointer (only inside the built-in tables). A VM header's `name` is the
  name's VM byte address. Walk lists with `lfFollow` (forth.c), never
  `->link`/`->name` directly. So flash holds no C addresses: no rebase at
  `close-flash`, and the image boots wherever it's mapped.
- **Booting** (`-o 8`, `SYS_OPTION_BOOTING`): `go.f` ends with
  `_text here 32 bit 1 !` then `save-wids save-idata`, so cell 1 points to a
  record (skip address, wordlist count, raw `wids` table; offsets
  `WIDS_RECORD_*` in `forth.h`), followed by the saved IDATA.
  At startup `lfQuit` calls `lfBootFromFlash` to restore `wids`, then resets
  the VM and sets `SYS_OPTION_RUNNING`. A head that isn't a valid link
  (e.g. a C pointer in an image from an older build) becomes NULL.
- **Multitasker** (`go.f`, `doc/multitasking.md`): user areas of 3 cells
  (NEXT, ACTION, R:D). `pause` jumps to the running task's ACTION: `awake`
  saves its `rp:sp` and switches to NEXT with `]task`; `asleep` only moves U.
  `multitask` makes a ring of one, the terminal (`operator`). `task[` (`sys>`)
  pushes T and R and gives `rp:sp`; `]task` is a `sys` instruction (not
  `>sys`): it sets `sp`/`rp` from T and doesn't pop. `regression.f` carries a
  copy of these words.
- **`label`** declares a forward reference: a header with `A_UNRESOLVED` and
  two code slots holding an invalid opcode (`VMI_INVALID`, so an early call
  from code fails with -105). `:` checks for an unresolved label of that name first
  (`lfParseLabel`, which puts `>IN` back if there is none). If it finds one,
  it stores a jump to the new code in the slots (`storeJump`, shared with
  `does>`) and sets `noname` so `;` doesn't toggle `A_SMUDGED` on `latest`.
  `lfSearchContext(name, flags, &name)` finds both, and `lfQuit` uses it at
  `bye` to report a label left unresolved (-119).
- The interpreter's token buffer is static on purpose (keeps recursion cheap
  on MCU stacks); a level never reads it after executing a word.

## Git notes

- Main branch is `main`; larger work goes on a branch (e.g. `cleanup`) and in
  via PR.
- A shallow clone only tracks `main`. To see other branches:
  `git ls-remote --heads origin`, then
  `git fetch origin <branch>:refs/remotes/origin/<branch>`.
