# Firmware updates: a root of trust in the V3F

How a CH32H417 board running LiteForth takes security patches to its C
firmware in the field, once its JTAG/SDI is gone for good (RDP level 2).
This is the design; nothing here is built yet. It applies
[security-plan.md](security-plan.md) (sections 5.1-5.6) to this chip and
board, with two changes: the root of trust is in the **V3F** core, and
staging and backups go on the **microSD card**, not on a NOR flash.

What's covered:

- **The V5F image**: LiteForth's C code and the product's own C code.
- **Stage 1** of the V3F: the USB bridge and the update code.

What isn't: the application. Forth source, fonts and data already arrive
as sealed block files ([encrypted-blocks.md](encrypted-blocks.md),
[updates.md](updates.md)), and the dictionary in flash is rebuilt from
them by `1 load`. Stage 0 itself never changes (section 1).

## 1. The chain of stages

| Stage | Where | Changes in the field? | Does |
|---|---|---|---|
| **Stage 0** | V3F, the first 24 KB of flash, under WRPR | Never | Checks a stage 1 slot's signature and starts it; if neither slot passes, tries the other, then stops |
| **Stage 1** | V3F, slot A or B | Yes, checked by stage 0 | The USB CDC bridge, the card, installing and rolling back, checking the V5F image, starting the V5F, recovery over USB |
| **V5F image** | V5F flash | Yes, checked by stage 1 | LiteForth |

This is how the industry squares an immutable root of trust with patches:
the immutable part does almost nothing, so it has almost nothing to get
wrong, and everything that faces the outside world (USB, the card, file
systems, YMODEM) is in stage 1, which can be patched. A bug in stage 0 can
never be fixed, on any board shipped (the boot ROM bugs in Nintendo's
Tegra and Apple's checkm8 are the usual examples), so stage 0 gets the
least code and the most review.

Two slots for stage 1 keep stage 0 small: stage 0 needs no USB, card or
YMODEM code to recover from a bad stage 1, because the other slot still
holds the last good one. Stage 1 installs a new stage 1 into the slot it
isn't running from.

### Stage 0, at every reset

1. Read the security sector's record (section 3). If it's blank, this is
   the first boot after an erase: make the device token (section 4) and a
   new record.
2. Pick the slot the record says is current; check its header's
   signature and its image hash (section 5). If it passes, and its
   version isn't below the record's minimum for stage 1, start it.
3. Otherwise try the other slot the same way. If that passes, record the
   switch and start it.
4. Neither: stop, and blink or print why on the debug UART. Only a
   debugger can fix this, so it must never happen in the field: a new
   stage 1 is installed into the other slot and confirmed (section 6)
   before the old one is ever replaced.

Measured: Ed25519 with SHA-512 (Monocypher) and BLAKE2s compile to 17.6 KB
for RV32IMAC at `-Os`. With the flash and record code, stage 0 is about
20 KB: three 8 KB sectors. One Ed25519 check takes about 6 ms on the V5F
at 400 MHz (`sealed_mount` does two and an X25519 in 18 ms), so 20-40 ms
on the V3F at 100 MHz; hashing a 64 KB slot with BLAKE2s adds about
30 ms.

### Stage 1, at every boot

1. **Finish any update** (section 6): a package waiting, an install cut
   short, or a new version on trial.
2. **Check the V5F image:** its MAC under the device token's image key
   (section 4), against the value stored when it was installed. A mismatch
   means the flash changed outside an install: don't start it; go to
   recovery (section 7).
3. **Start the V5F**, which locks itself down (section 8) and runs
   LiteForth.
4. **Run the USB bridge**, as today.

The V5F image is checked by MAC, not signature, at boot: it was checked
against its signature when it was installed, and the MAC only has to show
that the flash hasn't changed since. That makes the token's image key
something an attacker who can write flash must not be able to read
(section 8).

## 2. Flash layout

The code flash in dual-flash mode: 960 KB in 8 KB sectors, the V3F
booting at 0x00000000 and starting the V5F at an address of its choice.
Proposed:

| Address | Size | Region | WRPR |
|---|---|---|---|
| 0x00000 | 24 KB | Stage 0 | Yes |
| 0x06000 | 8 KB | Security sector, copy A | No |
| 0x08000 | 8 KB | Security sector, copy B | No |
| 0x0A000 | 72 KB | Stage 1, slot A: a 4 KB header, then up to 68 KB | No |
| 0x1C000 | 72 KB | Stage 1, slot B | No |
| 0x2E000 | 8 KB | Reserved | No |
| 0x30000 | 160 KB | V5F image | No |
| 0x58000 | 256 KB | LiteForth flash pages 0-3 (the dictionary) | No |
| 0x98000 | 8 KB | The V5F's board record (sealed files' X25519 key, minimums) | No |
| 0x9A000 | 344 KB | Free | |

