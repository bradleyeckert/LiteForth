# Firmware updates: a root of trust in the V3F

How a CH32H417 board running LiteForth takes security patches to its C
firmware in the field, once its JTAG/SDI is gone for good (RDP level 2).
This is the design; it applies
[security-plan.md](security-plan.md) (sections 5.1-5.6) to this chip and
board, with two changes: the root of trust is in the **V3F** core, and
staging and backups go on the **microSD card**, not on a NOR flash.

What's covered:

- **The V5F image**: LiteForth's C code and the product's own C code.
- **Stage 1** of the V3F: the USB bridge and the update code.
- **The application**: LiteForth's flash pages, as a developer built
  them from plain block files with `1 load`, in an app package with the
  V5F image (section 5). (Sealed block files carried the application
  before; they are retired, [encrypted-blocks.md](encrypted-blocks.md).)

Stage 0 itself never changes (section 1).

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
   (section 4), against the value stored when it was installed, and at
   `SECURITY_LEVEL` 2 its signature too (below). A mismatch
   means the flash changed outside an install: don't start it; restore the
   backup, or go to recovery (section 7).
3. **Start the V5F**, which locks itself down (section 8) and runs
   LiteForth.
4. **Run the USB bridge**, as today.

Below level 2 the V5F image is checked by MAC alone at boot: it was
checked against its signature when it was installed, and the MAC only has
to show that the flash hasn't changed since. That makes the token's image
key something an attacker who can write flash must not be able to read
(section 8), and the V5F's other bus masters (the ECDC, SDMMC, USB, ...)
could read it if code that isn't ours ran there.

**At level 2 the boot check needs the signature** (`upd_io.check_signature`,
`SEC_SIGNED_BOOT`), so it doesn't rest on the token staying secret.
Stage 1 keeps the installed package's block 0 in its own sector
(0x70000, `UPD_HEADER`) and checks it at every boot with
`sealed_check_signed` (`src/sealed.c`): the root key's certificate of the
release key, the release key's signature over block 0, the product, the
hardware revision, the version and release key serial against the
record's minimums, and that it describes the installed version and
length. Then it hashes the slot and the pages and compares the hash with
block 0's. Only the root's private key and the release key (both off the
board) can make a block 0 that passes, so the sector needs no protection:
anything written there, or into the slot or pages, by anything but an
install of a genuine package fails the check and never starts. Stage 1
restores the backup, which keeps that block 0 too, or goes to recovery.

What a leaked token still allows: writing the record, so an older genuine
release could be made to start (rollback below the minimum version), or the
board's key changed (it stops taking its packages). Neither runs code the
release key didn't sign. Stage 1 itself is write-protected (section 8),
and clearing that protection means removing read protection, which erases
the flash, the token with it.

Only app packages (component 3) install at level 2: a V5F-only package
would leave the flash pages unsigned. Checking two Ed25519 signatures and
hashing 368 KB adds to each boot (to be measured on the board).

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
| 0x98000 | 352 KB | Free | |

Everything the V3F owns is below 0x30000, so one PMP entry can fence it
off (section 8). The board's X25519 key for packages is one of the device
token's keys (section 4), so it lives in the security sector too. This
moves the V5F and LiteForth's flash, so it changes both cores' linker
scripts and `flash.c`. Today's layout, a step towards it, is in
section 6. Sizes: today's V3F image (`LiteForth_V3F.bin`) is 17.3 KB.
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
A build below `SECURITY_LEVEL` 2 derives it from the UID instead, as it
does the board's X25519 key.

Each use gets its own key, so one never reveals another:

