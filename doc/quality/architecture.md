# Architecture (ARC)

The components LiteForth is built from, what each is responsible for, and
how they meet. Each element satisfies system requirements
([system.md](system.md)), is made of modules ([modules.md](modules.md)),
and is verified by tests that run it with its neighbors: integration
tests on the desktop, the simulated board, or the board itself.
[README.md](README.md) explains the format; `CLAUDE.md` and
[popthehood.md](../popthehood.md) hold the design notes.

```
 desktop (lf)                          CH32H417
 ------------                          ---------------------------------------------
                                       V3F core                 V5F core
 ARC-FORTH  interpreter, compiler      ARC-STAGE1 root of trust ARC-FORTH, ARC-VM
 ARC-VM     the Forth VM                 updates, keys,         ARC-V5F  drivers, flash,
 ARC-BLOCKS blocks, card, exFAT          recovery, lock-down             card, LCD, RTC
 ARC-DESKTOP files, stdio, serial      ARC-V3F USB terminal     ARC-MMODE PMP, services,
                                         bridge  <-shared RAM->           Guarded DMA
 shared: ARC-PKG packages, ARC-LOG data logs, ARC-DISPLAY fonts and LCD
 PC: ARC-TOOLS scripts; process: ARC-QA build, tests, CI, SBOM, RTM
```

### ARC-VM: The Forth virtual machine
A 16-bit-instruction stack machine that runs the compiled Forth: its
registers, stacks, memory pages (flash, RAM, the guarded I/O page with its
allowlist), the application's stepping and step limit, and traps that
turn CPU faults into iors. One C implementation, and one in RV32 assembly
for the board that must match it. Interfaces: `vmRun`, `vmExec`,
`vm_io_ranges`, `vmTrap`; API 0 calls out to C.