Everything the V3F owns is below 0x30000, so one PMP entry can fence it
off (section 8). This moves the V5F (now at 0x10000) and LiteForth's flash
(now at 0x30000), so it changes both cores' linker scripts and `flash.c`. Sizes: today's V3F image (`LiteForth_V3F.bin`) is 17.3 KB.
Stage 1 adds about 25-30 KB (SD driver, `card.c`/`exfat.c`, `sealed.c` with
Monocypher, the installer), so about 45-50 KB, in a 68 KB slot. Stage 1
runs from the V3F's 64 KB of RAM_CODE, which caps it at 64 KB of code
whatever the slot size.

Each slot starts with the 4 KB header of the package that installed it
(section 5), so stage 0 checks the slot exactly as it was signed.

## 3. The security sector

Written only by stage 0 and stage 1. Two copies, so a power cut during an
erase or a write never loses the record: each copy has a sequence number
and a BLAKE2s hash (person `LFrecord`); the newer valid copy wins, and a
write always goes to the other one (even sequence numbers in copy A, odd
in B). The hash only catches a torn write. It can't be a MAC under the
token, because the token is in the record: whoever can read the record
can forge its MAC. What keeps the record safe is that nothing but the
boot stages can write the sector (section 8).

`src/update.c` keeps it (`upd_record` in `update.h`); the fields so far:

| Field | Contents |
|---|---|
| Magic, format, sequence | |
| Device token | 32 bytes (section 4) |
| Stage 1 | Current slot; each slot's version and state (empty, on trial, confirmed). Not yet in `update.c` |
| V5F image | Version, length, MAC |
| Minimums | Lowest version allowed for the V5F image (and later stage 1); lowest release key serial |
| Update state | Idle, backing up, installing (blocks done), on trial (boots so far), rolling back (blocks done); the package under way (version, length, hash, new minimum) |
| Backup | What `lfbackup.bin` holds: version, length, MAC |
| Results | The last update's outcome (installed, refused, no room for a backup, rolled back, broken), for `attest` and the terminal |

## 4. The device token

32 random bytes made by stage 0 the first time it finds both copies of the
security sector blank, from the RNG after the health checks `board.c`
already does. It never leaves the chip. An erase by a debugger makes a new
one, which is right: everything the old one protected is then worthless.
An `INSECURE` build derives it from the UID instead, as it does the
board's X25519 key.

Each use gets its own key, so one never reveals another:

```
K_img    = BLAKE2s(key=token, person="LFimage")    V5F image MAC (and stage 1's, if wanted)
K_encard = BLAKE2s(key=token, person="LFcardenc", digest_size=16)   AES on card files
K_maccard= BLAKE2s(key=token, person="LFcardmac")  MACs on card files
```

The image MAC is `BLAKE2s(key=K_img, person="LFimgmac")` over the
version (4 bytes), the length (4 bytes) and the image. The backup's MAC
is `BLAKE2s(key=K_maccard, person="LFbackup")` over every block of
`lfbackup.bin` as written: block 0 (`LFBACKUP`, version, length, a random
8-byte CTR nonce) and the image encrypted with AES-128-CTR under K_encard.

The token protects what the board itself writes: the MAC of the
installed image, and backups on the card. It isn't needed for update
packages, which the release key signs.

## 5. The update package

A sealed block file ([encrypted-blocks.md](encrypted-blocks.md) section 3)
with `F` instead of `S` at offset 9, whose content blocks are the firmware
image. Everything else is unchanged: the root key certifies the release
key, the release key signs block 0, each content block is checked against
the BLAKE2s hash table, and the image is encrypted with a CEK wrapped for
each board in `boards.csv`. So stage 1 reuses `sealed.c`, and
`sealblocks.py` gains one command, `package`.