```
K_img    = BLAKE2s(key=token, person="LFimage")    V5F image MAC (and stage 1's, if wanted)
K_encard = BLAKE2s(key=token, person="LFcardenc", digest_size=16)   AES on card files
K_maccard= BLAKE2s(key=token, person="LFcardmac")  MACs on card files
K_board  = BLAKE2s(key=token, person="LFboard")    the board's X25519 private key, at level 2
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

A file in the sealed format ([encrypted-blocks.md](encrypted-blocks.md)
section 3), marked `F` at offset 9 (`LITEFORTHF`), whose content blocks
are the firmware image: the root key certifies the release key, the
release key signs block 0, each content block is checked against the
BLAKE2s hash table, and the image is encrypted with a CEK wrapped for
each board in `boards.csv`. Stage 1 opens it with `sealed.c`, and
`sealblocks.py package` makes it.

The 12 reserved bytes at offset 180 describe the image:

| Offset | Size | Contents |
|---|---|---|
| 180 | 1 | Component: 1 = V5F image, 2 = stage 1, 3 = V5F image and LiteForth's flash pages ("app") |
| 181 | 1 | Flags, 0 |
| 182 | 2 | Hardware revision, 0 = any |
| 184 | 4 | Image length in bytes |
| 188 | 4 | New minimum version (anti-rollback): 0 = leave it |

The revision field at offset 20 is the image's version. A package for one
board only (from support) lists just that board as a recipient.

Bytes 4000-4031, just before the signature, hold the plaintext image's
hash: `BLAKE2s(image, digest_size=32, person="LFimage")`, over exactly
the image's length. So the table hashes and recipients must end by 4000. The image is padded with
0xFF (erased flash) to whole blocks before it's encrypted.

The hash is what makes an installed image checkable without the board's
key. A stage 1 slot holds the package's block 0 followed by the decrypted
image, so stage 0 checks the slot with block 0's signature and this hash.
Stage 1 also checks the decrypted V5F image against it before it stores
the image's MAC.

**App packages (component 3)** carry the Forth application with the
firmware. The image is the V5F's slot, the `.bin` padded with 0xFF to the
slot's size, followed by all of LiteForth's flash pages: one region in the
code flash (0x14000 to 0x6FFFF today), which stage 1 installs, backs up,
rolls back and checks like a V5F image, just longer. Stage 1 takes one
only if it's exactly that long. The way an application gets to a product:

1. A developer, on a board at `SECURITY_LEVEL` 0 or 1, loads the Forth
   source from a plain block file (`1 load`), which compiles into the
   flash pages, caches fonts (`font-cache`), and so on.
2. `save-pages` writes the pages into `lfpages.bin` on the card (made
   beforehand like `lfstage.bin`: 262144 bytes, contiguous).
3. The administrator packages them with the V5F's build:
   `sealblocks.py package LiteForth_V5F.bin app.lfu --component app
   --pages lfpages.bin --release R --boards CSV --revision N` (`--slot` and
   `--pages-len` give another layout).
4. The package installs as any other, by `-2 ymodem` or recovery.

At `SECURITY_LEVEL` 2 (`SEC_LOCKED_PAGES`) only packages write the pages:
`open-flash`, the V5F's machine-mode flash service and `save-pages`
refuse. Stage 1 counts every installed image there as the whole region,
slot and pages (`upd_io.check_pages`): its MAC, checked at every boot,
covers the pages, its backup holds them, and a rollback restores them. So
a changed page is caught like a changed V5F image. An image installed
without that (a record from a lower level, its MAC over the slot only)
fails the check, and a slot-only backup isn't trusted for a rollback,
since it would leave the pages as they are. At levels 0 and 1 the MAC
covers the V5F image only, so a developer can go on changing the pages
after an app package.

Made by `sealblocks.py package IMAGE PACKAGE --component v5f|stage1|app
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
- `lfbackup.bin`: the V5F image as it was before the last update; with
  the flash pages too, after an app package or at `SECURITY_LEVEL` 2
  (`1 + 92` blocks for today's 112 KB slot and 256 KB of pages, and at
  level 2 one more for the image's signed block 0: 94 blocks, 385024
  bytes).

| Phase | Runs in | Trusted? | Does |
|---|---|---|---|
| A. Fetch | V5F | No | `-2 ymodem` (kwai: `~x FILE -2`) writes a package into `lfstage.bin`, block 0 last; reads it back; checks it as stage 1 will (`blk_stage_check`, all but the minimum version); if it passes, sets `update` in the shared SRAM block to `CDC_UPDATE_MAGIC` (`blk_stage_ready`). Or the package is copied there in a PC. Then a reset starts the install. In recovery, stage 1 takes the package itself (section 7) |
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

These rules came out of writing it:

- A rollback never restores a backup older than the board's minimum: it
  records "broken" and goes to recovery instead.
- At level 2 the installed package's block 0 goes with the image: stored
  at `UPD_HEADER` after the install and checked there as the boot check
  will check it (a failure rolls back), copied into the backup after the
  image, and restored with it. A backup without it isn't trusted there.
- Only an installed image that passes its MAC check (and at level 2 its
  signature) is backed up. An image
  that changed in flash since its MAC was made mustn't get a backup that a
  rollback would then trust. Without a new backup, the one already on the
  card, if any, stays the one to roll back to.
- No working image means recovery, and the board stays in recovery until a
  package installs. That covers an image that fails its MAC, one whose trial
  runs out with no backup, and a rollback that can't be done. The record
  marks it with `v5f_length` 0 and result "broken". An image that never
  confirmed is taken as not working, even though its MAC is good.
- A board with a blank record (fresh from a debugger) has no installed
  image, so stage 1 goes to recovery. `adopt_blank`, for development
  only, takes whatever is in the V5F slot as installed instead, and
  `upd_adopt` does the same later, from recovery.
- At `SECURITY_LEVEL` 0 and 1, `adopt_blank` also covers an image that
  fails its check: the debugger reprogrammed the V5F. Stage 1 adopts it as
  version 0 (ending a trial, keeping the backup) and starts it, instead of
  restoring the backup or going to recovery, so developers can re-flash at
  will and only level 2 enforces the check. It says so on the debug port
  (`update: the V5F image changed (the debugger?); adopted as version 0`),
  as it now reports anything a boot did, a rollback included.

Power cuts: every phase records its progress in the security sector before
it starts. Phase D resumes from the block it reached, since `lfstage.bin`
is kept until the trial ends. A card pulled during phase D leaves a
half-programmed V5F image. Stage 1 then stops with an error and doesn't
start it, because the record says it was programming. It goes to recovery
(section 7), where `reset` carries on once the card is back.

### On the board now (first step)

`V3F/User/stage1.c` runs this on the V3F at every reset, before it wakes
the V5F, with today's flash map (`stage1_layout.h`):

| Address | Size | Region |
|---|---|---|
| 0x00000 | 64 KB | The V3F's image (stage 0 and 1) |
| 0x10000 | 8 KB | Security record, copy A |
| 0x12000 | 8 KB | Security record, copy B |
| 0x14000 | 112 KB | The V5F's slot |
| 0x30000 | 256 KB | LiteForth's flash pages |
| 0x70000 | 8 KB | The installed package's block 0, signed (level 2's boot check) |
| 0x72000 | | Free |

