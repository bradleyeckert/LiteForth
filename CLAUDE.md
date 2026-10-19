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
  runs `bin/lf -o 3` in a temp dir (load also builds a scratch `lfblocks.fb4`
  with `dd`) and diffs the output, minus the banner line, against
  `expected.txt`. On failure it leaves `got.txt`. Use the same pattern for
  other interpreter-level tests.
- `unit_tests/vmasm/` runs random VM code through vm.c's C `vmExec`
  (native) and through `vm_rv32.S` (built with clang for RV32 and run in its
  own small RV32IMC emulator, `rv32emu.c`) and compares the states, with one
  and two TOS registers. It skips itself without clang's riscv32 target and
  `ld.lld`. `make bench` there prints RV32 instructions per VM instruction.
  Any change to `vmExec` needs the same change in `vm_rv32.S`.
- `unit_tests/rtc/` tests the RTC words on the desktop, where the clock is
  the host's plus an offset and `standby` sleeps until the alarm, then
  restarts lf in-process (`lfReset` in main.c: `longjmp` back to before
  the memory map, with the starting options). It takes about 3 s.
- `unit_tests/api0/` checks the API 0 index order against `expected.txt`.
  After deliberately appending an API 0 function, run `make update` there.
- `scripts/regression.f` holds the VM and dictionary tests as `T{ ... -> ... }T`
  assertions. `make test` runs it with `-o 39` (validation, so the first
  failure stops lf and prints its line number; 32 = ignore CR, so Windows
  CRLF checkouts work and line numbers stay right) in a temp dir on copies of
  `bin/lfflash.bin` and `bin/lfblocks.fb4`. It opens flash at the start,
  closes it at the end, and copies in the `core.f` definitions it tests.
- `STACK_CAPACITY` must be a power of 2, at least 32 (checked in `vm.h`).
- For refactors that shouldn't change behavior, save `build/*.o` and `bin/lf`
  first and `cmp` them afterwards; gcc output is reproducible here.
- Security tests on the board (updates, rollback, PMP, protection) are
  logged in `doc/board-tests.md`, as evidence for a cybersecurity document:
  firmware build and hashes, packages' sha256, steps, expected, and the
  board's output word for word. Add an entry for every such run.
- CI (`.github/workflows/c-cpp.yml`) runs `make` and `make test`, but only on
  pushes and PRs to `main`, so work on other branches is untested until a PR.

## Running lf by hand

```
printf '0 open-flash\n: foo 42 . ;\nfoo\nbye\n' | ./bin/lf -o 3
```

- `-o N` sets `g_lf_sys_options` (flags in `vm.h`): 1 no `ok>`, 2 no stack
  display, 4 quit on first error (validation), 8 boot from flash, 16 app
  running (set by `cold`), 32 ignore CR, 64 verbose echo, 256 boot without
  starting the app (`cold` starts it), 512 `.` prints plain hex, not
  size:shift:cell slice addresses (`SYS_OPTION_NOT_SLICE`, `lfDot` in tools.c). The variable `stop-tib` (`LF_DEADTIB`,
  a bit of the packed state cell) gives a running app the keyboard: `loadTIB`
  runs it even with input waiting and reads nothing itself until the app
  stops (`unit_tests/deadtib`). Without 32, CRLF input counts every line twice (the CR ends a line
  and the LF makes an empty one), so reported line numbers double. There are no options to skip the flash or block files: lf always
  loads or creates them. `-f file` and `-k file` name the flash and block
  files (defaults `FLASHFILENAME` and `BLOCKFILENAME` in `options.h`);
  `-t port` and `-b baud` select a serial port instead of stdio.
  `-k` also takes a card image (an unsigned file holding an MBR, or none,
  and an exFAT volume with `lfblocks.fb4` in its root): blocks.c then maps
  the blocks onto that file as the board does (`src/card.c`), and the
  sector functions (`blk_sector_*`) reach the whole image, with writes
  fenced to the exFAT volume. `scripts/mkexfat.py` builds such images;
  `unit_tests/exfat` uses them. `-K board_file` gives lf a board's keys
  for sealed block files (`doc/encrypted-blocks.md`; the file comes from
  `sealblocks.py lf-board`, format in `board.h`): a block file or card
  whose block 0 starts with `LITEFORTHS` is mounted read-only through
  `src/sealed.c`, or offers capacity 0 without `-K` or if refused; stderr
  says which. A board file with `sealed-only 1` (`lf-board --sealed-only`,
  like a `SEALED_ONLY` build) refuses plain files the same way. Use `-o 3` when a test must keep going after errors.