The 12 reserved bytes at offset 180 describe the image:

| Offset | Size | Contents |
|---|---|---|
| 180 | 1 | Component: 1 = V5F image, 2 = stage 1 |
| 181 | 1 | Flags, 0 |
| 182 | 2 | Hardware revision, 0 = any |
| 184 | 4 | Image length in bytes |
| 188 | 4 | New minimum version (anti-rollback): 0 = leave it |

The revision field at offset 20 is the image's version. A package for one
board only (from support) lists just that board as a recipient.

Bytes 4000-4031, just before the signature, hold the plaintext image's
hash: `BLAKE2s(image, digest_size=32, person="LFimage")`, over exactly
the image's length. So the table hashes and recipients must end by 4000
(32 bytes less room than a sealed block file). The image is padded with
0xFF (erased flash) to whole blocks before it's encrypted.

The hash is what makes an installed image checkable without the board's
key. A stage 1 slot holds the package's block 0 followed by the decrypted
image, so stage 0 checks the slot with block 0's signature and this hash.
Stage 1 also checks the decrypted V5F image against it before it stores
the image's MAC.

Made by `sealblocks.py package IMAGE PACKAGE --component v5f|stage1
--release R --boards CSV --revision N [--hw-rev N] [--min-version N]
[--cipher aes|sm4]`; `check` and `open` read packages too (`open` writes
the image itself and checks its hash). A package is never a block file:
`lf` and the board offer nothing from one (capacity 0, "a firmware update
package, not a block file"), and `-1 ymodem` refuses it
(`PACKAGE_MAGIC` in `sealed.h`).

## 6. Installing an update

The card holds two files, made once in a PC and contiguous like
`lfblocks.fb4`, so `card.c` finds them and nothing ever writes the file
system:

- `lfstage.bin`: an update package, exactly as it came;
- `lfbackup.bin`: the V5F image as it was before the last update.

| Phase | Runs in | Trusted? | Does |
|---|---|---|---|
| A. Fetch | V5F | No | `-2 ymodem` (kwai: `~x FILE -2`) writes a package into `lfstage.bin`, block 0 last; reads it back; checks it as stage 1 will (`blk_stage_check`, all but the minimum version); if it passes, sets `update` in the shared SRAM block to `CDC_UPDATE_MAGIC` (`blk_stage_ready`). Or the package is copied there in a PC. Then a reset starts the install. Built; stage 1 isn't yet, so nothing acts on the flag |
| B. Check | Stage 1 | Yes | Block 0 as `sealed_mount` checks it, plus the component, the hardware revision, and the version against the minimum. Then every content block against its hash. Any failure: drop it, record why, boot as before |
| C. Back up | Stage 1 | Yes | The current V5F image into `lfbackup.bin`, encrypted with K_encard and MACed with K_maccard, with its version; read back and check before going on |
| D. Install | Stage 1 | Yes | Erase and program block by block, reading each block from the card again and checking its hash just before it's programmed, so a card changed after phase B can't slip anything in; read back; store the new MAC and version |
| E. Trial | V5F, then stage 1 | Checked | The new V5F confirms it's working (`confirm`, or LiteForth reaching its first `ok>`, by a flag in the shared SRAM block). Then stage 1 marks it confirmed and raises the minimum if the package asks |
| F. Roll back | Stage 1 | Yes | No confirmation within N boots (**Decide** N), or a watchdog reset during the trial: restore `lfbackup.bin` after checking its MAC and version, record "rolled back" |

A new stage 1 goes into the other slot instead: phase C isn't needed, and
the trial is stage 0 starting the new slot, with the old slot as the
fallback.

`src/update.c` does phases B to F for the V5F image (`upd_boot`, and
`upd_confirm` for the confirmation), through callbacks for the flash and
the two card files, so it runs on the desktop. `unit_tests/update` tests
it with simulated flash and packages from `sealblocks.py package`:
installing, confirming, the minimum, rolling back, restoring a changed
image from the backup, every refusal, a block changed between the check
and programming, a package swapped mid-install, and a power cut (a torn
write or half-erased sector) at every single step of an install and of a
rollback. After any cut the board ends up running exactly the old image
or the new one.

Three rules came out of writing it:

- A rollback never restores a backup older than the board's minimum: it
  records "broken" and goes to recovery instead.
- An image on trial with no backup (the first install on a board with
  nothing installed) just stays when its trial runs out, since there's
  nothing to go back to.
- A board with a blank record (fresh from a debugger) has no installed
  image, so stage 1 goes to recovery. `adopt_blank`, for development
  only, takes whatever is in the V5F slot as installed instead.

Power cuts: every phase records its progress in the security sector before
it starts. Phase D resumes from the block it reached, since `lfstage.bin`
is kept until the trial ends. A card pulled during phase D leaves a
half-programmed V5F image, which fails its MAC at the next boot: stage 1
goes to recovery (section 7) and offers to finish from the card or take
the package over USB.

## 7. Recovery

Stage 1 runs the USB bridge itself, so it can take a package over USB when
the V5F can't run: no V5F image, one that fails its MAC, or one that
failed its trial and has no backup. In recovery stage 1 answers on the USB
terminal with a short message, and kwai's `~x FILE -2` sends the package by
YMODEM straight to stage 1, which checks and installs it block by block as
in phase D (block 0 first, then each block against its hash before it's
programmed). After RDP 2 this is the only way to fix a board whose V5F
image is broken, so it has to work before any board is locked.

## 8. Keeping the V5F out

The V5F runs code that faces the card and the terminal, so it mustn't be
able to change stage 1, the security sector or the record, or read the
token.

- **Forth:** `VM_IO_PAGE` maps all the peripherals today. In a production
  build it's an allowlist without the flash controller, the DMA
  controllers, SDIO, the ECDC, the RNG, RCC/PWR and the option bytes
  ([security-plan.md](security-plan.md) section 5.6).
- **The V5F's C code:** PMP. LiteForth already runs in user mode (see
  *What the board showed*, below), and only machine-mode code can change
  the PMP: in the V5F image that's the start-up code before its `mret`
  into `main`, and the trap handlers. So the start-up code sets the
  entries, locked, and nothing in LiteForth can undo them. The last
  entry, locked, opens all memory (RWX) to everything the others don't
  fence off. LiteForth's own flash writes (`close-flash`, the board
  record) then can't reach the flash controller directly: they become an
  `ecall` service in machine mode that checks what's written where. There
  are only four entries per core (NAPOT or TOR), so they go to what
  matters most:
  1. the V3F's flash: stage 0, the security sector and both stage 1
     slots, 0x00000-0x2FFFF. Entry 0 in TOR mode covers 0 up to its
     address, so that's one entry. The same flash also appears at
     0x08000000 (LiteForth's pages are reached there), and that window
     needs fencing too: **Verify** how the two windows map and whether
     one more entry is needed;
  2. the flash controller's registers;
  3. the V3F's RAM (0x20100000-0x20177FFF, its running code and data:
     the V5F could otherwise patch stage 1 in RAM), leaving the shared
     32 KB at 0x20178000;
  4. the locked catch-all: all memory, RWX.

  That leaves nothing for the RNG or the option bytes; the IO allowlist
  above keeps Forth away from them, and the C code is trusted.