The V5F's PMP entry 0 covers everything below its slot (section 8). It
does nothing at all on a board that has never been updated
(no record and no package waiting), so such a board boots as before,
below level 2. At level 2 it goes to recovery instead, and so does a board
whose record can't be read or that has no key yet: there the V5F starts
only after the boot check passes, so erasing the record sectors doesn't
start whatever is in the slot. A freshly locked level 2 board therefore
waits in recovery for `enroll` and its first app package.
Otherwise it opens the card (WCH's polling SD driver) and runs
`upd_boot`, then wakes the V5F. When LiteForth starts, the V5F sets
`update` in the shared block to `CDC_CONFIRM_MAGIC`, and the V3F's USB loop
(`stage1_poll`) confirms an image on trial.

For development, and until the rest exists:

- A blank record takes the V5F image in flash as installed, version 0,
  its whole 112 KB slot (`adopt_blank`), so the first update has
  something to back up and roll back to. `lfbackup.bin` must hold at
  least 29 blocks (116 KB).
- No working V5F image: recovery (section 7), on the USB terminal.
- Only stage 1 holds the board's private key: K_board (section 4) at
  `SECURITY_LEVEL` 2, from the UID below it. The root key and product come
  from `V5F/User/sealed_root.h`, which both projects build with. The V5F
  checks a package for `-2 ymodem` without the key
  (`sealed_verify_package`: signature, certificate, product, that this
  board is a recipient, every block's hash), and stage 1 does the rest.
  Forth's `enroll` asks stage 1 for the public key through the shared
  block (`enroll`, `board_pub`).
- At level 2 a board with no record makes one, and so its key, with
  `enroll` in recovery (section 7). From then on it starts only an image
  that stage 1 installed, so a new board goes: lock it down, `enroll` in
  recovery, its line into `boards.csv`, a package over `-2 ymodem`.

## 7. Recovery

Stage 1 runs the USB bridge itself, so it can take a package over USB when
the V5F can't run: no V5F image, one that fails its MAC, or one that
failed its trial and has no backup. In recovery stage 1 answers on the USB
terminal with a short message, and kwai's `~x FILE -2` sends the package by
YMODEM straight to stage 1, which checks and installs it block by block as
in phase D (block 0 first, then each block against its hash before it's
programmed). After RDP 2 this is the only way to fix a board whose V5F
image is broken, so it has to work before any board is locked.

### Recovery on the board now

`stage1_boot` returns `STAGE1_RECOVERY` when `upd_boot` finds no working
image. It also does so when an error stops it part way through
programming. Then `main.c` doesn't wake the V5F, and `stage1_poll`, in the
V3F's USB loop, is the other end of the USB rings in its place: it reads
what the host sends from the rx ring and writes into the tx ring, while
`cdc_bridge_poll` moves the bytes to and from USB as usual. Ctrl+X x3 is
off meanwhile (`binary`).

When a terminal opens the port, stage 1 says why it's in recovery and
prompts with `recovery ok>`, so kwai paces its lines as usual. It takes
these commands:

- `-2 ymodem`, which is what kwai's `~x FILE -2` types. The package comes
  by YMODEM (`src/ymrecv.c`, a small receiver tested by
  `unit_tests/ymrecv`) into the card's `lfstage.bin`, block 0 last. Then
  `upd_boot` runs as if the package had been staged by the V5F. It
  checks the whole package, installs it without a backup (there's nothing
  worth keeping) and resets the chip, so the new image starts on trial as
  usual. A refusal is reported and leaves the board in recovery.
- `reset`, to try again, for example with the card back in.
- `enroll`, which prints the board's `boards.csv` line. At
  `SECURITY_LEVEL` 2, on a board with no record, it first makes the
  record, with a new device token from the RNG and so the board's key
  (section 4); after that the line never changes until the record is
  erased.
- `adopt`, at `SECURITY_LEVEL` 0 and 1 only (`Common/security.h`; it
  also
  turns on `adopt_blank`). It takes the image in flash as it is
  (`upd_adopt`), for a V5F that the debugger reprogrammed without erasing
  the record.

With no `lfstage.bin`, or no card at all, the package goes into RAM
instead: 32 blocks (128 KB) of the V3F's spare RAM, enough for the
largest V5F package, which `upd_boot` then reads as it would read
`lfstage.bin`. An app package (about 375 KB) doesn't fit: it needs
`lfstage.bin`, and the receiver refuses it otherwise. That isn't the block-by-block install planned above, since
a package's hash table comes after its content, so no block could be
checked as it arrives. A reset part way through an install from RAM loses
the package: the install can't resume, so the board ends up in recovery
again (there's no backup to roll back to) and asks for it once more.

**Getting into recovery when the V5F is stuck.** The trial catches a new
image that never gets LiteForth going, but an image that was confirmed
can still get stuck later, in its C code before LiteForth, for example
on a card it can't handle. Then the USB terminal is up (the V3F runs it),
but nothing answers. Ctrl+X x3 already restarts the V5F without the app.
If Ctrl+X x3 comes again while that safe start still hasn't reached
LiteForth (`safe_boot` is still set: the V5F clears it after its memory
setup), the V3F sets `recover` in the shared block and resets the chip.
Stage 1 then starts in recovery and says why. A package sent then
replaces the stuck image: `upd_mark_broken` first marks it as not
working, so the package installs without a backup, and a failed trial
afterwards still rolls back to the backup on the card, if any. The mark
comes as soon as a package has arrived, so if that package is then
refused, the board stays in recovery until a good one comes. `reset`
before any package just tries the V5F again.

**A V5F that doesn't start at all.** The V3F used to sleep in Stop mode
until the V5F woke it, which an image that never starts never does. The
board then stayed dead until someone pressed reset. `stage1_wait_v5f`
now waits up to `STAGE1_V5F_START_MS` (3 s) for the V5F to release HSEM0,
which its `main` does first, and otherwise resets the chip, counting the
boot in `nostart` in the shared block. So an image on trial uses up its
trial boots by itself and is rolled back, with nobody pressing reset. An
installed image that stops starting gets recovery after
`STAGE1_START_TRIES` (3) such boots in a row. In recovery, a package then
replaces it as after Ctrl+X x3 twice, and `reset` tries it again. Boots
with an update under way don't count, since the trial counts those. A
debugger holding the V5F at its start would count too, so set
`STAGE1_V5F_START_MS` to 0 for that.

`reboot` resets the chip from LiteForth, as the reset pin does. With it,
nothing outside the board is needed to drive any of this: no reset
button, no debug probe.

Watch out when developing: on a board that has a record, a V5F build
downloaded by the debugger fails its MAC. If `lfbackup.bin` holds a good
backup, stage 1 restores that over the new build; otherwise it goes to
recovery, where `adopt` takes the new build. A download that erases the
whole chip also erases the record, so the new build is adopted as on a
fresh board.

### The lock-down check

At every boot, before anything else, stage 1 reads the option bytes as
`FLASH->OBR` and `FLASH->WPR` have them, and checks that:
- read protection is on (`RDPRT`);
- the V3F's own sectors, below the V5F slot, are write-protected;
- every sector above them is writable: the V5F slot, LiteForth's flash
  pages and the record sectors, which updates and
  LiteForth write;
- the boot ROM's USB and USART downloads are off (`USBFSDL`, `USARTDL`).

The masks are in `stage1_layout.h`. Anything else gives one line on the
debug UART, `V3F security: not locked down: ...`, listing what's wrong,
with the two registers. A build at `SECURITY_LEVEL` 2
(`Common/security.h`) also refuses to start the V5F on such a board. It goes into recovery,
which says why and takes only `reset`, so a production image can't run
on a board that was never locked.

The check only reads. Locking a board is stage 1's `lockdown`, in a level
2 build's recovery console, which programs the option bytes as the check
wants them (one erase and one pass, with the values WCH's
`FLASH_EnableWriteProtection`, `FLASH_UserOptionByteConfig` and
`FLASH_ReadOutProtection` would leave), reads them back and resets.
`scripts/admin/lockdown.py` flashes a board with wlink and drives it
(section 9, step 3). This chip has one level of read protection: RDPR at
0xA5 is off and anything else is on ([security-plan.md](security-plan.md)
section 3, from the reference manual). The "RDP 2" in this document means
that, with write protection and the boot ROM downloads off: a board that
can't be read out or reflashed except by a wipe.

## 8. Keeping the V5F out

The V5F runs code that faces the card and the terminal, so it mustn't be
able to change stage 1, the security sector or the record, or read the
token.

- **Forth:** `VM_IO_PAGE` maps the peripheral registers. At
  `SECURITY_LEVEL` 0 (`Common/security.h`) that's all of them. At 1 and 2
  it's the guarded page (`vm_io_page` in
  `src/vm.h`): every access goes through the allowlist in
  `V5F/User/io_allow.h`, which leaves out the flash controller, the DMA
  controllers and the peripherals with DMA of their own, SDIO, the ECDC,
  the RNG, PWR, USB and external memory, and makes RCC, the RTC, the
  watchdogs and the debug UART read-only. Anything else is an invalid
  address (-9), and a write to a read-only one is write-protected (-20).
  `vm.c` and `vm_rv32.S` both check it (`unit_tests/ioguard`,
  `unit_tests/vmasm`); memory under a page's read limit costs nothing
  more. The option bytes aren't in the I/O page at all
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
- **DMA on this chip** (CH32H417 reference manual V1.8, checked
  20 October 2026): nothing in hardware limits what a bus master reads.
  There's no MPU or firewall (`__MPU_PRESENT 0` in `ch32h417.h`), no lock
  bit on any DMA, DMAMUX or RCC register, and no privilege control on
  peripheral registers. DMA1/DMA2 read code flash (RM 10.1: "Flash memory,
  SRAM, peripheral SRAM, HB peripherals can all be used as the sources
  and targets"), and nothing says read protection stops them. Bus masters
  besides DMA1/DMA2: ETH, the USB controllers, USBPD, SDMMC, DVP, SerDes,
  LTDC, GPHA, HSADC, UHSIF, and the ECDC (`SRC_ADDR`/`DST_ADDR`), the
  engine `ecdc.c` uses. The one access control is the V5F's
  `MEMARY_CFGR` CSR (0xBC5, RM 4.2.3.12, machine mode, no lock), which can
  turn off DMA reads and writes of the V5F's ITCM and DTCM, and the V3F's
  access to them; it doesn't cover flash or the shared SRAM. So the only
  fence is in software: keep user mode from programming any bus master.
  DMA1, DMA2, DMAMUX, RCC and the flash controller all lie in one 16 KB
  block (0x40020000-0x40023FFF, with CRC, USBFS and the RNG), which one
  NAPOT entry can cover.
- **What PMP doesn't stop:** DMA. A DMA controller isn't the core, so the
  V5F's C code could still use DMA to read the token. So the V5F's C code
  is trusted (it's signed and checked), and PMP is a second line against
  bugs in it. The V5F's own SDIO driver uses DMA; it stays in C.
- **When it's set:** the first thing the V5F image does, so it depends on
  the image being good, which stage 1 has just checked. A stricter
  alternative: stage 1 starts the V5F at a small shim in stage 0 that sets
  the entries and then jumps to the image. **Verify** that the V5F can run
  code from the V3F's flash.
- **Guarded DMA** (`V5F/User/gdma.c`, 20 October 2026): entry 2 now
  covers the whole 16 KB block, read-only to user mode, so user mode
  can't program DMA1, DMA2 or DMAMUX. DMA goes through an `ecall`
  service: a fixed channel table (the SD card's DMA2 channel 1), one
  16 KB buffer in DTCM that is the only memory a channel ever points at,
  user buffers checked on every call (DTCM .data/.bss, or since
  22 October 2026 the memory pool in general-purpose SRAM; never the
  stack, the peripherals or code), and the channel, its DMAMUX request
  and the buffer burned after every transfer. RCC and CRC share the
  block and user mode writes them, so `HardFault_Handler` does those
  stores for it (`guard.c` decodes them); stores to the DMA registers,
  the flash controller, USBFS or the RNG fault (-127 to Forth).
  (`READ-BLOCK` never could aim the card's DMA into the I/O page:
  `blockCells` in `api0.c` refuses it.) Not covered: the ECDC (a bus
  master with its own addresses, outside the block, still programmed by
  `ecdc.c`), and machine mode's data in DTCM, which user mode can still
  write (its code is in flash: below).
- **`SECURITY_LEVEL` 0 is for development only.** At level 0 Forth's I/O
  page maps every peripheral register, and the PMP can't fence them all:
  entry 2 stops Forth writing DMA1, DMA2, DMAMUX, the flash controller,
  USBFS and the RNG, but every other bus master is outside it and open to
  `!`. The ECDC, for one, copies memory to memory from any address Forth
  writes to its `SRC_ADDR` and `DST_ADDR`, so Forth at level 0 can read
  the device token, stage 1 and the board's key, and write ITCM, where
  machine mode's code runs (the board's key at level 0 comes from its
  UID anyway). SDMMC, ETH, LTDC, GPHA, DVP, SerDes, HSADC, UHSIF and the
  USB controllers are the same. Only the allowlist at levels 1 and 2
  (`io_allow.h`, above) keeps Forth away from them. So a board that runs
  untrusted Forth needs level 1 or 2; level 0 is for bringing up hardware
  on a bench board.

- **What's set today** (`V5F/User/pmp.c`, with today's layout,
  section 6: stage 1's records at 0x10000 and 0x12000, the V5F at
  0x14000, LiteForth's pages at 0x30000): the start-up code calls
  `pmp_lock` just before its `mret`, which sets these entries:

  | # | Region | User | Machine | Locked |
  |---|---|---|---|---|
  | 0 | TOR, 0x00000000-0x08013FFF: the whole low window, and in the high window the V3F's image (stage 0 and 1) and stage 1's records (the device token and the board's key) | none | none | yes |
  | 1 | 0x20100000-0x2013FFFF: the V3F's RAM (stage 1's code, data and stack) | none | none | yes |
  | 2 | 0x40020000-0x40023FFF: DMA1, DMA2, DMAMUX, RCC, the flash controller, CRC, USBFS, RNG | read | all | no |
  | 3 | all memory | RWX | RWX | yes |

  Entry 0 (20 October 2026; the records under it since 21 October) is
  one TOR entry from address 0 up to the V5F's slot in the high window,
  0x08014000: the code flash's whole low window (the V3F's image, the
  records, the V5F's own image and LiteForth's pages all appear there
  too) and, in the high window, the V3F's image and stage 1's records.
  So the V5F, machine mode included, can't read stage 0, stage 1, the
  device token or the board's key at all. What it still sees of the code
  flash is its own image and LiteForth's pages, through the high window.
  The V5F
  runs from the low window only in its start-up code, before `pmp_lock`,
  so nothing may run that code again with the entry locked: Ctrl+X x3,
  which used to restart the V5F alone (an HSEM interrupt jumping to
  `handle_reset`), now resets the chip (`Common/cdc_bridge.c`), and the
  V5F only ever starts from a reset.

  `flash.c` reaches the flash controller through an `ecall` service in
  machine mode (`Ecall_U_Mode_Handler`, pmp.c) that programs a LiteForth
  flash page, its address and buffer checked. A service never writes
  where user mode points it unless the buffer lies in DTCM's .data or
  .bss, or in the pool (`guard_buffer_ok`; the pool, in general-purpose
  SRAM at 0x20140000, holds nothing of machine mode): the handler's frames share user mode's stack,
  so a pointer just below the caller's sp could overwrite machine mode's
  return address. Guarded DMA checks its buffers that way. The V5F holds
  no key: until 21 October 2026 its machine mode kept the board's
  private key in a record of its own (`board_serve`, behind the same
  `ecall`, with entry 1 over it); now stage 1 holds it (section 6).
  `pmp_report` prints the entries at each start (`V5F PMP: ...`).

  Entry 1 (21 October 2026) is a locked NAPOT entry over the 256 KB from
  0x20100000, where `Link_v3f.ld` now keeps all of the V3F's RAM: stage
  1's code (RAM_CODE), its data, heap and stack, and so the device token,
  the board's key while stage 1 uses it, and every return address. The
  V5F, machine mode included, can't read or write it. The shared block
  (0x20178000, 32 KB: the USB rings, the flags, the crumbs) lies above it
  and stays open to both; 0x20140000-0x20177FFF is unused. So a bug in
  the V5F's C code can't write stage 1's code or stack directly.

- **Machine mode's code runs from flash** (21 October 2026). User mode
  can write ITCM, where the rest of the V5F's code runs, so machine
  mode's code isn't there: the vector table, the trap and interrupt
  handlers (`ch32h417_it.c`, `guard.c`, the `ecall` handler and its
  services in `pmp.c`, Guarded DMA in `gdma.c`, the 1 ms tick) are in the
  last 8 KB of the V5F's slot (`.mmode`, `V5F/User/mmode.h`), run in
  place from the code flash's high window, 0x0802E000. Nothing there
  calls anything else: no `memcpy` or `memcmp` (local loops), no
  `vmTrapArmed` (a flag it reads), and the V5F is built without
  `-msave-restore`, whose `__riscv_save_N` helpers would be in ITCM.
  `scripts/mmodecheck.py` checks every build for that (`ch32build.sh`,
  so CI): any direct call or jump out of machine mode's code, and any
  vector table entry outside it, fails the build. WCH's flash routines
  are the exception: the chip locks up if they run from the flash they
  program, so they're kept in flash and copied into the top 4 KB of ITCM
  by machine mode (`flash_ram_begin`, `pmp.c`), with interrupts off,
  just before each erase and write; anything user mode left there is
  overwritten before it could run. The V3F-only handlers in `Common/`
  (`TIM2_IRQHandler`, `USBFS_IRQHandler`) are left out of the V5F's build.
  Running from flash is slow (about 240 times slower than ITCM, measured
  on the board), which doesn't matter for this little code: an `ecall`
  takes about 12 us, a page erase and write about 1.2 ms.

  The fault handler runs on a stack of its own (27 October 2026,
  SYS-SEC-11, part 1 below). `HardFault_Handler` switches `sp` to the
  machine-mode stack (`_mstack_top`, set into `mscratch` by the start-up
  code before its `mret`) before it stores anything, so user mode can't
  have the handler save the faulting registers into a peripheral by
  setting its own `sp` there first; `mscratch` is a machine CSR user mode
  can't change. The stack is 1 KB in DTCM (`Link_v5f.ld`), which user mode
  can write, and that's fine: one core, so no user code runs while the
  handler does, and the handler fills its frame before using it. A fault
  inside the handler double-faults and resets the chip, so the stack is
  never re-entered.

  Still open: the fast-interrupt handlers (`SysTick1`, the `ecall`
  services) still save at user mode's `sp`, through WCH's hardware
  prologue (HPE, CSR 0x804 bit 0). Turning HPE off makes every one of them
  a stub that switches stacks the way the fault handler now does; that
  comes with part 3 below, which also moves the system timer off SysTick.
  Guarded DMA's buffer and state and the `ecall` block are in DTCM too,
  checked on each call. Whether user mode can reach the interrupt
  controller (PFIC, 0xE000E000) is part 3's concern.

The V3F keeps its own PMP unlocked: it's the trusted side.

### Closing the bus masters (planned, 27 October 2026)

**The gap.** Entry 2 fences DMA1, DMA2 and DMAMUX from user mode, but not
the other bus masters. The ECDC (0x40016C00) has a RAM-to-RAM engine with
addresses of its own; SDMMC, USBHS, USBSS, USBPD, ETH, DVP, SerDes, LTDC,
GPHA, HSADC, UHSIF, QSPI and FMC are bus masters too. User mode can program
every one of them. The VM's allowlist keeps Forth out at levels 1 and 2, but
code that gets past the VM can point the ECDC at stage 1's records and read
the device token. That might be a memory-safety bug in LiteForth's C code,
or a fault injected into the allowlist's check. PMP entry 0 doesn't stop a
bus master. A signed but malicious image can do the same from machine mode.
This chip has no bus firewall, and its flash has no area hidden from bus
masters. Read protection stops only the debugger and the boot ROM.
`MEMARY_CFGR` covers only the V5F's ITCM and DTCM. The PMP has four entries,
all in use.

**A second way in.** Machine mode's trap and interrupt handlers run on user
mode's stack: `HardFault_Handler` saves x1-x31 at user mode's `sp`, and so
does the hardware prologue (HPE, turned on by the start-up code's write to
CSR 0x804) of every "fast" interrupt. User code that set `sp` to a
peripheral's registers and then trapped would have machine mode store its
registers there. It could program a DMA channel through entry 2 that way,
or turn on a clock. This hasn't been tried on the board, and won't be: the
fix doesn't need it shown.

**The plan.** Four parts, none needing another PMP entry:

1. **Machine mode on its own stack.** Every vector goes to a stub whose
   first instruction is `csrrw sp, mscratch, sp`. It switches to machine
   mode's stack before anything is stored, and switches back before
   `mret`. HPE is off. The start-up code sets `mscratch` before its `mret`
   into `main`. The stack is in DTCM, which user mode can write, and
   that's acceptable:
   - there's one core, and no user code runs while a handler does;
   - user mode can't program a bus master to write it meanwhile (part 2);
   - so nothing on it can change under a handler.

   Two rules for machine mode, which `mmodecheck.py` partly checks:
   - **No stores through user mode's pointers,** except into buffers that
     `guard_buffer_ok` allows.
   - **No trust in its own data between traps without checking it again.**
     The `ecall` block (`svc` in `pmp.c`) is copied in at entry; Guarded
     DMA's state is checked on every call.

   `mmodecheck.py` also checks that every vector's target starts with the
   swap. This replaces fencing machine mode's data with a PMP entry, which
   there isn't room for.

   *Built so far:* the fault handler switches to the machine-mode stack
   (above), confirmed on the board (B29, 28 October 2026); the
   fast-interrupt handlers still use HPE and come with part 3.
2. **No bus master runs for user mode.** User mode can't write RCC: entry 2
   makes it read-only, and `guard.c` does the stores user mode is allowed.
   A peripheral without its clock neither takes register writes nor runs.
   - **The guard refuses a store that would turn on a bus master's clock:**
     ECDC, LTDC, GPHA, HSADC, SDMMC, USBHS, USBSS, USBPD, ETH, DVP, SerDes,
     UHSIF, QSPI1/2, FMC and PIOC, as well as DMA1/2. It computes the
     register's value after the store.
   - **The start-up code turns all of them off.**
   - **Machine-mode services turn on only what they use,** and turn it off
     again before returning: Guarded DMA (DMA2) as now, and a new ECDC
     service that `ctr_hw_crypt` calls. That service takes the key, the
     counter and a buffer, checked as Guarded DMA checks its buffers. The
     engine then only ever runs inside machine mode.
   - **The V3F shares RCC and is trusted.** Its USBFS is inside entry 2.
3. **User mode out of the interrupt controller and the system timer.**
   *Done and confirmed on the board (B31, 29 October 2026).* Entry 3
   shrinks from all memory to the 2 GB from 0 (NAPOT `0x0FFFFFFF`), so
   from 0xE0000000 up, the PFIC and SysTick, is machine mode's alone (an
   unmatched access is allowed for machine mode, denied for user mode).
   Its VTF registers could otherwise send an interrupt to user code in
   machine mode. That means:
   - **The time base moves from SysTick1 to a general-purpose timer** that
     user mode reads: it counts microseconds and wraps each millisecond,
     and its update interrupt counts milliseconds in machine mode, as
     SysTick's does now. *Built and confirmed on the board (B30, 28
     October 2026):* TIM6, a basic timer (no output channels), in
     `V5F/User/lftime.c`; its microsecond rate tracks the RTC and
     survives standby. Its prescaler comes from HCLK, not the V5F's
     `SystemCoreClock` (`doc/ch32h417-notes.md`). This was the
     prerequisite for the shrink.
   - **Interrupt enables moved to the start-up code** (`v5f_irq_setup`,
     `V5F/User/pmp.c`, called in machine mode after `pmp_lock`): TIM6's to
     the V5F, SDIO's left off. `reboot` and `standby` became machine-mode
     ecall services (`PMP_SVC_REBOOT`, `PMP_SVC_STANDBY`); `standby`'s
     privileged tail, which enters Stop mode and resets on the alarm wake,
     is `lf_standby_service` in `.mmode`. `Delay_Init` (SysTick, WCH's
     unused demo timer) is skipped on the V5F.
4. **Machine mode's code is the root's.** Today the release key signs the
   whole image, machine-mode code included. The code that runs in machine
   mode (`.mmode` with the vector table, `.ramfunc`'s flash copy, and the
   start-up path up to its `mret`, moved into `.mmode`) also gets a root
   signature, made offline (`sealblocks.py mmode-sign`, to be written).
   Stage 1 checks it at every boot beside the release signature. A stolen
   release key can then change only user-mode code, and parts 1 to 3
   fence that in. A machine-mode change needs the root key; that code is
   about 8 KB and rarely changes.

Entries after part 3:

| # | Region | User | Machine | Locked |
|---|---|---|---|---|
| 0 | TOR, 0x00000000-0x08013FFF | none | none | yes |
| 1 | 0x20100000-0x2013FFFF: the V3F's RAM | none | none | yes |
| 2 | 0x40020000-0x40023FFF: DMA1, DMA2, DMAMUX, RCC, the flash controller, CRC, USBFS, RNG | read | all | no |
| 3 | 0x00000000-0x7FFFFFFF | RWX | RWX | yes |
| | from 0x80000000: the PFIC, SysTick | none | all | |

Considered and left out: `MEMARY_CFGR`, to turn off DMA access to ITCM and
DTCM. With part 2, only machine mode runs a bus master, and the ECDC's
in-place path works in DTCM.

**What's left.** Machine-mode code signed by the root that is itself
hostile: on this chip whatever machine mode can program can read the
records. Then physical attacks on stage 1 itself (glitching its checks, side
channels on its key operations). Per-board keys keep any one extraction to
that board.

**Board checks first**, from Forth on a level 0 board (VM page 5 maps every
register there; nothing new to build, nothing flashed):
- **The gap:** the ECDC copies the first 256 bytes of the V3F's image
  (0x08000000, inside entry 0 but not secret) into the VM's RAM page.
  Matching the image shows a bus master reading past the PMP.
- **The fence:** with the ECDC's clock off, a write to its `SRC_ADDR`
  doesn't stick, and starting it does nothing.
- **The reset values:** which bus masters' clocks are on at boot (RCC
  reads only).

