# Modules (MOD)

The bottom of the V: the code, grouped into modules, each with its
design and its unit tests. A module is part of one architecture element
([architecture.md](architecture.md)). Its detailed design is the document
or header it names; this file doesn't repeat it. [README.md](README.md)
explains the format.

Some suites drive a module through `lf` (the script-driven ones). They
count as unit tests here when they aim at that module's behavior.

## The Forth system

### MOD-VM: The VM in C
The VM's instructions, registers and memory pages, the guarded page's
allowlist, stepping, and the traps that hand faults to Forth.

- **Parent:** ARC-VM
- **Code:** src/vm.c::vmExec, src/vm.h, src/vm_labels.h
- **Design:** README.md#ISA, doc/popthehood.md#VM memory, CLAUDE.md#Design notes
- **Verified by:** unit:ioguard, unit:traps::once: the trap's ior, unit:scripts/regression.f::T{
- **Status:** verified

### MOD-VMASM: The VM in RV32 assembly
`vmExec` for the board, every VM register in a CPU register; must match
the C VM exactly.

- **Parent:** ARC-VM
- **Code:** src/vm_rv32.S
- **Design:** CLAUDE.md#Design notes
- **Verified by:** unit:vmasm::vm_rv32.S matches vm.c
- **Status:** verified

### MOD-FORTH: The text interpreter
Parsing, `QUIT`, `load` and `-->`, the built-in dictionary, error traces,
booting from flash.

- **Parent:** ARC-FORTH
- **Code:** src/forth.c::lfInterpret, src/forth.h
- **Design:** doc/popthehood.md#Boot sequence, CLAUDE.md#Design notes
- **Verified by:** unit:quit::stack underflow, unit:load::chain-done, unit:boot::no-record, unit:heads
- **Status:** verified

### MOD-COMP: The compiler
Compiling words, literals and code slots, `label` and its resolution.