- **What PMP doesn't stop:** DMA. A DMA controller isn't the core, so the
  V5F's C code could still use DMA to read the token. So the V5F's C code
  is trusted (it's signed and checked), and PMP is a second line against
  bugs in it. The V5F's own SDIO driver uses DMA; it stays in C.
- **When it's set:** the first thing the V5F image does, so it depends on
  the image being good, which stage 1 has just checked. A stricter
  alternative: stage 1 starts the V5F at a small shim in stage 0 that sets
  the entries and then jumps to the image. **Verify** that the V5F can run
  code from the V3F's flash.

The V3F keeps its own PMP unlocked: it's the trusted side.

### What the board showed (`pmp-test`, 18 October 2026)

Runs of `pmp-test` (PR #164, #165; the word was removed afterwards, since
the version on `main` faulted at once in user mode) on the nanoCH32H417 at
RDP 0:

- **LiteForth runs in user mode.** The first `csrr pmpcfg0` faulted with
  mcause 2 (illegal instruction, mtval `3A0025F3`). WCH's start-up code
  writes `mstatus = 0x6088`, MPP = 0, before its `mret` into `main`, so
  `main` and everything after it run in user mode. The V3F's start-up
  does the same. Machine mode then runs only in trap handlers.
- **An `ecall` from user mode has its own vector:** entry 8 of the V5F's
  table, `Ecall_U_Mode_Handler` (and 5, `Ecall_M_Mode_Handler`, for one
  from machine mode), not `HardFault_Handler`. WCH's start-up code points
  both at a weak handler that loops forever, so an `ecall` hangs until a
  handler is defined. A plain `__attribute__((interrupt))` handler works:
  `pmp-test`'s ran, read and wrote the PMP CSRs, and returned.
- **The PMP starts unused:** `pmpcfg0` reads 0 after reset; the
  `pmpaddr` registers hold junk (not the manual's 0).
- **A locked entry applies to machine mode.** With entry 2 locked over a
  32-byte buffer with no access, a machine-mode load of the buffer
  faulted.
- **A fault inside a trap handler locks the core up** (the reset flags
  say LOCKUP). That machine-mode fault came while the `ecall` handler was
  running, and the core reset instead of taking it. So machine-mode code
  must never touch what a locked entry denies, and faults can't be caught
  and skipped from inside a handler.
- **A locked entry open to all memory (RWX) is harmless:** machine mode
  carried on normally with it locked. That's the catch-all in the list
  above.
- **Not settled:** whether user-mode accesses are checked, and whether a
  user-mode access that matches no entry fails (the RISC-V spec says it
  does once any entry is set). The test made those accesses from machine
  mode with MPRV set, as user mode, and none faulted, even with no entry
  open to them: this core most likely ignores MPRV. They need probes from
  real user mode, one per run, letting `HardFault_Handler` report the
  fault and reset.
- **Not yet run:** the code flash's two windows, the V5F reading the
  V3F's RAM, and the V5F running code in the V3F's flash.

## 9. Testing, without bricking anything

1. **Desktop.** Stage 0 and stage 1's update logic in plain C, reaching
   flash and the card through callbacks (as `sealed.c` reads blocks), with
   unit tests: every refusal, a power cut at every step of every phase,
   both copies of the security sector, rollback, recovery. `sealblocks.py
   package` and its tests. Done for the V5F image (`src/update.c`,
   `unit_tests/update`); still to do: stage 0, a new stage 1 into its
   other slot, and recovery over USB. `unit_tests/vmasm`'s RV32 emulator runs the
   V3F code as built, for size and speed.
2. **This board, RDP 0, no WRPR.** Phases A-F for real, then recovery.
   Anything that goes wrong is fixed with the WCH-Link.
3. **The spare board.** WRPR on stage 0, then RDP 2; then an update, a
   rollback and a recovery over USB. Only this step can't be undone.

## 10. To verify or decide

- **Verify** how the code flash's two windows (0x00000000 and 0x08000000)
  map, for the PMP entries in section 8.
- ~~**Verify** that a locked PMP entry applies in machine mode.~~ It
  does, on the V5F (section 8, *What the board showed*); the V3F not yet
  tested.
- **Verify,** from real user mode: that user-mode accesses are checked,
  and whether one that matches no entry fails.
- **Verify** whether the clearing of a locked entry is ignored (the test
  locked up before it got that far).
- **Verify** that the V5F can execute from the V3F's flash (for the shim
  in section 8).
- **Verify,** on the spare board only: that under RDP 2 nothing clears
  WRPR or rewrites the option bytes, and that USB and the card still work.
- **Verify** the RNG on real parts before trusting the token to it.
- **Decide:** the slot sizes; N trial boots; whether stage 1 is encrypted;
  `confirm` explicit or automatic; what stage 0 does when both slots fail
  (blink a code, print on the debug UART, or both).
- The download erase (`"clearcodeflash": false` still erases everything)
  stops mattering after RDP 2, but until then it wipes the security
  sector and so the token on every download, which re-makes the token and
  orphans card backups: expected during development.