- **Parent:** SYS-VM-01, SYS-VM-03, SYS-LANG-07, SYS-LANG-09
- **Verified by:** integ:vmasm::vm_rv32.S matches vm.c, integ:traps::app: PC at the yeet handler, sys:scripts/regression.f::T{, sys:deadtib::VM ran too long
- **Status:** verified

### ARC-FORTH: The interpreter, compiler and dictionary
Parses and interprets terminal lines and blocks, compiles into the flash
pages, keeps the dictionary (built-in tables and headers in VM memory),
saves and boots it, and reports errors with their traces. The Forth half
(`core.f`) adds the lexicon, the multitasker, the editor, fonts and the
calendar. Interfaces: the terminal (`serial_io.h`), blocks
(`lfblocks.h`), flash pages (`flash.h`), API 0 (`api0_list.h`).

- **Parent:** SYS-LANG-01, SYS-LANG-02, SYS-LANG-03, SYS-LANG-04, SYS-LANG-05, SYS-LANG-06, SYS-LANG-08, SYS-LANG-10, SYS-VM-02, SYS-SEC-10
- **Verified by:** sys:quit::stack underflow, sys:load::chain-done, sys:boot::forth root extra, sys:label, sys:heads, sys:editor, sys:api0
- **Status:** verified

### ARC-BLOCKS: Blocks, the card and the memory pool
Block buffers over a block space that's a file: on the desktop a file or
a card image, on the board `lfblocks.fb4` found on the card's exFAT
volume. Also the other card files (`lfstage.bin`, `lfpages.bin`), file
transfers over the terminal (`ymodem`), and the memory pool that
occasional buffers come from. Interfaces: `blocks.h` (each target's),
`card.h`, `exfat.h`, `memalloc.h`, `ymodem.h`.

- **Parent:** SYS-STO-01, SYS-STO-02, SYS-STO-04, SYS-STO-05, SYS-STO-06, SYS-STO-08, SYS-LANG-02
- **Verified by:** integ:exfat::run_lf: lf finds lfblocks.fb4, sys:blocks::[signed], sys:ymodem::corrupt packet NAKed, sys:sealed::package refused as a block file, integ:blkrun, sys:ymodem::Ctrl+X runs, board:B21, board:B26
- **Status:** verified

### ARC-DESKTOP: The desktop host
lf on a PC: flash and blocks in files, the terminal on stdio or a serial
port read by its own thread, the clock from the host's, a simulated LCD,
and a board file standing in for the board's keys (`-K`).

- **Parent:** SYS-TERM-02, SYS-TERM-03, SYS-LANG-03, SYS-TIME-01, SYS-TIME-02
- **Verified by:** sys:boot, sys:reboot, sys:rtc::[woke], sys:appstep, unit:serial_io
- **Status:** verified

### ARC-V5F: The V5F runtime
LiteForth on the V5F core, in user mode: start-up, the card driver
(SDIO by Guarded DMA), the flash pages (through machine mode's service),
the board's identity without its key, the ECDC engine for AES and SM4,
the LCD on SPI, the RTC and standby, and the memory map.

- **Parent:** SYS-STO-03, SYS-STO-07, SYS-DISP-01, SYS-TIME-01, SYS-TIME-02, SYS-UPD-10
- **Verified by:** integ:blocksim::test_reads, integ:blocksim::test_faults, board:B17, board:B24, board:B26, board:B27
- **Status:** partial
- **Gap:** the LCD has no board run logged

### ARC-MMODE: The V5F's machine mode
What user mode can't change or get around: the PMP entries set before
`main` (stage 1's flash and the V3F's RAM fenced, the DMA, flash and USBFS
registers read-only), machine-mode code run from flash, the `ecall`
services (flash programming, Guarded DMA), and the fault handler that
emulates RCC and CRC stores and sends faults to Forth.

- **Parent:** SYS-SEC-01, SYS-SEC-02, SYS-SEC-03, SYS-SEC-04, SYS-SEC-08, SYS-SEC-11, SYS-STO-07, SYS-LANG-09
- **Verified by:** integ:blocksim::test_guard, board:B14, board:B15, board:B18, board:B22, board:B23, board:B24
- **Status:** partial
- **Gap:** machine mode's data and stack are in RAM user mode can write; the interrupt controller's reach from user mode is unchecked (SYS-SEC-11)

### ARC-V3F: The V3F's USB terminal
The V3F core runs USB CDC and bridges it to the V5F through rings in
shared RAM, with the Ctrl+X escape and the flags that tie the cores
together (an update waiting, a confirm, a safe boot, recovery).
Interface: `cdc_shared.h`.

- **Parent:** SYS-TERM-01, SYS-TERM-04
- **Verified by:** integ:stage1sim::s_recovery_ram_and_ctrl_x, integ:stage1sim::s_reboot, board:B18
- **Status:** verified

### ARC-STAGE1: Stage 1, the root of trust
Runs on the V3F at every reset before the V5F: keeps the security record
and the device token (and so the board's key), checks the option bytes,
installs, confirms and rolls back updates, checks the installed image
(its MAC and, at level 2, its signature) before starting the V5F, and
runs the recovery terminal. Reaches the flash and the card only through
`upd_io` callbacks.

- **Parent:** SYS-UPD-03, SYS-UPD-04, SYS-UPD-05, SYS-UPD-06, SYS-UPD-07, SYS-UPD-08, SYS-UPD-09, SYS-UPD-11, SYS-UPD-12, SYS-SEC-01, SYS-SEC-05, SYS-SEC-06, SYS-SEC-07, SYS-SEC-12, SYS-LOG-08
- **Verified by:** integ:stage1sim::s_install, integ:stage1sim::s_power_cuts, integ:stage1sim::s_enroll, integ:stage1sim::s_lockdown, board:B6, board:B11, board:B25
- **Status:** partial
- **Gap:** the log key service (SYS-LOG-08) isn't built

### ARC-PKG: Update packages
The sealed format: per-board content keys wrapped with X25519, AES or SM4
in CTR mode, BLAKE2s hashes per block and per table, Ed25519 signatures
and certificates; read by the board (`sealed.c`) and written by the
vendor (`sealblocks.py`).

- **Parent:** SYS-UPD-01, SYS-UPD-02, SYS-STO-04, SYS-TOOL-01
- **Verified by:** sys:sealed::stage: checked, unit:seal::package round trip, integ:update::test_refusals, board:B2
- **Status:** verified

### ARC-LOG: Data logs
Encrypted, authenticated logs on the card: the format and keys
(`datalog.c`), the log files on exFAT and their recovery after a power
cut (`logfile.c`, `exfat_create`/`exfat_trim`), and the server's tool.
The capture from the CODECs and stage 1's key service are still to come.

- **Parent:** SYS-LOG-01, SYS-LOG-02, SYS-LOG-03, SYS-LOG-04, SYS-LOG-05, SYS-LOG-06, SYS-LOG-07, SYS-LOG-09, SYS-TOOL-04
- **Verified by:** integ:datalog::test_round_trip, integ:datalog::test_power_cuts, integ:datalog::record not wiped
- **Status:** partial
- **Gap:** not on the board yet (SYS-LOG-07, SYS-LOG-08 planned)

### ARC-DISPLAY: The display
LCD drivers (simulated on the desktop, ST7789 on the board), and the
bitmap fonts: made on the PC, kept in blocks, looked up and drawn by
Forth.

- **Parent:** SYS-DISP-01
- **Verified by:** integ:glyph, integ:lcd
- **Status:** partial
- **Gap:** no board run logged

### ARC-TOOLS: The PC tools
The scripts that prepare what goes on a board and drive one: blocks and
fonts in and out of block files, card images, the board tests and
lock-down, and the assistant's access to the terminal and probe.

- **Parent:** SYS-TOOL-02, SYS-TOOL-03, SYS-SEC-09
- **Verified by:** integ:putblocks::round trip: same blocks, integ:stage1sim::hiltest on the simulated board, integ:stage1sim::lockdown.py run: a locked board refused before any flash, integ:lfmcp::wlink flash refused
- **Status:** verified

### ARC-QA: Build, test and evidence
The makefile and its test suites, CI, the board's build check
(`ch32build.sh` with `mmodecheck.py`), the SBOM, the coding rules' lint,
the board-test log and the RTM.

- **Parent:** SYS-QA-01, SYS-QA-02, SYS-QA-03, SYS-QA-04, SYS-QA-05, SYS-SEC-04
- **Verified by:** ci:.github/workflows/c-cpp.yml::make test, ci:scripts/ch32build.sh::mmodecheck, sys:sbom, sys:rtm
- **Status:** verified
