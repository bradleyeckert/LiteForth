# System requirements (SYS)

What LiteForth, as a whole product, must do: each requirement refines a
stakeholder need ([needs.md](needs.md)), is satisfied by architecture
elements ([architecture.md](architecture.md)), and is verified by system
or integration tests, or on a board. [README.md](README.md) explains the
format and the evidence kinds.

"The board" is the CH32H417 target; "lf" is the desktop build. "Level"
is `SECURITY_LEVEL` (`Common/security.h`): 0 and 1 for development, 2
for production.

## LANG: the Forth system

### SYS-LANG-01: Interpret and compile from the terminal
The system interprets and compiles Forth from the terminal a line at a
time. An undefined word, a stack underflow or overflow, or a line longer
than the input buffer is reported with its ior and message, and the
system carries on with empty stacks and decimal base.

- **Parent:** UN-01, UN-02
- **Verified by:** sys:quit::stack underflow, sys:quit::TIB filled up, sys:scripts/regression.f::T{, board:B24
- **Status:** verified

### SYS-LANG-02: Load source from blocks
`load` interprets a block, nested to `MAX_LOAD_NESTING`, `-->` chains
blocks without nesting, and an error reports the block, row and column of
each level it unwound through.

- **Parent:** UN-01, UN-04
- **Verified by:** sys:load::chain-done, sys:load::Screen 31 [0:21], board:B24
- **Status:** verified

### SYS-LANG-03: The dictionary persists in flash and boots
Words compiled into the flash pages survive a reset; at start-up the
system restores the wordlists from flash and starts the application,
unless the board is reset with Ctrl+X three times.

- **Parent:** UN-01, UN-03
- **Verified by:** sys:boot::forth root extra, unit:flash, board:B24, board:B18
- **Status:** verified

### SYS-LANG-04: Forward references
`label` declares a word to be defined later; calling it before then
fails with -105, and a label still unresolved at `bye` is reported
(-119).

- **Parent:** UN-01
- **Verified by:** sys:label::Unresolved `later` found
- **Status:** verified

### SYS-LANG-05: Temporary headers
Headers made between `head-` and `head+` are dropped by `-heads`; the
dictionary can't be saved while any remain (-122).

- **Parent:** UN-01
- **Verified by:** sys:heads::Missing `-heads`
- **Status:** verified

### SYS-LANG-06: Cooperative multitasking
Tasks share the VM cooperatively through `pause`, each with its own
stacks and user area, and the terminal is a task of the ring.

- **Parent:** UN-03
- **Verified by:** sys:scripts/regression.f::multitask
- **Status:** verified

### SYS-LANG-07: The application runs while the terminal is idle
A started application runs whenever no terminal input is waiting. An
error in it goes to its own handler (`yeet`), not to the terminal, and
an application that never yields is stopped (-116) and reported.

- **Parent:** UN-03
- **Verified by:** sys:appstep::yeet handler tests passed, sys:deadtib::VM ran too long
- **Status:** verified

### SYS-LANG-08: The application can take the keyboard
`stop-tib` gives a running application the terminal's input until it
gives it back.

- **Parent:** UN-03
- **Verified by:** sys:deadtib::terminal-back
- **Status:** verified

### SYS-LANG-09: CPU faults become Forth errors
On the board, a misaligned access (-23), an access fault (-127) or an
illegal instruction (-128) in Forth code is reported to Forth (the
terminal, or the application's handler) instead of resetting the chip.

- **Parent:** UN-01, UN-03
- **Verified by:** integ:traps::app: PC at the yeet handler, board:B15, board:B18, board:B27
- **Status:** partial
- **Gap:** -23 and -128 can't be made from Forth on the board (whole-cell accesses, no machine code), so they're tested on the desktop with API calls standing in

### SYS-LANG-10: A block editor
Blocks are edited on the board from the terminal, each change saved at
once.

- **Parent:** UN-01, UN-04
- **Verified by:** sys:editor::inserted above line 0
- **Status:** verified

## VM: the virtual machine

### SYS-VM-01: One VM, two implementations
The VM in assembly for the board (`vm_rv32.S`) behaves exactly as the C
VM for every instruction, with one and two cached stack registers.

- **Parent:** UN-02
- **Verified by:** integ:vmasm::vm_rv32.S matches vm.c
- **Status:** verified

### SYS-VM-02: Compiled code stays valid across releases
The indices of the C functions Forth calls (API 0) never change, so code
saved in flash keeps working with a newer firmware.

- **Parent:** UN-01, UN-07
- **Verified by:** sys:api0::api0 order test passed
- **Status:** verified

### SYS-VM-03: Forth reaches only allowed peripherals
At level 1 and 2, Forth's I/O page reaches only the peripherals in the
allowlist (`io_allow.h`): any other register reads as an invalid address
(-9), and a write to a read-only one is refused (-20).

- **Parent:** UN-09, UN-11
- **Verified by:** unit:ioguard, board:B7
- **Status:** verified

## STO: storage

### SYS-STO-01: The block file on the card
The block space is `lfblocks.fb4` in the root directory of the card's
exFAT volume, contiguous, in 4 KB blocks. Without its `LITEFORTH`
signature it offers one readable block and is never written.

- **Parent:** UN-04
- **Verified by:** sys:blocks::[foreign], integ:exfat::test_card_signed_file, integ:exfat::test_card_unsigned_file, board:B26
- **Status:** verified

### SYS-STO-02: Malformed cards are refused safely
A malformed exFAT volume (a directory loop, a bad checksum, a volume
larger than its partition, a cluster past the end) is refused or the
entry ignored; nothing is read or written outside the volume.

- **Parent:** UN-04, UN-09
- **Verified by:** integ:exfat::test_root_directory_loop, integ:exfat::test_find_ignores_bad_checksum, integ:exfat::test_volume_larger_than_partition, integ:exfat::test_first_cluster_past_the_volume, integ:exfat::test_sectors_and_fence
- **Status:** verified

### SYS-STO-03: Card transfers by DMA, with errors reported
Reads and writes of the card run by DMA in the background; a card error,
a timeout or a DMA error is reported as an ior, and the next transfer
works.

- **Parent:** UN-04
- **Verified by:** integ:blocksim::test_faults, integ:blocksim::test_reads, integ:blocksim::test_writes, board:B17
- **Status:** verified

### SYS-STO-04: A package is never a block file
A block file whose block 0 is a firmware update package or a retired
sealed block file offers no blocks and is never written.

- **Parent:** UN-04, UN-07
- **Verified by:** sys:sealed::package refused as a block file, sys:sealed::sealed block file refused, board:B21
- **Status:** verified

### SYS-STO-05: Files over the terminal
`ymodem` receives source into blocks, binary data from a block on, a
whole block file (checked, block 0 written last, read back), or an update
package into `lfstage.bin`, over the terminal, with corrupt packets
retried.

- **Parent:** UN-01, UN-04, UN-07
- **Verified by:** sys:ymodem::corrupt packet NAKed, sys:ymodem::read back and checked; capacity now 64, board:B2
- **Status:** verified

### SYS-STO-06: An interrupted install can't pass for a good one
If a `-1 ymodem` install stops part way, block 0 says the install didn't
finish, so the half-written file isn't mistaken for a good one.

- **Parent:** UN-04, UN-07
- **Verified by:** sys:ymodem::the install didn't finish
- **Status:** verified

### SYS-STO-07: Flash pages written only through a checked service
On the board, the application's flash pages are erased and programmed
only by machine mode's service, which checks the address range; at level
2 the service refuses, `open-flash`, `close-flash` and `save-pages`
refuse with -20, and only update packages write the pages.

- **Parent:** UN-08, UN-09
- **Verified by:** board:B14, board:B19, board:B28, sys:locked::locked tests passed, unit:update::test_app_locked
- **Status:** partial
- **Gap:** on the board, `close-flash` refusing at level 2 (be529e1) isn't on a board yet; B28 showed it answering `ok`, a no-op

### SYS-STO-08: Shared memory for occasional buffers
Buffers needed only now and then, and the data logs' buffers, come from
one memory pool, allocated and freed in strict order, in general-purpose
SRAM on the board.

- **Parent:** UN-01, UN-10
- **Verified by:** unit:memalloc, board:B26
- **Status:** verified

## TERM: the terminal

### SYS-TERM-01: USB terminal through the V3F
On the board the terminal is a USB CDC serial port run by the V3F core
and bridged to the V5F through shared memory; Ctrl+X three times resets
the chip and starts LiteForth without the application.

- **Parent:** UN-01, UN-03
- **Verified by:** board:B18, integ:stage1sim::s_recovery_ram_and_ctrl_x
- **Status:** verified

### SYS-TERM-02: The desktop terminal never blocks the application
On the desktop, lf talks over stdio or a serial port, and reading the
terminal never stops the application from running.

- **Parent:** UN-02, UN-03
- **Verified by:** unit:serial_io, sys:appstep
- **Status:** verified

### SYS-TERM-03: Restart from Forth
`reboot` resets the chip (on the desktop, restarts lf).

- **Parent:** UN-01, UN-07
- **Verified by:** sys:reboot, board:B25
- **Status:** verified

### SYS-TERM-04: Binary transfers pass every byte
During a YMODEM transfer the terminal passes every byte to the receiver:
Ctrl+X in the data doesn't reset the board.

- **Parent:** UN-01, UN-07
- **Verified by:** sys:ymodem::Ctrl+X runs, insp:src/target/CH32H417/Common/cdc_shared.h::CDC_BINARY_MAGIC, board:B27
- **Status:** verified

## UPD: firmware updates

### SYS-UPD-01: Signed and encrypted packages
A firmware update package is encrypted for a list of boards (each
board's X25519 key) and signed by a release key that the root key
certifies. A package that's changed, signed by another key, made for
another product or for another board, or carries a retired release key,
is refused.

- **Parent:** UN-07, UN-08
- **Verified by:** unit:seal::not a recipient, unit:seal::wrong root, unit:seal::retired release key, unit:update::test_refusals, board:B2
- **Status:** verified

### SYS-UPD-02: Packages checked before staging
`-2 ymodem` writes a package into `lfstage.bin` and checks everything it
can without the board's private key before telling stage 1; a package
that fails leaves nothing for stage 1 to install.

- **Parent:** UN-07
- **Verified by:** sys:sealed::stage refused: not a package, sys:sealed::stage: for another board, board:B2
- **Status:** verified

### SYS-UPD-03: Install on trial, confirm or roll back
Stage 1 backs up the running image (encrypted, MACed) to the card,
installs the new one, and starts it on trial; LiteForth starting confirms
it, and an image not confirmed within its trial boots is rolled back.

- **Parent:** UN-07
- **Verified by:** unit:update::test_install_and_confirm, unit:update::test_rollback, integ:stage1sim::s_install, integ:stage1sim::s_rollback, integ:stage1sim::s_rollback_unattended, board:B3, board:B4, board:B6, board:B10, board:B11
- **Status:** verified

### SYS-UPD-04: Power cuts during an update
A power cut at any step of an install or a rollback leaves the board
running exactly the old image or the new one at the next start.

- **Parent:** UN-07
- **Verified by:** unit:update::test_power_cuts, integ:stage1sim::s_power_cuts
- **Status:** verified

### SYS-UPD-05: A refused update changes nothing
A package that fails any check, one swapped while installing, or an
update with no room for its backup, leaves the running image and its
backup as they were.

- **Parent:** UN-07
- **Verified by:** unit:update::test_refusals, unit:update::test_package_swapped, unit:update::test_changed_while_installing, integ:stage1sim::s_refusals, board:B5a
- **Status:** verified

### SYS-UPD-06: No downgrade below the minimum
A package older than the board's minimum version is refused, and a
rollback never restores a backup older than it; a package can raise the
minimum when its image is confirmed.

- **Parent:** UN-07, UN-08
- **Verified by:** unit:update::older revision, unit:seal::old revision, integ:stage1sim::minimum: version 1 refused
- **Status:** partial
- **Gap:** the minimum is kept in stage 1's record, which isn't hardware-protected; firmware-updates.md section 10

### SYS-UPD-07: Recovery over USB
With no working image (none, one that fails its check, a failed trial
with no backup, or one that never starts), stage 1 keeps the V5F off and
offers a recovery terminal over USB that takes a package; a V5F stuck at
start is sent there after a few failed starts.

- **Parent:** UN-07
- **Verified by:** integ:stage1sim::s_recovery_card, integ:stage1sim::s_recovery_ram_and_ctrl_x, integ:stage1sim::s_recovery_commands, integ:stage1sim::s_nostart, unit:update::test_recovery, board:B5, board:B6
- **Status:** verified

### SYS-UPD-08: Application packages
A package can carry the V5F firmware with LiteForth's flash pages, the
Forth application, installed, backed up and rolled back as one image.

- **Parent:** UN-07
- **Verified by:** unit:update::test_app_locked, unit:update::test_app_development, sys:savepages, board:B19, board:B25
- **Status:** verified

### SYS-UPD-09: Enrolling a board
`enroll` prints the board's line for the vendor's `boards.csv`. At level
2 the board's key is made from its RNG, once, by stage 1, which keeps
it; below level 2 it's derived from the UID, for development only.

- **Parent:** UN-07, UN-09, UN-11
- **Verified by:** integ:stage1sim::s_enroll, sys:sealed::enroll prints the boards.csv line, board:B22, board:B25
- **Status:** verified

### SYS-UPD-12: The backup on the card can't be read or changed
The backup stage 1 writes to the card is encrypted and MACed with the
device token's keys: it isn't the image in the clear, and a changed one
is never restored.

- **Parent:** UN-07, UN-08, UN-09
- **Verified by:** integ:update::test_backup_on_card, integ:update::a changed backup isn't restored, integ:update::a backup claiming another version isn't restored
- **Status:** partial
- **Gap:** unit level only; not on the simulated board or a board

### SYS-UPD-10: The installed version
`revision` reports the installed image's version.

- **Parent:** UN-07
- **Verified by:** board:B20, board:B25
- **Status:** verified

### SYS-UPD-11: Development boards adopt what the debugger flashed
Below level 2, an image the debugger wrote is adopted as version 0, so
developers can reflash at will; at level 2 it fails the boot check.

- **Parent:** UN-11
- **Verified by:** unit:update::test_debugger_adopt, unit:update::test_blank_without_adopt, board:B20
- **Status:** verified

## SEC: protection

### SYS-SEC-01: The V5F's CPU can't reach stage 1's secrets
The board's private key, the device token and stage 1's records are in
flash sectors that the V5F's CPU, in user or machine mode, can't read or
write (PMP entry 0, locked).

- **Parent:** UN-09
- **Verified by:** board:B18, board:B22, insp:src/target/CH32H417/V5F/User/pmp.c::pmp_lock
- **Status:** partial
- **Gap:** bus masters the V5F's code can program (the ECDC engine, SDMMC, USBHS) aren't fenced, so this rests on that code being the vendor's (SYS-SEC-06), until SYS-SEC-13 and SYS-SEC-14; no board run reads or writes 0x08010000-0x08013FFF since the records moved there (B22 shows the PMP entry only)

### SYS-SEC-02: The V3F's RAM is fenced from the V5F's CPU
The V5F's CPU, in either mode, can't read or write the V3F's RAM, where
stage 1 handles keys (PMP entry 1, locked).

- **Parent:** UN-09
- **Verified by:** board:B23
- **Status:** partial
- **Gap:** B23 shows reads faulting, not writes; bus masters aren't fenced (as SYS-SEC-01)

### SYS-SEC-03: DMA only through Guarded DMA
User mode can't program the DMA controllers, the flash controller, USBFS
or the RNG; the card's DMA runs through a machine-mode service that only
ever points the channel at its own buffer and copies to or from checked
user buffers.

- **Parent:** UN-09
- **Verified by:** integ:blocksim::test_guard, unit:guard::test_buffer, board:B15, board:B17
- **Status:** partial
- **Gap:** other bus masters (the ECDC, SDMMC, USBHS, ...) aren't fenced (SYS-SEC-13, planned); SYS-SEC-06 covers what that leaves

### SYS-SEC-04: Machine mode's code can't be changed by user mode
The V5F's machine-mode code (the vector table, the handlers, the
services) runs from flash that user mode can't write, and calls nothing
outside itself.

- **Parent:** UN-09
- **Verified by:** ci:scripts/mmodecheck.py, ci:scripts/ch32build.sh::mmodecheck, board:B24
- **Status:** verified

### SYS-SEC-05: Lock-down
A production board has read protection on, the V3F's sectors write
protected and the boot ROM's downloads off; stage 1 checks this at every
boot, and at level 2 a board that isn't locked down never starts the
V5F. `lockdown` in recovery locks a board in one step.

- **Parent:** UN-08, UN-09
- **Verified by:** integ:stage1sim::s_lockdown, integ:stage1sim::s_lockdown_command, integ:stage1sim::lockdown.py run: a locked board refused before any flash, board:B8, board:B25
- **Status:** verified

### SYS-SEC-06: Only signed images start at level 2
At level 2 stage 1 starts the V5F only if the installed firmware and
flash pages match the hash in a package block 0 signed by a release key
the root certifies, checked at every boot; a board with no record or no
key goes to recovery. A forged MAC isn't enough.

- **Parent:** UN-08
- **Verified by:** unit:update::test_signed, integ:stage1sim::s_enroll, integ:stage1sim::a locked board with no record stays in recovery, board:B25
- **Status:** verified

### SYS-SEC-07: The security level is one build setting
`SECURITY_LEVEL` sets every security behavior of both cores; the flags
it replaced stop the build.

- **Parent:** UN-11
- **Verified by:** insp:src/target/CH32H417/Common/security.h::SEC_SIGNED_BOOT, board:B7, board:B25
- **Status:** verified

### SYS-SEC-08: Protected stores fault into Forth
A user-mode store to the DMA, flash controller or USBFS registers faults
to Forth (-127); stores to RCC and CRC, which share that block, are done for it.

- **Parent:** UN-09, UN-01
- **Verified by:** unit:guard::test_allowed, unit:guard::test_decode, board:B15
- **Status:** verified

### SYS-SEC-09: Debug tools don't unlock boards
The PC tools that drive the debug probe (`lfmcp.py`, `lockdown.py`)
never erase, unprotect or write a protected board.

- **Parent:** UN-09, UN-13
- **Verified by:** unit:lfmcp::wlink flash refused, integ:stage1sim::lockdown.py run: a locked board refused before any flash, integ:stage1sim::lockdown.py: both images flashed
- **Status:** verified

### SYS-SEC-10: A restricted wordlist at start-up
At level 2 the terminal starts with a limited wordlist, and the full
system needs an unlock.

- **Parent:** UN-08, UN-09
- **Status:** planned

### SYS-SEC-11: User mode can't steer machine mode
Every trap and interrupt switches to machine mode's own stack before it
stores anything (no hardware stacking at user mode's `sp`); machine mode
stores through user mode's pointers only into buffers it has checked, and
trusts none of its data between traps without checking it again.

- **Parent:** UN-09
- **Verified by:** board:B29
- **Status:** partial
- **Gap:** the fault handler is on the machine-mode stack (B29); the
  fast-interrupt handlers (SysTick1, the ecall services) still save at
  user mode's `sp` through WCH's hardware prologue, done with the PFIC and
  timer move (firmware-updates.md section 8, part 3)

### SYS-SEC-13: No bus master runs for user mode
User mode can't turn on the clock of any bus master (the ECDC, LTDC,
GPHA, HSADC, SDMMC, the USB controllers, USBPD, ETH, DVP, SerDes, UHSIF,
QSPI, FMC, PIOC, DMA1/2): the guard refuses such an RCC store, and the
start-up code turns them all off. Machine-mode services (Guarded DMA, the
ECDC) turn on only what they use, and only while they use it.

- **Parent:** UN-09
- **Status:** planned

### SYS-SEC-14: Machine mode's code is the root's
The code that runs in the V5F's machine mode (the slot's last 8 KB: the
`.mmode` window and `.ramfunc`'s flash copy) carries a signature by the
root key over its hash, embedded in the package's block 0. Stage 1 checks
it at every boot as well as the release signature, and hashes the flash
window against it, so a release key alone can't change machine-mode code.

- **Parent:** UN-08, UN-09
- **Verified by:** unit:seal::machine-mode root signature, unit:update::unmatched machine-mode signature, integ:stage1sim::strict: the signed image starts, board:B32
- **Status:** verified

### SYS-SEC-15: User mode can't reach the interrupt controller
The PFIC and the system timers (from 0xE0000000) are machine mode's
alone; user mode's time base is a general-purpose timer, and interrupt
enables, `reboot` and `standby` are machine mode's.

- **Parent:** UN-09
- **Verified by:** board:B30, board:B31
- **Status:** verified

### SYS-SEC-12: Keys only from a healthy RNG
Stage 1 makes the device token, and so the board's key, from RNG words
each checked for data ready, seed and clock errors and a repeat, hashed
together; a failure makes no key.

- **Parent:** UN-09
- **Verified by:** insp:src/target/CH32H417/V3F/User/stage1.c::RNG_FLAG_SECS, board:B25
- **Status:** partial
- **Gap:** no test makes the RNG fail; its quality on real parts isn't verified

## LOG: data logs

### SYS-LOG-01: Logs only the server can read
Each log is encrypted with keys from a one-off X25519 key and the
server's public key built into the signed firmware; the board keeps the
keys only in RAM while the log is open.

- **Parent:** UN-10
- **Verified by:** sys:datalog::test_round_trip
- **Status:** partial
- **Gap:** the format and keys are built and tested on the desktop (datalog.c, lflog.py); the server key in the firmware and the board side are not (SYS-LOG-07, SYS-LOG-08)

### SYS-LOG-02: The server knows which board, and that nothing changed
The header's tag needs the board's key (through stage 1), and every
record's tag the log's keys; a changed, reordered, spliced or foreign
record, a changed header, or the wrong board's key is reported.

- **Parent:** UN-10
- **Verified by:** sys:datalog::test_tampering
- **Status:** partial
- **Gap:** the header tag with the board key is tested with a desktop stand-in for stage 1; stage 1's service for it is SYS-LOG-08, planned

### SYS-LOG-03: The UID stays private
The board's UID is in the log only encrypted, readable only with the
server's key.

- **Parent:** UN-10
- **Verified by:** sys:datalog::test_round_trip, insp:doc/admin/data-logs.md::Identity
- **Status:** verified

### SYS-LOG-04: Power cuts lose at most what was being written
After a power cut, the next start closes each open log at its last whole
record without any key, deletes a log file whose header never got
written, never brings back a deleted log, and leaves the file system
consistent but for clusters no file owns.

- **Parent:** UN-10
- **Verified by:** sys:datalog::test_power_cuts, sys:datalog::test_stale
- **Status:** verified

### SYS-LOG-05: Log files the board makes
The board makes each log file itself under a given name, 4 GB long and
contiguous, refuses a name that's taken, and trims it when the log
closes, so any PC sees an ordinary file.

- **Parent:** UN-10
- **Verified by:** sys:datalog::test_many_logs, sys:datalog::test_round_trip
- **Status:** partial
- **Gap:** the file handling runs on the desktop on card images; not yet on the board

### SYS-LOG-06: No plaintext on the card
If the encryption engine fails, the record is wiped and never written.

- **Parent:** UN-10
- **Verified by:** integ:datalog::record not wiped
- **Status:** verified

### SYS-LOG-07: Two CODECs at 75 kSPS
Up to two stereo CODECs at up to 75 kSPS each, 24-bit, are recorded as
128-byte sample blocks in arrival order; when the buffers overflow, the
dropped sample blocks are counted in the log.

- **Parent:** UN-10
- **Status:** planned

### SYS-LOG-08: The board key's service for logs
Stage 1 gives the V5F `k_auth` for a log's one-off key, without the board
key ever leaving the V3F.

- **Parent:** UN-10, UN-09
- **Status:** planned

### SYS-LOG-09: Development keys are flagged
The server's tool warns when a log comes from a board whose key is the
development key derived from its UID.

- **Parent:** UN-10, UN-11
- **Verified by:** sys:datalog::test_lflog_edges
- **Status:** verified

## DISP: the display

### SYS-DISP-01: Text on an ST7789 LCD
The system drives an ST7789 LCD and draws text in anti-aliased bitmap
fonts kept in blocks, CJK included, with a cache in flash.

- **Parent:** UN-05
- **Verified by:** unit:lcd, sys:glyph, unit:mkfont
- **Status:** partial
- **Gap:** no board run logged

## TIME: time and power

### SYS-TIME-01: A real-time clock and the calendar
`rtc@` and `rtc!` read and set the clock in seconds, and the calendar
words convert them to and from dates and times.

- **Parent:** UN-06
- **Verified by:** sys:rtc::[time kept], sys:calendar, board:B27
- **Status:** verified

### SYS-TIME-02: Sleep until an alarm
`standby` sleeps with both cores stopped until the alarm, then restarts,
keeping the time; without an alarm set it refuses.

- **Parent:** UN-06
- **Verified by:** sys:rtc::[woke], board:B27
- **Status:** partial
- **Gap:** the refusal without an alarm isn't shown on the board; standby's current isn't measured

## TOOL: the PC tools

### SYS-TOOL-01: Keys and packages
`sealblocks.py` makes the root, release and test board keys, packages
and checks them, and writes the root key header for a build.

- **Parent:** UN-13, UN-07
- **Verified by:** sys:seal::package round trip, sys:seal::retired release key
- **Status:** verified

### SYS-TOOL-02: Blocks, fonts and card images
`putblocks.py`, `getblocks.py`, `putbin.py`, `mkfont.py` and
`mkexfat.py` put source and fonts into block files and back, and make
card images.

- **Parent:** UN-13, UN-04
- **Verified by:** sys:putblocks::round trip: same blocks, sys:putbin, sys:mkfont, integ:exfat::run_lf: lf finds lfblocks.fb4
- **Status:** verified

### SYS-TOOL-03: Driving a board
`hiltest.py` and `lockdown.py` run the update and lock-down tests on a
board and write a report; `lfmcp.py` gives an AI assistant the terminal
and a safe part of the debug probe.

- **Parent:** UN-13
- **Verified by:** integ:stage1sim::hiltest on the simulated board, integ:stage1sim::lockdown.py run: a locked board refused before any flash, unit:lfmcp::wlink flash refused, board:B6, board:B11
- **Status:** verified

### SYS-TOOL-04: Opening logs on the server
`lflog.py` makes the server key, writes the board's header for it, and
checks and decrypts logs.

- **Parent:** UN-13, UN-10
- **Verified by:** sys:datalog::test_lflog_edges
- **Status:** verified

### SYS-TOOL-05: Provisioning, in two passes
`provision.py` takes a blank board through pass 1 (flash the release,
lock it down, `enroll`, record the board) and, once the vendor has
packaged the release for the batch, pass 2 (install it and check it).
It refuses a protected or non-blank chip, a UID that's enrolled already
or isn't the one the probe read, and a public key another board has; it
never reads the chip past the V3F's image. The station holds no private
key: the vendor reviews each batch (`batch-review`) before its lines go
into `boards.csv`.

- **Parent:** UN-14, UN-09
- **Verified by:** integ:stage1sim::provision.py on the simulated board: passed, integ:stage1sim::pass1: three boards enrolled, integ:stage1sim::pass2: two boards done, integ:stage1sim::batch-review: the batch checks out
- **Status:** partial
- **Gap:** no board run; whether wlink prints the UID in enroll's byte order, and what a new chip's flash reads, are to be seen on one (doc/admin/provisioning.md section 10)

### SYS-TOOL-06: A signed release for the station
`sealblocks.py manifest` lists a release's files with their sha256 and
signs the list with a release key; the station checks it against the root
key before every batch, and refuses a changed or extra file.

- **Parent:** UN-14, UN-08
- **Verified by:** integ:seal::manifest: a file missing, integ:seal::manifest: another root, integ:seal::manifest: a changed image, integ:stage1sim::provision station-check: a changed image stops it
- **Status:** verified

### SYS-TOOL-07: A turnkey root-of-trust hardness test
`hardness.py` is one command a testing lab runs against a locked board to
prove as much of the root of trust's hardness as can be shown without
opening the chip or holding the signing keys: it reads the board's locked
state (the boot's report of the security level, the lock-down and the PMP,
and optionally the option bytes over wlink) and checks it is as a shipped
board's should be, and it throws bad inputs -- update packages made by
corrupting the lab's own good one, so no key is needed -- and checks each
is refused and the good image still boots. It never probes a fence from
inside to try to escalate; the locked-state readback is the proof a fence
holds. Physical attacks and attacks needing a private key are out of scope,
stated in its report.

- **Parent:** UN-13, UN-09
- **Verified by:** integ:stage1sim::the bad-input battery is refused by the reference verifier, board:B34
- **Status:** verified

## QA: the development process

### SYS-QA-01: Every change to main is built and tested
Every pull request to `main` builds lf and both of the board's cores,
fails on a warning in the cores' builds, and runs every test (CI). The
cores' CI builds use upstream GCC, not the MounRiver builds that are
flashed.

- **Parent:** UN-12
- **Verified by:** ci:.github/workflows/c-cpp.yml::ch32build, ci:.github/workflows/c-cpp.yml::make test
- **Status:** partial
- **Gap:** lf's own build doesn't fail on a warning (no -Werror)

### SYS-QA-02: A current bill of materials
The SBOM lists every third-party component with its files' checksums,
and fails the tests when a listed file changes or an unclaimed copyright
notice appears.

- **Parent:** UN-12
- **Verified by:** ci:src/target/desktop/unit_tests/sbom
- **Status:** verified

### SYS-QA-03: Board tests on record
Every security test on a board is logged with the firmware's hashes,
the steps, what was expected and the board's output word for word, and
an entry whose design has since changed is marked superseded.

- **Parent:** UN-12
- **Verified by:** insp:doc/admin/board-tests.md::B25, insp:doc/admin/board-tests.md::Superseded
- **Status:** partial
- **Gap:** some early entries lack hashes or part of the output (B2, B7, B8, B13, B20, B22)

### SYS-QA-04: A checked traceability matrix
Every requirement traces to its need and its evidence; every link is
checked to exist (a suite and a text in it, a board test that isn't
superseded, a file, a symbol, a heading), and the matrix is current at
every change. Whether the evidence shows its claim is reviewed, not
checked.

- **Parent:** UN-12
- **Verified by:** ci:src/target/desktop/unit_tests/rtm::test_rtm.py
- **Status:** verified

### SYS-QA-05: Forth source follows the coding rules
The Forth source's definitions carry their stack pictures and glossary
lines where the tools expect them.

- **Parent:** UN-01, UN-12
- **Verified by:** ci:src/target/desktop/unit_tests/lintf::lintf
- **Status:** partial
- **Gap:** lintf reports problems in forth/*.f without failing