- Compiling colon definitions needs `0 open-flash` first (as `forth/core.f`
  does); flash is write-protected otherwise and `:` fails with ior -20.
- `open-flash` maps the flash page to a RAM buffer from the memory pool.
  `close-flash` programs the page and frees the buffer; nothing needs
  relocating, because headers in VM memory hold VM values (see the header
  links note below). Without
  a matching `close-flash`, lf exits with code 196 (pool_free fails in main).
- lf reads (or, if missing, creates) `lfblocks.fb4` / `lfflash.bin` in the
  current directory, unless `-k` / `-f` name other files. The repo tracks both in `bin/` only; keep them, never
  delete or regenerate them. Running lf in the repo root creates untracked
  copies there, so don't. `lfflash.bin` mimics an MCU's
  flash memory; `lfblocks.fb4` will hold source code and other data, and
  code to write flash to blocks is planned. `make test` uses copies of the
  `bin/` images. Run experiments in the scratchpad, not the repo, so the
  tracked images aren't modified.
- Block files: 4 KB blocks (not 1 KB), block n at byte offset n*4096. Block 0
  must start with the signature `LITEFORTH` (9 bytes); without it a block file
  (or the card's `lfblocks.fb4`) reports capacity 1, block 0 is readable and nothing is
  writable, and the file is never modified. That's the only block 0 metadata
  for now (`unit_tests/blocks/` checks it). Only `SYSTEM_BLOCKS` (2) buffers exist,
  so any 3-deep nesting of loads evicts buffers.
  `lfInterpret` ends every terminal line with `empty-buffers`, so an
  `update`d buffer that isn't saved before the line ends is discarded; the
  block editor (`core.f` blocks 18-20, `doc/editor.md`, `unit_tests/editor`)
  saves each change with `update save-buffers`.

## Source map

| File | Role |
|---|---|
| `src/forth.c/.h` | Text interpreter (`lfInterpret`, `interpretSource`), `QUIT`, parsing, the built-in dictionary table, `load`, `-->`, `.` etc. |
| `src/comp.c/.h` | Compiler: `lfCompileWord`, `lfExecuteWord`, literals, code slots |
| `src/vm.c/.h`, `vm_labels.h` | The simulated CPU (`vmRun`), registers (all in the struct `lf_vm`), memory pages, sys option flags |
| `src/vm_rv32.S` | `vmExec` in RV32IMC assembly (`vmExecAsm`), used instead of the C one when `VM_ASM` is defined (the CH32H417 does). Reaches `lf_vm` at the `VMS_*` offsets in `vm_labels.h`; `unit_tests/vmasm` compares it with the C version |
| `src/api0_list.h` | The API 0 function list (X-macro): single source of API 0 indices |
| `src/api0.c/.h` | API 0 index enum and `API0fns[]`, both generated from `api0_list.h` |
| `src/tools.c/.h` | Optional tools (`see`, `dump`, disassembler, test words), gated by `FAT_FORTH` |
| `src/utils.c/.h` | Number output, error message table, misc helpers |
| `src/lfblocks.c/.h` | Block buffer management (LRU, `lfAssignBlock`) |
| `src/memalloc.c/.h` | LIFO memory pool, 32-byte aligned: the RAM page is its first allocation, so the block buffers (`F_BLOCKBUFS`, a multiple of 8 cells) are aligned for the CH32H417's ECDC engine |
| `src/errcodes.h` | Forth `ior` codes (standard negative throw codes) |
| `src/exfat.c/.h` | A minimal exFAT reader (doc/exfat.md): `exfat_mount` finds the volume (MBR type 07, or at sector 0) and the allocation bitmap, `exfat_find` finds a name in a directory (NameHash, SetChecksum, ASCII case-insensitive) and checks contiguity. Reads only, through a sector-read callback |
| `src/card.c/.h` | `card_find_blocks`: the block space on a card is `lfblocks.fb4` in the exFAT root, if contiguous (capacity = ValidDataLength / 4096, writable with the signature; `space` is that size whatever block 0 says, for `-1 ymodem`). `card_find_file` finds another contiguous file in the root the same way: `lfstage.bin` (`CARD_STAGE_FILE`, for `-2 ymodem`; on the desktop with a plain block file, `STAGEFILENAME` in the current directory) and later `lfbackup.bin`. Shared by both targets' `blocks.c`; `unit_tests/exfat` tests both files on `mkexfat.py` images, plus `lf` itself (`run_lf.sh`) |
| `src/blake2s.c/.h` | BLAKE2s (RFC 7693), a copy of [bradleyeckert/blake2s](https://github.com/bradleyeckert/blake2s) (keep them the same): `b2s_init` (key, salt, personalization, matching Python's `hashlib.blake2s`), `b2s_putc`/`b2s_puts`, `b2s_final`; `b2s_puts` compresses whole 64-byte blocks straight from its input, and `-DBLAKE2S_UNROLL` unrolls the rounds (faster, bigger). For sealed block files (`doc/encrypted-blocks.md`); `unit_tests/blake2s` checks it against hashlib vectors |
| `src/sealed.c/.h` | Sealed block files (`doc/encrypted-blocks.md`): `sealed_mount` checks block 0 (fields, certificate, signature, minimums) against a `sealed_board` and unwraps the content key; `sealed_open_block` checks a block against its hash table entry and decrypts it; `sealed_mount_package` does the same for a firmware update package (`PACKAGE_MAGIC`) and returns its description (`sealed_package`); `sealed_check_package` also reads every block and checks the image hash (for `-2 ymodem`). Both targets' `blocks.c` call them (desktop: `lf -K`, `board.c`; `enroll` prints the board's `boards.csv` line, from the board file on the desktop and from the flash record on the CH32H417; with `INSECURE` in `sealed_root.h` the CH32H417's key is BLAKE2s of its UID, person `LFinsec1`, as `sealblocks.py board-key --insecure` computes); `unit_tests/sealed` drives `lf` with files from `sealblocks.py`. `unit_tests/blkrun` and `exfat` link it with its dependencies |
| `src/update.c/.h` | Stage 1's install logic for firmware updates (`doc/firmware-updates.md` sections 3, 4 and 6): `upd_boot` reads the security sector's record (two copies, even sequence numbers in A, odd in B), makes the device token on a blank one, carries on with any update under way (check, encrypted and MACed backup to `lfbackup.bin`, install block by block, trial, rollback) and checks the V5F image's MAC, or says recovery when there's no working image (none, a failed MAC, or a failed trial or rollback; `v5f_length` 0 until a package installs, without a backup); `upd_confirm` ends a trial; `upd_adopt` (development) takes the image in flash; `upd_mark_broken` marks it as not working. Reaches flash and the card's files only through `upd_io` callbacks; `sealed_mount_package` checks the package. The V3F runs it at reset (`V3F/User/stage1.c`). `unit_tests/update` tests it on simulated flash with packages from `gen.py`, including a power cut at every step |
| `src/ymrecv.c/.h` | A small YMODEM receiver for stage 1's recovery (`ymr_receive`): one file of whole 4 KB blocks, through callbacks, block 0 written last, refused (CAN CAN) if too big, not whole blocks or without its magic. Same protocol as `ymodem.c`; `unit_tests/ymrecv` drives it over pipes (`drv.c`) with a Python sender that reuses `unit_tests/ymodem/sender.py`'s packets |
| `unit_tests/stage1sim/` | The V3F's `stage1.c`, `cdc_bridge.c` and `lf_*.c` built unchanged on a simulated board: `sim.c` maps `flash.bin`, `shared.bin` and the UID at the chip's addresses (Linux, `MAP_FIXED_NOREPLACE`), gives a card (`card.img` from `mkexfat.py`), option bytes (`SIM_OBR`, `SIM_WPR`), USB over stdin/stdout through the real bridge (fake/ has the driver state), the debug UART on stderr, resets (exit 2) and power cuts (`SIM_CUT_AT`, exit 3), and a V5F model picked by its image (`SIMV5F:OK`, `SIMV5F:STUCK`, anything else never starts, so `stage1_wait_v5f` resets after 300 ms here; `SIM_SETUP_MS`; `reboot` resets). `test_stage1.py` plays every scenario (`--report`); `test_hil.py` runs `scripts/admin/hiltest.py` against it over ptys. When stage 1 uses a new chip function, add it to `fake/` |
| `src/ctr.c/.h` | AES-128 and SM4 in software and the sealed files' CTR mode (counter `nonce \|\| n*256 + i`); `unit_tests/ctr`. With `CTR_HW` (defined only by the V5F's `lf_ctr.c`), `ctr_crypt_block` hands whole aligned blocks to `ctr_hw_crypt` (`V5F/User/ecdc.c`: the ECDC engine, checked once against `ctr_crypt_block_sw`; a 32-byte aligned buffer, as the block buffers are, runs in place, any other through an aligned scratch copy; a failure in place makes `ctr_crypt_block` return -1 and `sealed_open_block` a read error; `ecdc-test` compares them on the board) |
| `src/monocypher*.c/.h` | [Monocypher](https://monocypher.org) 4.0.2 (CC0 or BSD-2, `src/monocypher-LICENCE.md`), changed only by one cast in Argon2 (marked `LiteForth:`) for MSVC's C4244, as upstream has since: X25519 and, from its optional `monocypher-ed25519.c`, Ed25519 with SHA-512 |
| `src/ymodem.c/.h` | `ymodem ( n -- )`: receives files by YMODEM (batch, CRC-16) over the terminal and writes them into blocks, exactly their length from the header: n = 0 source like `putblocks.py`, n = -1 a whole block file (`YMODEM_INSTALL`: block 0 checked by `blk_check_image` first, written last over an "unfinished" block 0, then read back, BLAKE2s compared, and `blk_remount`ed; into `blk_space`, the card file's size), n = -2 a firmware update package into `lfstage.bin` (`YMODEM_STAGE`: whole blocks starting with `PACKAGE_MAGIC`, block 0 written last, read back, then `blk_stage_check`, which runs `sealed_check_package` with the board's keys, and `blk_stage_ready`, which sets `update` in the CH32H417's shared block for stage 1; the block file is remounted after; `unit_tests/sealed` `stage_tests`), else binary from byte 32 of block n like `putbin.py`. Reports wait until the batch ends (a `C` in them would look like a request). `serial_binary(1)` turns off the CH32H417's Ctrl+X x3 escape meanwhile (`binary` in `cdc_shared.h`). `lfBlockBuffer` in lfblocks.c; `unit_tests/ymodem` drives it with a Python sender |
| `src/target/desktop/` | Host `main.c`, `options.h` (all tunables), file-backed flash and blocks, serial I/O, and `lcd.c`: a simulated graphic LCD (`lcd-init` with a 16-color palette, `lcd-tx`, `lcd-decode` for font pixel codes) with an ST7789 model behind a bus interface, shown in a Win32 or X11 window (the makefile links `-lgdi32`, or `-lX11` with `-DLCD_X11` when pkg-config finds x11; otherwise `lcd_init` gives -21). `unit_tests/lcd` tests the model headless (`LCD_HEADLESS`); `make demo` there opens the window. Controller 1 is an IPS panel (colors inverted until INVON), like the board's module. The V5F's `lcd.c` drives the nanoCH32H417's LCD connector (ST7789 on SPI2: PB13/PB15, CS PB12, D/C PD9, RES PD8) |
| `src/target/STM32H743/` | MCU target code (not built by the makefile) |
| `src/target/CH32H417/` | MCU target, MounRiver projects (not built by the makefile): the V3F core runs USB CDC and bridges it through shared-SRAM rings (`Common/cdc_bridge.c`, `cdc_shared.h`) to the V5F, whose `serial_io.c`, `flash.c` (256K at 0x08030000, reserved in `SRC/Ld/V5F/Link_v5f.ld`), `blocks.c` (`lfblocks.fb4` on the microSD card's exFAT volume, found by `src/card.c`; every transfer is a run of card sectors by DMA; see doc/sdcard.md; a sealed file is mounted at `blk_init` and its blocks checked and decrypted in `blk_status` as reads finish), `board.c` (the board's record for sealed files in the 8K sector at 0x08070000: X25519 key and minimums; `enroll`; the root key, product and optional `SEALED_ONLY` from the application's `sealed_root.h`, git-ignored, see `sealed_root.example.h`; `doc/updates.md` documents the update process and these options), `options.h` (4 x 64K flash pages, RAM_PAGE 4, `VM_IO_PAGE` 5: the peripheral registers at `IO_BASE`, mapped by `main.c`), `lftime.c` (microseconds, and the RTC: `rtc@ rtc! alarm! alarm? standby`; standby is Stop mode with both cores asleep, woken by the alarm, which then resets the chip and leaves `rtc_wake` in the shared block) and `main.c` (runs `lfQuit`) are in `V5F/User/`; `lf_*.c` there each `#include` one `src/*.c` (including `lf_exfat.c`, `lf_card.c`, `lf_blake2s.c`, `lf_ymodem.c`, `lf_sealed.c`, `lf_ctr.c`, `lf_monocypher.c`, `lf_monocypher_ed25519.c`), so a new core source file needs a new wrapper; `lf_vm_rv32.S` assembles `src/vm_rv32.S` when `options.h` defines `VM_ASM`. Ctrl+X three times on the terminal makes the V3F restart the V5F without starting the app (HSEM1, `SYS_OPTION_NO_AUTORUN`). The V3F's `V3F/User/stage1.c` is the first step of stage 1 (`doc/firmware-updates.md`): at reset, before waking the V5F, it runs `src/update.c` if `update` in the shared block says a package waits (`CDC_UPDATE_MAGIC`, set by `-2 ymodem`) or its security record exists (0x72000 and 0x74000, `stage1_layout.h`; the V5F image stays at 0x10000), with the card's `lfstage.bin` and `lfbackup.bin` through WCH's polling SD driver (`lf_sdio.c` includes the V5F's `sdio.c`) and the code flash through `FLASH_ROM_*`, fenced to the V5F slot and the record sectors; the V5F sets `CDC_CONFIRM_MAGIC` when LiteForth starts, which `stage1_poll` (from the USB loop) turns into `upd_confirm`. With no working V5F image (`STAGE1_RECOVERY`) `main.c` leaves the V5F off and `stage1_poll` runs a recovery terminal on the rings itself: `-2 ymodem` (`src/ymrecv.c`, into `lfstage.bin`, then `upd_boot` and a chip reset), `reset`, and `adopt` in a `STAGE1_DEVELOPMENT` build (`stage1.h`); with no `lfstage.bin` the package goes into RAM. Ctrl+X x3 again while a safe start is still stuck before LiteForth (`safe_boot` still set) resets into recovery (`recover` in `cdc_shared.h`), where a package replaces the stuck image (`upd_mark_broken`). After waking the V5F, `main.c` calls `stage1_wait_v5f`: no HSEM0 within `STAGE1_V5F_START_MS` (3 s) resets the chip and counts the boot in `nostart`, so a trial runs out by itself and `STAGE1_START_TRIES` failed starts in a row give recovery. `reboot` (`lfReboot` in both targets' `lftime.c`) resets the chip from Forth. At every boot `stage1_boot` checks the option bytes (read protection, the write-protection masks in `stage1_layout.h`, the boot ROM's downloads) and prints `V3F security: not locked down: ...`; with `STRICT_RDP` such a board doesn't start the V5F. `lf_*.c` in `V3F/User/` wrap `src/` files as the V5F's do, and its `blocks.h` is the little `card.c` needs. See its README, and `doc/ch32h417-notes.md` for where the chip differs from WCH's documents (add new findings there) |
| `forth/core.f` | Boot code: defines the basic Forth lexicon on top of the primitives. Its LCD blocks hold the palette (`fg!`, `bg!`) font lookup: `glyph ( u blk -- a )` and `font-cache` (flash page 3), and text: `lemit`, `at`, `font` (a 0-ended list of font blocks), `lcls`, `LCD` (doc/fonts.md). Its Calendar block converts `rtc@` seconds to dates and times and back (`>date`, `>time`, `date>`, `time>`, `clock!`, `.date`, `.time`; `unit_tests/calendar` checks them against Python), and the demo's `try` runs an LCD clock (`clock`) in task `t1`. The lookup is checked by `unit_tests/glyph` against the fonts at blocks 100 and 120 of `bin/lfblocks.fb4` |
| `forth/block1.f`, `forth/build.bat` | More Forth source; `build.bat` (Windows, run in `forth/`) writes `core.f` and `block1.f` into `bin/lfblocks.fb4` with `putblocks.py` |
| `scripts/regression.f` | Regression script run by `make test` |
| `scripts/lintf.py` | Checks Forth source against `doc/coding-rules.md` (`lintf.py forth/*.f`; exit 1 on problems). `unit_tests/lintf` checks it on a fixture of known problems and shows (without failing) any problems in `forth/*.f` |
| `scripts/putblocks.py` | Writes a source file into blocks: each `( BLOCK n )` line starts block n (the marker line itself isn't stored), `( BLOCK++ )` starts the block after the last one started, and `( BLOCK IGNORE )` skips the lines up to the next marker, lines padded to 128 columns, 32 rows. Needs an existing signed block file; never writes block 0 (`unit_tests/putblocks`) |
| `scripts/mkfont.py` | Makes 4-bit anti-aliased bitmap fonts (`.fnt`) for the graphic LCDs from TTF/OTF fonts (Noto Sans CJK; more `-f` fonts as fallbacks) with Pillow and fontTools: a glyph list in priority order, `--max-bytes`, `--blocks` (no glyph record crosses a 4 KB block, the font starting at byte 32 of a block), a page table + sorted index for 16-bit code points, and 16-bit run-length pixel codes; `--proof` draws the decoded glyphs. Format in `doc/fonts.md`; `unit_tests/mkfont` runs its self test (and a real build when the font and libraries are there) |
| `scripts/mkexfat.py` | Builds small exFAT card images for tests: MBR (or `--no-mbr`), boot regions with checksum, FAT, bitmap, up-case table, root directory, and `--file NAME[=SRC][,frag,fat,valid=N,pad=N,deleted,badsum,dir,nosig,size=N]` |
| `scripts/putbin.py` | Writes a binary file (a font from `mkfont.py`) into blocks as one stream from byte 32 of the given block (bytes 0-31 zeros, kept for an HMAC; the last block zero-filled), prints the next free block, and warns about font glyphs that cross a block. Same signature and block 0 rules as `putblocks.py` (`unit_tests/putbin`) |
| `scripts/getblocks.py` | The reverse: dumps a block file as text (stdout or `-o`), writing a `( BLOCK n )` marker before each non-blank block, and a binary block (`putbin.py`) as one `( BLOCK IGNORE ) block n: binary, crc32 ...` line that `putblocks.py` skips. `putblocks.py` writes the dump back byte for byte. Also the git diff driver for `*.fb4` (`.gitattributes`): run `git config diff.fb4.textconv "python3 scripts/getblocks.py -0"` once per clone so `git diff` shows blocks as text; GitHub can't use it and collapses `.fb4` diffs |
| `scripts/mksbom.py`, `sbom.spdx.json` | The SBOM (SPDX 2.3, `doc/sbom.md`): the third-party components (Monocypher, blake2s, WCH's EVT, Unity, Noto fonts, Python packages), written from the list in the script with each file's checksums. `unit_tests/sbom` fails if a listed file changes (`--check`) or a file has a copyright notice no component claims (`--scan`): when you change or add third-party code, update `COMPONENTS`/`MODIFIED` and run the script |
| `scripts/admin/hiltest.py` | Hardware-in-the-loop tests of stage 1 on a real board (`--list`, `check`, `run --good PKG --hang PKG`): the USB terminal (pyserial, found by 1A86:FE0C), the debug UART (default COM14); resets by typing `reboot` (or `reset` in recovery), or `--reset-cmd`; writes a report for `doc/board-tests.md` |
| `scripts/admin/sealblocks.py` | Sealed block files (`doc/encrypted-blocks.md`): `root-key`, `release-key`, `board-key` (test boards), `lf-board` (a board file for `lf -K`), `root-header` (`sealed_root.h` for a CH32H417 build), `seal`, `package` (a firmware update package: a sealed file marked `F`, `PACKAGE_MAGIC` in `sealed.h`, that `lf`, the board and `-1 ymodem` never take as a block file; `doc/firmware-updates.md` section 5), `check`, `open`. Needs `cryptography`; `unit_tests/seal` (skipped without it) tests round trips, the layout and every refusal |

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
- Forth source in `forth/` follows `doc/coding-rules.md`: each definition's
  header row carries its stack picture (column 12) and a `\` description
  (column 80), because `aux` records the block and row (`aux[27:5]` block,
  `aux[4:0]` row) and that row is the glossary entry.
- Tunables (`MAX_LOAD_NESTING`, `SYSTEM_BLOCKS`, `SCREEN_COLUMNS`, ...) are in
  `src/target/desktop/options.h`.
- File encodings are mixed. Preserve them byte-for-byte when editing:
  `forth.c` and `core.f` are UTF-8 with a BOM, `forth.h` is Windows-1252 (en
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
  Positions are `[row:column]`, both counted from 0.
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
  desktop timings, which are noisy. `vm_rv32.S` does the same with every
  VM register in a CPU register: the 32 micro-ops have 64-byte handlers at
  fixed offsets (no jump table), each ending by dispatching the next slot.
  A failed `@` leaves 0 in T in both versions (`vmFetch` stores 0).
- **T and N cache the top two data stack items** when `TWO_REGISTER_TOS`
  is defined (`vm.h`, on by default; comment it out for the one-register VM,
  where `datastack[sp]` is the second item). With it, `datastack[sp]` is the
  third. A push stores N to memory and moves T to N (`DDUP`, `VM_DDUP`); a
  pop does the reverse. `sp` still counts the items, so depth checks and
  `.s` are unchanged; `vmPeek(1)`/`vmPoke(1)` reach N and item k >= 2 is
  `datastack[sp + TOS_REGISTERS - k]`. In `vmExec`, `nos` is the second item
  either way (a local, or a macro for `datastack[dsp]`). `task[` also writes N
  to `datastack[sp + 1]` and `]task` reloads N from there, so the
  multitasker works unchanged in both builds.
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
  `loadTIB` returns 0 or `ERR_TIB_OVERFLOW`. `core.f` puts the app's entry jump at code
  cell 0 and the handler's at cell 2 with `,jump`; its handler empties the
  stacks (`0 ]task`), sends output to the console (`con`), makes the task
  ring just the operator (`multitask`), reports the error and parks the app
  (`unit_tests/appstep/test_yeet.py`). The app is stepped (`loadTIB`,
  `lf_putc` while the port is busy) only when `vmRunning()` is 0: never from
  inside a word the terminal runs or the app's own API calls.
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
- **Booting** (`-o 8`, `SYS_OPTION_BOOTING`): `core.f` ends with
  `_text here 32 bit 1 !` then `save-wids save-idata`, so cell 1 points to a
  record (skip address, wordlist count, raw `wids` table; offsets
  `WIDS_RECORD_*` in `forth.h`), followed by the saved IDATA.
  At startup `lfQuit` calls `lfBootFromFlash` to restore `wids`, then resets
  the VM and sets `SYS_OPTION_RUNNING`. A head that isn't a valid link
  (e.g. a C pointer in an image from an older build) becomes NULL.
- **Multitasker** (`core.f`, `doc/multitasking.md`): user areas of 3 cells
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
- **Temporary headers** (`head-` [temporary words] `head+` [permanent
  words] `-heads`, forth.c): in temporary mode `lfHeader` builds each
  header as a C struct in a pool buffer (`TEMP_HEAD_CELLS`) and links it
  with a plain C pointer, which `lfFollow` already treats like a built-in
  header; `latest` is 0 and `temp_latest` points to it. `head+` only leaves
  temporary mode, so permanent headers may link to temporary ones. `-heads`
  skips each wordlist's head past temporary headers, relinks any permanent
  header that points to one, and frees the buffer; the pool's LIFO order
  means this nests inside `open-flash`/`close-flash`. `save-wids` and
  `close-flash` refuse with -122 until `-heads`.
- The interpreter's token buffer is static on purpose (keeps recursion cheap
  on MCU stacks); a level never reads it after executing a word.

## Git notes

- Main branch is `main`; larger work goes on a branch (e.g. `cleanup`) and in
  via PR.
- A shallow clone only tracks `main`. To see other branches:
  `git ls-remote --heads origin`, then
  `git fetch origin <branch>:refs/remotes/origin/<branch>`.