- **Parent:** ARC-FORTH
- **Code:** src/comp.c::lfCompileWord, src/comp.h
- **Design:** CLAUDE.md#Design notes
- **Verified by:** unit:label::Unresolved, unit:scripts/regression.f::T{
- **Status:** verified

### MOD-API0: The C functions Forth calls
The API 0 list, in a fixed order, and the words it implements.

- **Parent:** ARC-FORTH
- **Code:** src/api0.c, src/api0_list.h, src/api0.h
- **Design:** doc/add2api.md
- **Verified by:** unit:api0::api0 order test passed
- **Status:** verified

### MOD-TOOLS: Development tools in Forth
`see`, `dump`, the disassembler, the test words.

- **Parent:** ARC-FORTH
- **Code:** src/tools.c, src/tools.h
- **Design:** doc/glossary.md
- **Verified by:** unit:scripts/regression.f::T{
- **Status:** partial
- **Gap:** the regression script uses the test words; `see` and `dump` have no test of their own

### MOD-CORE-F: The Forth lexicon
`core.f`: the basic words, the multitasker, the editor, the LCD and font
words, the calendar.

- **Parent:** ARC-FORTH, ARC-DISPLAY
- **Code:** forth/core.f
- **Design:** doc/glossary.md, doc/multitasking.md, doc/editor.md, doc/fonts.md
- **Verified by:** unit:calendar, unit:glyph, unit:editor, unit:scripts/regression.f::multitask
- **Status:** verified

## Storage

### MOD-LFBLOCKS: Block buffers
The block buffers, their LRU, and `update`/`save-buffers`.

- **Parent:** ARC-BLOCKS
- **Code:** src/lfblocks.c::lfAssignBlock, src/lfblocks.h
- **Design:** doc/blocks.md
- **Verified by:** unit:blocks::[signed]
- **Status:** verified

### MOD-MEMALLOC: The memory pool
A LIFO pool; on the board in its own section of general-purpose SRAM.

- **Parent:** ARC-BLOCKS
- **Code:** src/memalloc.c, src/memalloc.h
- **Design:** doc/storage.md#Where the buffer lives
- **Verified by:** unit:memalloc::test_lifo_violation
- **Status:** verified

### MOD-EXFAT: exFAT, reading
Finding the volume, a file in a directory, listing a directory; refusing
what's malformed.

- **Parent:** ARC-BLOCKS
- **Code:** src/exfat.c::exfat_find, src/exfat.h
- **Design:** doc/exfat.md
- **Verified by:** unit:exfat::test_find_any_case, unit:exfat::test_root_directory_loop
- **Status:** verified

### MOD-EXFAT-WRITE: exFAT, making, trimming and deleting files
`exfat_create`, `exfat_trim` and `exfat_delete`: a contiguous file in the
root directory, written in an order safe against power cuts.

- **Parent:** ARC-LOG
- **Code:** src/exfat.c::exfat_create, src/exfat.c::exfat_trim, src/exfat.c::exfat_delete
- **Design:** doc/exfat.md#Part 3, doc/admin/data-logs.md#Closing the file in exFAT
- **Verified by:** unit:datalog::test_many_logs, unit:datalog::test_stale
- **Status:** verified

### MOD-CARD: Files on the card
The block space, the stage, pages and backup files, and the listing.

- **Parent:** ARC-BLOCKS
- **Code:** src/card.c::card_find_file, src/card.h
- **Design:** doc/sdcard.md
- **Verified by:** unit:exfat::test_card_signed_file, unit:exfat::test_card_fragmented_file
- **Status:** verified

### MOD-YMODEM: YMODEM in LiteForth
Receiving source, binary data, a whole block file or a package.

- **Parent:** ARC-BLOCKS
- **Code:** src/ymodem.c::lfAPI_ymodem, src/ymodem.h
- **Design:** doc/blocks.md#Sending files over the terminal
- **Verified by:** unit:ymodem::corrupt packet NAKed, unit:sealed::stage: written exactly
- **Status:** verified

### MOD-DESKTOP: The desktop target
`main`, the files for flash and blocks, the terminal, the clock, the
board file.

- **Parent:** ARC-DESKTOP
- **Code:** src/target/desktop/main.c, src/target/desktop/flash.c, src/target/desktop/blocks.c, src/target/desktop/serial_io.c, src/target/desktop/lftime.c, src/target/desktop/board.c
- **Design:** doc/targets.md#Desktop, CLAUDE.md#Running lf by hand
- **Verified by:** unit:flash, unit:serial_io, unit:rtc::[woke], unit:reboot, unit:sealed::enroll without -K
- **Status:** verified

## Security and updates

### MOD-SEALED: Reading packages
Mounting and checking a package's block 0, opening its blocks, checking
a package without the key, and a block 0 alone for the boot check.

- **Parent:** ARC-PKG
- **Code:** src/sealed.c::sealed_mount_package, src/sealed.c::sealed_check_signed, src/sealed.h
- **Design:** doc/admin/encrypted-blocks.md#On the board
- **Verified by:** unit:sealed::stage: checked, unit:update::test_signed, unit:blkrun
- **Status:** verified

### MOD-CRYPTO: Ciphers, hashes and signatures
AES-128 and SM4 in CTR mode (`ctr.c`), BLAKE2s, and Monocypher's X25519,
Ed25519 and Poly1305.

- **Parent:** ARC-PKG, ARC-LOG
- **Code:** src/ctr.c, src/blake2s.c, src/monocypher.c, src/monocypher-ed25519.c
- **Design:** doc/admin/encrypted-blocks.md#Encryption, doc/admin/sbom.md
- **Verified by:** unit:ctr, unit:blake2s::test_vectors, unit:update::test_signed
- **Status:** verified

### MOD-UPDATE: Stage 1's install logic
The record, the token, install, backup, trial, rollback, the boot check
(MAC and signature), adopt and recovery, all through callbacks.

- **Parent:** ARC-STAGE1
- **Code:** src/update.c::upd_boot, src/update.h
- **Design:** doc/admin/firmware-updates.md#Installing an update, doc/admin/firmware-updates.md#The security sector
- **Verified by:** unit:update::test_power_cuts, unit:update::test_signed, unit:update::test_record_copies
- **Status:** verified

### MOD-YMRECV: YMODEM in stage 1
Receiving a package in recovery.

- **Parent:** ARC-STAGE1
- **Code:** src/ymrecv.c::ymr_receive, src/ymrecv.h
- **Design:** doc/admin/firmware-updates.md#Recovery on the board now
- **Verified by:** unit:ymrecv
- **Status:** verified

### MOD-STAGE1: Stage 1 on the V3F
The flash and card callbacks, the lock-down check and `lockdown`, the
board's key and `enroll`, the recovery terminal, starting the V5F.

- **Parent:** ARC-STAGE1
- **Code:** src/target/CH32H417/V3F/User/stage1.c::stage1_boot, src/target/CH32H417/V3F/User/stage1_layout.h, src/target/CH32H417/Common/security.h
- **Design:** doc/admin/firmware-updates.md#On the board now, doc/admin/firmware-updates.md#The lock-down check
- **Verified by:** unit:stage1sim::s_enroll, unit:stage1sim::s_lockdown_command, unit:stage1sim::s_recovery_commands
- **Status:** verified

### MOD-V3F: The V3F's main loop and bridge
USB CDC, the rings in shared RAM, Ctrl+X, the flags between the cores.

- **Parent:** ARC-V3F
- **Code:** src/target/CH32H417/Common/cdc_bridge.c, src/target/CH32H417/Common/cdc_shared.h, src/target/CH32H417/V3F/User/main.c
- **Design:** src/target/CH32H417/Common/cdc_shared.h
- **Verified by:** unit:stage1sim::s_recovery_ram_and_ctrl_x
- **Status:** partial
- **Gap:** the V3F's `main.c` runs only on the board (stage1sim has its own); B18 covers it

### MOD-PMP: The V5F's PMP and ecall services
The four PMP entries, `pmp_report`, the flash service, machine mode in
flash.

- **Parent:** ARC-MMODE
- **Code:** src/target/CH32H417/V5F/User/pmp.c::pmp_lock, src/target/CH32H417/V5F/User/mmode.h
- **Design:** doc/admin/firmware-updates.md#Keeping the V5F out
- **Verified by:** board:B1, board:B14, board:B18, board:B22, board:B23, board:B24
- **Status:** partial
- **Gap:** no unit test: the PMP and machine mode can only be tried on the chip (B12, B13 and B16 tried layouts since retired)

### MOD-GDMA: Guarded DMA
The fixed channel table, the machine-mode buffer, the user buffer check,
burning the channel after each transfer.

- **Parent:** ARC-MMODE
- **Code:** src/target/CH32H417/V5F/User/gdma.c, src/target/CH32H417/V5F/User/gdma.h
- **Design:** doc/admin/firmware-updates.md#Keeping the V5F out
- **Verified by:** unit:blocksim::test_guard, unit:blocksim::test_protected
- **Status:** verified

### MOD-GUARD: Store emulation and the buffer rule
Decoding a faulting store, allowing only RCC and CRC; the rule for
buffers a service may touch.

- **Parent:** ARC-MMODE
- **Code:** src/target/CH32H417/V5F/User/guard.c, src/target/CH32H417/V5F/User/ch32h417_it.c
- **Design:** src/target/CH32H417/V5F/User/guard.h
- **Verified by:** unit:guard::test_decode, unit:guard::test_allowed, unit:guard::test_buffer
- **Status:** verified

## The V5F's drivers

### MOD-V5F-BLOCKS: The card driver
SDIO by Guarded DMA, runs of sectors, the block file and the other card
files, the stage check.

- **Parent:** ARC-V5F, ARC-BLOCKS
- **Code:** src/target/CH32H417/V5F/User/blocks.c::blk_stage_check, src/target/CH32H417/V5F/User/blocks.h
- **Design:** doc/sdcard.md, doc/blocks.md#Transfers without waiting
- **Verified by:** unit:blocksim::test_reads, unit:blocksim::test_faults
- **Status:** verified

### MOD-V5F-FLASH: Flash pages on the V5F
The pages in the code flash, their shadow in DTCM, programming through
machine mode's service.

- **Parent:** ARC-V5F
- **Code:** src/target/CH32H417/V5F/User/flash.c
- **Design:** doc/flashmem.md
- **Verified by:** board:B14, board:B24
- **Status:** partial
- **Gap:** no unit test; the desktop's flash.c is tested instead

### MOD-V5F-BOARD: The board's identity
The UID, the root key and product for `-2 ymodem`'s keyless check, and
`enroll` through stage 1.

- **Parent:** ARC-V5F
- **Code:** src/target/CH32H417/V5F/User/board.c
- **Design:** doc/admin/updates.md#Both cores
- **Verified by:** board:B22, board:B25
- **Status:** partial
- **Gap:** no unit test

### MOD-ECDC: The AES and SM4 engine
CTR mode on the ECDC engine, checked once against software, in place
only in DTCM.

- **Parent:** ARC-V5F, ARC-PKG
- **Code:** src/target/CH32H417/V5F/User/ecdc.c::ctr_hw_crypt
- **Design:** doc/ch32h417-notes.md#ECDC
- **Verified by:** unit:datalog::record not wiped, board:B27
- **Status:** partial
- **Gap:** the engine exists only on the chip: `ecdc-test` (B27) compares it with software there; the desktop tests its failure handling with a stand-in

### MOD-V5F-TIME: Time and standby on the V5F
The microsecond counter, the RTC, the alarm and standby.

- **Parent:** ARC-V5F
- **Code:** src/target/CH32H417/V5F/User/lftime.c
- **Design:** doc/glossary.md
- **Verified by:** unit:rtc, board:B27
- **Status:** partial
- **Gap:** the desktop's version is unit-tested; the V5F's is shown on the board (B27)

### MOD-LCD: The LCD drivers
The simulated LCD and its ST7789 model on the desktop, the ST7789 on SPI
on the V5F.

- **Parent:** ARC-DISPLAY
- **Code:** src/target/desktop/lcd.c, src/target/CH32H417/V5F/User/lcd.c
- **Design:** doc/fonts.md#Drawing text in Forth
- **Verified by:** unit:lcd
- **Status:** partial
- **Gap:** the V5F's driver has no test

## Data logs

### MOD-DATALOG: The log format
Keys, block 0, records, the end of a log after a power cut.

- **Parent:** ARC-LOG
- **Code:** src/datalog.c::dl_record, src/datalog.c::dl_find_end, src/datalog.h
- **Design:** doc/admin/data-logs.md#Deriving the keys, doc/admin/data-logs.md#The file
- **Verified by:** unit:datalog::test_round_trip, unit:datalog::test_tampering, unit:datalog::record not wiped
- **Status:** verified

### MOD-LOGFILE: Log files
Making, closing and recovering log files on the card.

- **Parent:** ARC-LOG
- **Code:** src/logfile.c::log_recover, src/logfile.h
- **Design:** doc/admin/data-logs.md#After a power cut
- **Verified by:** unit:datalog::test_power_cuts, unit:datalog::test_stale
- **Status:** verified

## PC tools

### MOD-SEALBLOCKS: The package tool
- Keys, packages, `check` and `open`, the root key header, and the
provisioning manifest.

- **Parent:** ARC-PKG
- **Code:** scripts/admin/sealblocks.py
- **Design:** doc/admin/encrypted-blocks.md#On the PC
- **Verified by:** unit:seal::package round trip, unit:seal::retired release key, unit:seal::manifest: a changed image
- **Status:** verified

### MOD-LFLOG: The log tool
The server's key, the board's header for it, opening logs.

- **Parent:** ARC-LOG
- **Code:** scripts/admin/lflog.py
- **Design:** doc/admin/data-logs.md#On the server
- **Verified by:** unit:datalog::test_lflog_edges
- **Status:** verified

### MOD-BLOCKTOOLS: Block and font tools
`putblocks.py`, `getblocks.py`, `putbin.py`, `mkfont.py`, `mkexfat.py`.

- **Parent:** ARC-TOOLS
- **Code:** scripts/putblocks.py, scripts/getblocks.py, scripts/putbin.py, scripts/mkfont.py, scripts/mkexfat.py
- **Design:** doc/blocks.md#Using Blocks, doc/fonts.md#Making a font
- **Verified by:** unit:putblocks::round trip: same blocks, unit:putbin, unit:mkfont
- **Status:** verified

### MOD-BOARDTOOLS: Board test and provisioning tools
`hiltest.py`, `lockdown.py`, `provision.py` and `lfmcp.py`.

- **Parent:** ARC-TOOLS
- **Code:** scripts/admin/hiltest.py, scripts/admin/lockdown.py, scripts/admin/provision.py, scripts/lfmcp.py
- **Design:** doc/admin/firmware-updates.md#without bricking anything, doc/admin/provisioning.md#Pass 1
- **Verified by:** unit:stage1sim::hiltest on the simulated board, unit:stage1sim::lockdown.py run: a locked board refused before any flash, unit:stage1sim::provision.py on the simulated board: passed, unit:lfmcp::wlink flash refused
- **Status:** verified

### MOD-QATOOLS: Quality tools
The SBOM, the lint, the RTM, the machine-mode check, the board build.

- **Parent:** ARC-QA
- **Code:** scripts/mksbom.py, scripts/lintf.py, scripts/rtm.py, scripts/mmodecheck.py, scripts/ch32build.sh
- **Design:** doc/admin/sbom.md, doc/coding-rules.md, doc/quality/README.md
- **Verified by:** unit:sbom, unit:lintf, unit:rtm
- **Status:** partial
- **Gap:** mmodecheck.py and ch32build.sh have no test of their own; CI runs them on every pull request to main