Then parts 1 to 3, each with its board test, and part 4.

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
- **With no entry set, user mode has full access.** LiteForth runs in user
  mode with `pmpcfg0` = 0 and reads and writes all memory normally. So on
  this core an unset PMP doesn't deny user mode, although the RISC-V spec
  (and an outside review that relied on it) says an access that matches no
  entry fails once any entry is implemented. Whether that changes once an
  entry is set is still open (above).
- **Not yet run:** the code flash's two windows, the V5F reading the
  V3F's RAM, and the V5F running code in the V3F's flash.

## 9. Testing, without bricking anything

1. **Desktop.** Stage 0 and stage 1's update logic in plain C, reaching
   flash and the card through callbacks (as `sealed.c` reads blocks), with
   unit tests: every refusal, a power cut at every step of every phase,
   both copies of the security sector, rollback, recovery. `sealblocks.py
   package` and its tests. Done for the V5F image (`src/update.c`,
   `unit_tests/update`), and recovery's YMODEM receiver (`src/ymrecv.c`,
   `unit_tests/ymrecv`); still to do: stage 0 and a new stage 1 into its
   other slot. `unit_tests/vmasm`'s RV32 emulator runs the V3F code as
   built, for size and speed.

   **The simulated board** (`unit_tests/stage1sim`) runs the V3F's real
   `stage1.c` and `cdc_bridge.c`, built unchanged on the desktop. Their
   flash, card, shared SRAM, option bytes, USB and resets come from
   `sim.c`, with the board's memory mapped at the chip's own addresses,
   and the V5F is a model. `test_stage1.py` plays every scenario on it,
   with a fresh board for each, and checks the output: B3 to B8, the
   refusals, Ctrl+X x3, the recovery console, level 2's lock-down, and a power
   cut at every flash and card write of an install. It writes the
   board's output word for word to `reports/stage1sim.md`. It runs in
   CI with `make test`, in about 40 seconds.
2. **This board, RDP 0, no WRPR.** Phases A-F for real, then recovery.
   Anything that goes wrong is fixed with the WCH-Link. Every run is
   logged in [board-tests.md](board-tests.md), with the firmware, the
   packages' hashes and the board's output.

   `scripts/admin/hiltest.py` runs the board's scenarios with nobody at
   the board: install, rollback, recovery on request, and the lock-down
   check. It drives the USB terminal as kwai does and reads the debug
   UART. It resets the chip by typing `reboot` (or `reset` in recovery),
   and the rollback needs no resets at all. It writes a report for
   board-tests.md. `stage1sim/test_hil.py` runs the same script against
   the simulated board in CI, with ptys for its ports, so the lab's script
   is tested too.
2. **This board, RDP 0, no WRPR.** Phases A-F for real, then recovery.
   Anything that goes wrong is fixed with the WCH-Link. Every run is
   logged in [board-tests.md](board-tests.md), with the firmware, the
   packages' hashes and the board's output.
3. **The spare board.** `scripts/admin/lockdown.py run`: a level 2 build
   flashed, then locked with stage 1's `lockdown` (read protection, WRPR
   on the V3F's sectors, boot ROM downloads off); then `enroll`, an
   update, a rollback and a recovery over USB. Only a chip erase undoes
   it. To check first, on the spare: that wlink flashes the V5F's image
   at 0x10000 on this dual-core part, that the option bytes load at a
   software reset (or need a power cycle), and that stage 1 and the V5F
   can still write flash under read protection (stage 1 runs from
   RAM_CODE, and the reference manual says code running from RAM can't
   program flash then).

## 10. To verify or decide

- **Verify** how the code flash's two windows (0x00000000 and 0x08000000)
  map, for the PMP entries in section 8.
- ~~**Verify** that a locked PMP entry applies in machine mode.~~ It
  does, on the V5F (section 8, *What the board showed*); the V3F not yet
  tested.
- ~~**Verify,** from real user mode, that user-mode accesses are
  checked.~~ They are: a load from a locked no-access entry faulted
  (mcause 5; [board-tests.md](board-tests.md) B13). **Verify** whether
  one that matches no entry fails.
- ~~**Verify** whether the clearing of a locked entry is ignored.~~ It
  is (`pmp_report` at boot, [ch32h417-notes.md](../ch32h417-notes.md)).
- **Verify** that the V5F can execute from the V3F's flash (for the shim
  in section 8).
- **Verify,** on the spare board only: that under RDP 2 nothing clears
  WRPR or rewrites the option bytes, and that USB and the card still work.
- **Verify** the RNG on real parts before trusting the token to it.
- **Decide** whether level 2 needs a rollback floor that a leaked token
  can't lower (the minimum version is in the record, which stage 1 can
  write and so, through a bus master, could anything that controls the
  V5F). The signed boot check keeps out unsigned code either way.
- **Decide:** the slot sizes; N trial boots; whether stage 1 is encrypted;
  `confirm` explicit or automatic; what stage 0 does when both slots fail
  (blink a code, print on the debug UART, or both).
- The download erase (`"clearcodeflash": false` still erases everything)
  stops mattering after RDP 2, but until then it wipes the security
  sector and so the token on every download, which re-makes the token and
  orphans card backups: expected during development.
