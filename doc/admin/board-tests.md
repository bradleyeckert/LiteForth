# Board tests: firmware updates and isolation

This is the record of security tests run on real hardware, kept as
evidence for a later cybersecurity document. Each entry gives what was
tested, the firmware and keys involved, how it was run, what was expected,
and what the board printed, word for word. The design these tests check is
in [firmware-updates.md](firmware-updates.md), and the PMP findings are in
its section 8. The desktop tests (`unit_tests/update`, `sealed`, `seal`)
cover every refusal and a power cut at every step, and
`unit_tests/stage1sim` plays these board scenarios on a simulated board in
CI. This log covers what only the real board can show. From B6 on, the
runs are meant to be made with `scripts/admin/hiltest.py`, whose report
goes in here as it is.

## The setup

| | |
|---|---|
| Board | nanoCH32H417 (WCH CH32H417, V3F + V5F cores), UID `69a90e647f9cf6a9` |
| Protection | RDP 0, no write protection (WRPR): everything can be undone with the WCH-Link ([firmware-updates.md](firmware-updates.md) section 9, step 2) |
| Board key | the X25519 private key is BLAKE2s of the UID (person `LFinsec1`): an `INSECURE` build up to B6, `SECURITY_LEVEL` 0 or 1 from B7 on. For development only; a level 2 board uses the random key from `enroll` |
| Root key | a test root, public key `5700c0f2...83ade742`; release key 1, product 0x4C46 |
| Card | microSD with an exFAT volume: `lfblocks.fb4` (sealed, revision 6), `lfstage.bin` (1 MB), `lfbackup.bin` (256 KB) |
| Host | kwai v0.5 on COM17; the V3F's debug UART at 9600 baud |

A package is made with `sealblocks.py package IMAGE PKG --component v5f
--release release1.key --boards boards.csv --revision N`, checked with
`sealblocks.py check` against the root key and `open` with the board key
(the output must equal the image), and staged with kwai's `~x PKG -2`.

## The log

### B1. PMP behaviour of the V5F (18 October 2026)

Runs of `pmp-test` (PR #164, #165). Results and their meaning are in
[firmware-updates.md](firmware-updates.md) section 8, *What the board
showed*. In short:
- LiteForth runs in user mode.
- A user-mode `ecall` has its own vector, whose default handler hangs.
- A locked entry applies to machine mode.
- A fault inside a trap handler locks the core up.
- MPRV appears to be ignored.

Still open: user-mode checks with entries set.

### B2. Staging a package, `-2 ymodem` (18 October 2026)

**Firmware:** PR #171 and kwai #11 (v0.5).

**Run:** `~x FILE -2` with three packages: `fw-test.lfu` for this board,
`fw-other.lfu` for another board's key, and a bad file.

**Expected:**
- The good package is written to `lfstage.bin`, read back, checked in full,
  and flagged for stage 1.
- The other two are refused, and the flag is not set.

**Result:** pass, all three as expected. The terminal output was not kept.

### B3. First install, V5F version 1 (18 October 2026)

**Firmware:** `main` at `e490126` (PR #172).
- V3F built Oct 18 2026 17:52:57, GCC 12.2.0, 56,180 bytes.
- V5F `LiteForth_V5F.bin`: 89,360 bytes, sha256 `45e9109b...f0ade93031` (`45e9109b9de8f7159cd019c26cec0d2566ca26bc02687e551169e34808d83a89`).

**Package:** `v5f-1.lfu`, the same V5F image as version 1, 98,304 bytes, sha256 `5726c464c8246d213787d66f714abd0924a9b7f122e87323bad0d19acf694424`.

**Starting state:**
- The security record was blank (never updated).
- The V5F slot held the image above, flashed by the WCH-Link.

**Run:** stage the package, then press the reset button.

**Expected:**
1. The blank record adopts the image in flash as version 0 and makes the device token.
2. The old image is backed up to `lfbackup.bin`.
3. Version 1 is installed and starts on trial.
4. The V5F starts and confirms.

**Terminal:**

```
~x ..\mailbox\v5f-1.lfu -2
ymodem: send the file(s) now (YMODEM)

[kwai: sending ..\mailbox\v5f-1.lfu: 98304 bytes]
[kwai: 96/96]
[kwai: sent ..\mailbox\v5f-1.lfu]
ymodem: v5f-1.lfu: staged in lfstage.bin, 24 blocks, read back and checked
ymodem: package: V5F image, 89360 bytes, version 1: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>
```

**Debug UART, after reset** (a delay first, for the backup and the install):

```
V3F update: version 1 installed, on trial
V3F: starting the V5F
V3F: the V5F is running
V5F crumbs: reset by pin, boot 2; the previous run left 1767 crumbs
V3F: built Oct 18 2026 17:52:57, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 18 ms)
V5F: LiteForth starting, flash is blank
V3F update: version 1 confirmed
V3F: USB configured
```

**Result:** pass.
- `1 load` and the clock demo ran normally afterwards.
- "flash is blank" refers to LiteForth's flash pages at 0x08030000. Stage 1 can't write there, since its writes are fenced below 0x30000. The pages had been erased when the new builds were downloaded.

### B4. Rollback of an image that never confirms (18 October 2026)

**Package:** `v5f-2-hang.lfu`, version 2. It holds a 4,096-byte V5F image of `j .` (`6f000000` repeated; image sha256 `5778694d4183a7f8fabc40f79d036b4bbb12d9fe728f5a8d845665f0ade93031`; package sha256 `41bb01729d99e23e1697a0a5ff53a168a8114b7c483e5e977328af9f75061bb2`). It passes every check, but the V5F only spins, so it never confirms.

**Firmware:** as in B3.

**Starting state:** version 1 installed and confirmed (B3).

**Run:** stage the package, then press the reset button five times.

**Expected:**
1. Version 2 is installed on trial. The V5F hangs, so the V3F stays at "starting the V5F" and USB doesn't come up.
2. Resets 2 to 4 report "on trial, boot 1 of 3" through "boot 3 of 3".
3. Reset 5 rolls back to version 1 from `lfbackup.bin`. LiteForth starts but does not confirm, because nothing is on trial.

**Debug UART** (the first two lines are the end of the previous boot; the package was then staged with `~x ..\mailbox\v5f-2-hang.lfu -2` and the reset button pressed five times):

```
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 18 ms)
V3F update: version 2 installed, on trial
V3F: starting the V5F
V3F update: version 2 on trial, boot 1 of 3
V3F: starting the V5F
V3F update: version 2 on trial, boot 2 of 3
V3F: starting the V5F
V3F update: version 2 on trial, boot 3 of 3
V3F: starting the V5F
V3F update: rolled back to version 1
V3F: starting the V5F
V3F: the V5F is running
V5F crumbs: reset by pin, boot 3; the previous run left 745 crumbs
V3F: built Oct 18 2026 17:52:57, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 18 ms)
V5F: LiteForth starting, booting from flash
V3F: USB configured
```

**Result:** pass, exactly as expected.
- Version 2 got four boots without a confirm (the install, then trial boots 1 to 3).
- At the fifth reset, stage 1 restored version 1 from the encrypted backup on the card. The restored image passed its MAC check, LiteForth started from flash, and USB came up.
- No "confirmed" line followed, because nothing was on trial.
- During the four boots of version 2, the V3F never printed "the V5F is running", and USB never came up. A V5F that doesn't start leaves the board with no terminal, and only stage 1's rollback brings it back: what section 7's recovery mode is for.

### B5a. No backup room, so no update (18 October 2026)

**Firmware:**
- PR #174 (recovery), V3F built Oct 18 2026 18:54:04, GCC 12.2.0, with the linker fix.
- The V5F as in B3.
- The MounRiver download erased the whole chip, so the security record was blank again, and the boot adopted the V5F slot (128 KB, version 0).

**Package:** `v5f-2-hang.lfu` (B4).

**Starting state:** `lfbackup.bin` renamed on the card, so stage 1 couldn't find it.

**Run:** stage the package with `~x ..\mailbox\v5f-2-hang.lfu -2`, then press reset several times.

**Expected:**
- The installed image checks out, so it must be backed up before anything is programmed.
- With no `lfbackup.bin`, the update is refused and nothing changes.
- The V5F starts as before. The refusal clears the flag, so later resets don't try again.

**Debug UART:**

```
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 18 ms)
V3F update refused: lfbackup.bin needs 33 blocks
V3F: starting the V5F
V3F: the V5F is running
V5F crumbs: reset by pin, boot 2; the previous run left 301 crumbs
V3F: built Oct 18 2026 18:54:04, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 18 ms)
V5F: LiteForth starting, flash is blank
V3F: USB configured
V3F: starting the V5F
V3F: the V5F is running
V5F crumbs: reset by pin, boot 3; the previous run left 58 crumbs
(the same for the next reset)
```

**Result:** pass.
- The refusal names what's missing: 33 blocks, a header plus the 128 KB slot.
- The working image kept running.
- Later resets had no package waiting, so they booted quietly.

### B5. Recovery over USB (18 October 2026)

**Firmware:** as in B5a. Version 0 (the adopted slot) was installed.

**Packages:**
- `v5f-2-hang.lfu`, the never-confirming version 2 (B4).
- `v5f-1.lfu`, the real V5F as version 1 (B3).

**Run:**
1. With `lfbackup.bin` in place, stage version 2 and press reset. Version 2 is installed, and version 0 is backed up.
2. Unplug, rename `lfbackup.bin` on the card, plug in again.
3. Press reset until stage 1 gives up on version 2.
4. In kwai, send version 1 with `~x ..\mailbox\v5f-1.lfu -2`.

**Expected:**
1. Version 2 gets three trial boots without a confirm.
2. The rollback finds no backup, so there's no working image and stage 1 goes into recovery: the V5F stays off, and stage 1 answers on USB.
3. Version 1 comes in over USB into `lfstage.bin` and installs without a backup, since there's nothing worth keeping.
4. The chip resets, and version 1 starts on trial and confirms.

**Terminal (kwai):**

```
[kwai: sending ..\mailbox\v5f-2-hang.lfu: 12288 bytes]
[kwai: 12/12]
[kwai: sent ..\mailbox\v5f-2-hang.lfu]
ymodem: v5f-2-hang.lfu: staged in lfstage.bin, 3 blocks, read back and checked
ymodem: package: V5F image, 4096 bytes, version 2: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>
[kwai: com17 lost; waiting for it to come back]

[kwai: com17 is back]
LiteForth stage 1: recovery mode. The V5F stays off:
update: the backup is missing or damaged; can't roll back
In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
adopt: take the V5F image in flash as it is (development builds only).
reset: start again.
recovery ok> ~x ..\mailbox\v5f-1.lfu -2
-2 ymodem

[kwai: sending ..\mailbox\v5f-1.lfu: 98304 bytes]
[kwai: 96/96]
[kwai: sent ..\mailbox\v5f-1.lfu]
ymodem: v5f-1.lfu: 98304 bytes received
update: version 1 installed, on trial
recovery: installed; restarting to try it

[kwai: com17 lost; waiting for it to come back]

[kwai: com17 is back]

ok>
```

**Debug UART** (from the boot before step 1):

```
V3F: the V5F is running
V5F crumbs: reset by pin, boot 1; the previous run left 297 crumbs
V3F: built Oct 18 2026 18:54:04, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 18 ms)
V5F: LiteForth starting, flash is blank
V3F: USB configured
V3F update: version 2 installed, on trial
V3F: starting the V5F
V3F update: version 2 on trial, boot 1 of 3
V3F: starting the V5F
V3F update: version 2 on trial, boot 2 of 3
V3F: starting the V5F
V3F update: version 2 on trial, boot 3 of 3
V3F: starting the V5F
V3F update: the backup is missing or damaged; can't roll back
V3F: recovery mode: the V5F stays off; waiting for a package over USB
V3F: built Oct 18 2026 18:54:04, GCC 12.2.0
V3F: USB configured
V3F LiteForth stage 1: recovery mode. The V5F stays off:
V3F update: the backup is missing or damaged; can't roll back
V3F In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
V3F adopt: take the V5F image in flash as it is (development builds only).
V3F reset: start again.
V3F ymodem: v5f-1.lfu: 98304 bytes received
V3F update: version 1 installed, on trial
V3F recovery: installed; restarting to try it
V3F update: version 1 on trial, boot 1 of 3
V3F: starting the V5F
V3F: the V5F is running
V5F crumbs: reset by software power pin; none from before (power-up)
V3F: built Oct 18 2026 18:54:04, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 18 ms)
V5F: LiteForth starting, flash is blank
V3F update: version 1 confirmed
V3F: USB configured
```

**Result:** pass, every step as expected.
- With no working image and no backup, the board refused to run the bad image and still answered on USB.
- The recovery console worked unchanged with kwai's `~x FILE -2`: its `recovery ok>` prompt paced kwai's lines, and kwai's reconnect carried over both resets.
- The package was checked in full before the install: signature, every block and the image hash. It was installed with no `lfbackup.bin` at all.
- The chip reset after the install. Version 1 took one trial boot and was confirmed when LiteForth started.
- The V5F's crumbs line after the recovery reset says "reset by software power pin; none from before (power-up)". `NVIC_SystemReset` seems to set the software and pin flags and clear the crumbs, as after a power-up. That's harmless, but worth knowing when reading the crumbs.

### B6. Install, rollback, recovery and lock-down, run by hiltest.py (18 October 2026)

The first run of [scripts/admin/hiltest.py](../../scripts/admin/hiltest.py) on the board. Nobody touched the board during the run: every reset was `reboot` typed to LiteForth, `reset` typed to the recovery console, or the V3F's own reset when the V5F didn't start.

**Firmware:**
- `main` at PR #179: the V3F's timed wait for the V5F, `reboot`, the lock-down check, and recovery on request.
- V3F built Oct 18 2026 22:20:41, GCC 12.2.0.
- V5F `LiteForth_V5F.bin`: 89,472 bytes, sha256 `dc8569d2d2f6e2bd59f55126bd3f31088eda8271206cf6f1588083e22b5e0c45`.

**Packages:**
- GOOD, `v5f-3.lfu`: that V5F as version 3.
- HANG, `v5f-2-hang.lfu`: B4's image that never starts.

**Starting state:** a record from earlier tests; `lfstage.bin` and `lfbackup.bin` on the card; read protection off.

**Command:** `py scripts\admin\hiltest.py run --good mailbox\v5f-3.lfu --hang mailbox\v5f-2-hang.lfu`

The report, as hiltest.py wrote it (`hil-report-20261018-2234.md`):

Run 2026-10-18 22:34:49; debug UART COM14, USB COM17, reset by `reboot`.

- GOOD: `v5f-3.lfu`, sha256 0171f47be64530e7f525fab85ae1c603e5fd53ede042ffe79bde37944034c733
- HANG: `v5f-2-hang.lfu`, sha256 41bb01729d99e23e1697a0a5ff53a168a8114b7c483e5e977328af9f75061bb2

#### install

GOOD.lfu staged by LiteForth's `-2 ymodem`, installed at the next
reset, started on trial and confirmed.

- PASS: staged: "waiting for stage 1 to install it"
Debug UART:

```
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 17 ms)
```
USB terminal:

```
ok>ymodem: send the file(s) now (YMODEM)
C.C<YMODEM>C.ymodem: v5f-3.lfu: staged in lfstage.bin, 24 blocks, read back and checked
ymodem: package: V5F image, 89472 bytes, version 3: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>
```
- PASS: installed (V3F update: version 3 installed, on trial)
- PASS: confirmed by LiteForth
Debug UART:

```
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F update: version 3 installed, on trial
V3F: starting the V5F
V5F crumbs: reset by software, boot 3; the previous run left 1800 crumbs, the last:
V3F: the V5F is running
   303975236 DONE 00000000 00000000
V3F: built Oct 18 2026 22:20:41, GCC 12.2.0
   303975239 READ 00003140 200D7DEC
   303975244 STRT 00000000 00000001
   303975451 PROG 00000000 00000000
   303975452 DEND 00000500 00000007
   303975456 CM12 00000000 00000000
   303975460 DONE 00000000 00000000
   303975473 READ 00003640 200D79EC
   303975478 STRT 00000000 00000001
V3F: USB configured
   303975478 PROG 00000200 00000080
   303975677 DEND 00000500 00000007
   303975682 CM12 00000000 00000000
   303975686 DONE 00000000 00000000
   303975691 MAP  00003640 0000007A
   304045529 READ 00003140 200D7DEC
   304045534 STRT 00000000 00000001
   304045844 DEND 00000500 00000007
   304045848 CM12 00000000 00000000
   304045852 DONE 00000000 00000000
   304045864 READ 00003141 200D7DEC
   304045869 STRT 00000000 00000001
   304046068 DEND 00000500 00000007
   304046072 CM12 00000000 00000000
   304046076 DONE 00000000 00000000
   304046081 READ 00003640 200C1600
   304046086 STRT 00000000 00000008
   304046086 PROG 00001000 00000400
   304046294 PROG 00000E00 00000380
   304046325 PROG 00000C00 00000300
   304046357 PROG 00000A00 00000280
   304046389 PROG 00000800 00000200
   304046420 PROG 00000600 00000180
   304046452 PROG 00000400 00000100
   304046484 PROG 00000200 00000080
   304046516 PROG 00000000 00000000
   304046516 DEND 00000500 00000007
   304046520 CM12 00000000 00000000
   304046524 DONE 00000000 00000000
   304265084 LINE 000182F9 0000000A
   304265089 LINE 00018300 0000000A
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 17 ms)
V5F: LiteForth starting, booting from flash
V3F update: version 3 confirmed
```

#### rollback

HANG.lfu installed. It never starts, so the V3F resets the chip each
time it waits for it in vain: three trial boots, then the image before
it is rolled back and starts LiteForth, with no resets from here.

- PASS: the hanging image staged
Debug UART:

```
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 17 ms)
```
USB terminal:

```
ok>ymodem: send the file(s) now (YMODEM)
C.C<YMODEM>C.ymodem: v5f-2-hang.lfu: staged in lfstage.bin, 3 blocks, read back and checked
ymodem: package: V5F image, 4096 bytes, version 2: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>
```
- PASS: installed (V3F update: version 2 installed, on trial)
- PASS: by itself: didn't start within (V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip)
- PASS: by itself: boot 1 of 3 (V3F update: version 2 on trial, boot 1 of 3)
- PASS: by itself: boot 2 of 3 (V3F update: version 2 on trial, boot 2 of 3)
- PASS: by itself: boot 3 of 3 (V3F update: version 2 on trial, boot 3 of 3)
- PASS: by itself: rolled back to version (V3F update: rolled back to version 3)
- PASS: by itself: LiteForth starting (V5F: LiteForth starting, booting from flash)
Debug UART:

```
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F update: version 2 installed, on trial
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F update: version 2 on trial, boot 1 of 3
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F update: version 2 on trial, boot 2 of 3
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F update: version 2 on trial, boot 3 of 3
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F update: rolled back to version 3
V3F: starting the V5F
V5F crumbs: reset by software, boot 4; the previous run left 298 crumbs, the last:
V3F: the V5F is running
     3274820 DONE 00000000 00000000
V3F: built Oct 18 2026 22:20:41, GCC 12.2.0
     3274823 READ 00003140 200D7DEC
     3274828 STRT 00000000 00000001
     3275036 DEND 00000500 00000007
     3275040 CM12 00000000 00000000
     3275044 DONE 00000000 00000000
     3275057 READ 00003640 200D79EC
     3275062 STRT 00000000 00000001
     3275262 DEND 00000500 00000007
     3275266 CM12 00000000 00000000
V3F: USB configured
     3275270 DONE 00000000 00000000
     3275275 MAP  00003640 0000007A
     3345178 READ 00003140 200D7DEC
     3345183 STRT 00000000 00000001
     3345492 PROG 00000000 00000000
     3345493 DEND 00000500 00000007
     3345497 CM12 00000000 00000000
     3345501 DONE 00000000 00000000
     3345513 READ 00003141 200D7DEC
     3345518 STRT 00000000 00000001
     3345519 PROG 00000200 00000080
     3345716 DEND 00000500 00000007
     3345720 CM12 00000000 00000000
     3345725 DONE 00000000 00000000
     3345730 READ 00003640 200C1600
     3345735 STRT 00000000 00000008
     3345735 PROG 00001000 00000400
     3345942 PROG 00000E00 00000380
     3345974 PROG 00000C00 00000300
     3346006 PROG 00000A00 00000280
     3346037 PROG 00000800 00000200
     3346069 PROG 00000600 00000180
     3346101 PROG 00000400 00000100
     3346133 PROG 00000200 00000080
     3346164 PROG 00000000 00000000
     3346165 DEND 00000500 00000007
     3346169 CM12 00000000 00000000
     3346173 DONE 00000000 00000000
     3566487 LINE 00003154 0000000A
     3566492 LINE 0000315B 0000000A
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 17 ms)
V5F: LiteForth starting, booting from flash
```

#### recovery

Ctrl+X x3, then Ctrl+X x3 again while the V5F restarts: the chip
resets into recovery. GOOD.lfu sent to stage 1 itself is installed
(the image marked as not working first, so no backup), on trial, and
confirmed.

- PASS: USB terminal open
- PASS: Ctrl+X x3 twice: "resetting into recovery"
- PASS: recovery mode (V3F: recovery mode: the V5F stays off; waiting for a package over USB)
- PASS: USB terminal open in recovery
- PASS: the recovery prompt
- PASS: GOOD.lfu sent to stage 1
- PASS: the image marked as not working
- PASS: installed
- PASS: restarted, on trial
- PASS: confirmed
Debug UART:

```
Ctrl+X x3: restarting the V5F in safe mode
V5F crumbs: reset by the V3F, boot 5; the previous run left 59 crumbs
V5F restarted
Ctrl+X x3 again: the V5F is stuck; resetting into recovery
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: recovery mode: the V5F stays off; waiting for a package over USB
V3F: built Oct 18 2026 22:20:41, GCC 12.2.0
V3F: USB configured
V3F LiteForth stage 1: recovery mode. The V5F stays off:
V3F Ctrl+X x3 twice: the V5F got stuck before LiteForth started
V3F In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
V3F adopt: take the V5F image in flash as it is (development builds only).
V3F reset: start again.
V3F ymodem: v5f-3.lfu: 98304 bytes received
V3F update: the V5F image marked as not working
V3F update: version 3 installed, on trial
V3F recovery: installed; restarting to try it
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F update: version 3 on trial, boot 1 of 3
V3F: starting the V5F
V5F crumbs: reset by software, boot 6; the previous run left 32 crumbs, the last:
V3F: the V5F is running
       80176 BOOT 00000000 00000000
V3F: built Oct 18 2026 22:20:41, GCC 12.2.0
      115173 CARD 0747B000 00000000
      115174 READ 00000000 200D7DEC
      115179 STRT 00000000 00000001
      115180 PROG 00000200 00000080
      115648 PROG 00000000 00000000
      115649 DEND 00000500 00000007
      115653 CM12 00000000 00000000
      115657 DONE 00000000 00000000
V3F: USB configured
      115658 READ 00000800 200D7DEC
      115663 STRT 00000000 00000001
      115663 PROG 00000200 00000080
      115871 DEND 00000500 00000007
      115875 CM12 00000000 00000000
      115879 DONE 00000000 00000000
      115881 READ 00003140 200D7DEC
      115886 STRT 00000000 00000001
      116094 DEND 00000500 00000007
      116098 CM12 00000000 00000000
      116103 DONE 00000000 00000000
      116106 READ 00003140 200D7DEC
      116110 STRT 00000000 00000001
      116318 DEND 00000500 00000007
      116322 CM12 00000000 00000000
      116326 DONE 00000000 00000000
      116339 READ 00003640 200D79EC
      116344 STRT 00000000 00000001
      116543 PROG 00000000 00000000
      116544 DEND 00000500 00000007
      116548 CM12 00000000 00000000
      116552 DONE 00000000 00000000
      116558 MAP  00003640 0000007A
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 17 ms)
V5F: LiteForth starting, booting from flash
V3F update: version 3 confirmed
```
USB terminal:

```
ok>LiteForth stage 1: recovery mode. The V5F stays off:
Ctrl+X x3 twice: the V5F got stuck before LiteForth started
In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
adopt: take the V5F image in flash as it is (development builds only).
reset: start again.
recovery ok> -2 ymodem
C.C<YMODEM>C.
```

#### lockdown

The lock-down check at boot: what this board reports.

- The board is not locked down: `V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)`
- PASS: the boot reported
Debug UART:

```
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: starting the V5F
V5F crumbs: reset by software, boot 7; the previous run left 59 crumbs, the last:
V3F: the V5F is running
     1590859 STRT 00000000 00000001
V3F: built Oct 18 2026 22:20:41, GCC 12.2.0
     1591067 DEND 00000500 00000007
     1591071 CM12 00000000 00000000
     1591075 DONE 00000000 00000000
     1591088 READ 00003640 200D79EC
     1591093 STRT 00000000 00000001
     1591293 PROG 00000000 00000000
     1591293 DEND 00000500 00000007
     1591297 CM12 00000000 00000000
V3F: USB configured
     1591302 DONE 00000000 00000000
     1591308 MAP  00003640 0000007A
     1661165 READ 00003140 200D7DEC
     1661170 STRT 00000000 00000001
     1661171 PROG 00000200 00000080
     1661479 PROG 00000000 00000000
     1661480 DEND 00000500 00000007
     1661484 CM12 00000000 00000000
     1661488 DONE 00000000 00000000
     1661501 READ 00003141 200D7DEC
     1661505 STRT 00000000 00000001
     1661506 PROG 00000200 00000080
     1661704 DEND 00000500 00000007
     1661708 CM12 00000000 00000000
     1661712 DONE 00000000 00000000
     1661717 READ 00003640 200C1600
     1661722 STRT 00000000 00000008
     1661722 PROG 00001000 00000400
     1661930 PROG 00000E00 00000380
     1661961 PROG 00000C00 00000300
     1661993 PROG 00000A00 00000280
     1662025 PROG 00000800 00000200
     1662057 PROG 00000600 00000180
     1662088 PROG 00000400 00000100
     1662120 PROG 00000200 00000080
     1662152 DEND 00000500 00000007
     1662156 CM12 00000000 00000000
     1662160 DONE 00000000 00000000
     1784398 QUIT 00000079 00000008
     1961162 LINE 00000001 0000000A
     1961167 LINE 00000008 0000000A
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 17 ms)
V5F: LiteForth starting, booting from flash
```

Build: `V3F: built Oct 18 2026 22:20:41, GCC 12.2.0`

**All passed.**

**Result:** pass, all four scenarios.
- **Rollback:** the hung image went through its three trial boots and was rolled back in about 15 seconds, with no reset from outside. Each time, the V3F gave up on the V5F after 3 s and reset the chip itself. The "1 in a row" count started over at every trial boot, because an update was under way; the trial counted those boots instead.
- **Recovery:** the second Ctrl+X x3 arrived while the V5F was still mounting the card after the first one, which gave recovery on the first try. Stage 1 then took version 3 over USB, marked the installed image as not working, installed version 3 without a backup, and LiteForth confirmed it.
- **Crumbs:** every software reset now lists the V5F's last crumbs, about 40 lines. At 9600 baud that adds a few seconds to each boot.

### B7. The I/O allowlist, `SECURITY_LEVEL` 1 (19 October 2026)

What Forth can reach in the peripheral page (`VM_IO_PAGE`, VM addresses
`280000H` and up: register `40000000H + 4c` is at `280000H + c`). At
`SECURITY_LEVEL` 1 and 2 every access goes through the allowlist in
`V5F/User/io_allow.h`; at level 0 Forth reaches every register.

**Firmware:**
- Branch `io-allowlist` at 5452018 (PR #183), built by the board's owner in MounRiver.
- Baseline: the firmware from before the allowlist (B6's), which is level 0's map.
- Test: both cores built with `SECURITY_LEVEL` 1.
- V3F built Oct 19 2026 09:35:17, GCC 12.2.0. The image hashes weren't recorded.

**Debug UART at the start of the level 1 run:**

```
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: starting the V5F
V5F crumbs: reset by pin, boot 2; the previous run left 54 crumbs
V3F: the V5F is running
V3F: built Oct 19 2026 09:35:17, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file, revision 6, release key 1, AES-128, 121 blocks (checked in 17 ms)
V5F: LiteForth starting, flash is blank
V3F: USB configured
```

At level 1 an open board gets the warning but still starts the V5F (at
level 2 it would stay in recovery). The board key still comes from the
UID: the sealed card made for B3-B6 mounted. LiteForth's flash was blank
after the download, so only the built-in words were there; none of the
test needs more.

**Registers used:**
- GPIOA (`40010800H`, `284200H`): allowed, read and write.
- RCC (`40021000H`, `288400H`, the clock control register): read only.
- FLASH (`40022000H`, `288800H`, the flash controller): not listed.
- SDMMC (`40024000H`, `289000H`): not listed.

**Expected at level 1:**
- GPIOA reads and writes work.
- RCC reads work; a write gives -20 (write address prohibited) and changes nothing.
- FLASH and SDMMC reads and writes give -9 (invalid memory address).

**Baseline, level 0** (typed by hand in kwai; `.` with option 200H prints plain hex):
- `284200 @ .` gave `44444444H`.
- `288800 @ .` gave `4801H`: Forth reads the flash controller.
- `0 288400 !` was accepted: it wrote 0 to RCC's clock control register. The board stayed up; `288400 @ .` then read `3336983H`, so the clocks the chip runs on stayed on. `reboot` reset it.

**Level 1, first try, typed by hand:**
- `284200 @ .` gave `44444444H`.
- `288800 @ .` gave -9.
- The RCC lines after it also gave -9, but they don't count: after an error LiteForth sets `BASE` back to decimal (`quitReset` in `forth.c`), so `288400` was read as decimal, an address past the end of flash page 0. Every line below starts with `hex` so that can't happen.

**Level 1, run through COM17 by Claude (the LiteForth MCP tool, which sends each line and waits for `ok>`):**

```
> hex 200 >options
ok>
> hex 284200 @ .
8:11:44444H ok>
> hex 288800 @ .
Error: ior=-9 invalid memory address
ok>
> hex 288400 @ .
3336983H ok>
```

The FLASH read was refused, so this was the level 1 build. Only then were the writes sent:

```
> hex 0 288400 !
Error: ior=-20 write address prohibited
ok>
> hex 288400 @ .
3336983H ok>
> hex 0 288800 !
Error: ior=-9 invalid memory address
ok>
> hex 289000 @ .
Error: ior=-9 invalid memory address
ok>
> hex 0 284200 !
ok>
> hex 284200 @ .
0H ok>
> hex 44444444 284200 !
ok>
> hex 284200 @ .
8:11:44444H ok>
```

`8:11:44444H` is `44444444H` printed as a slice address. `200 >options`
had turned the plain-hex option off again, since the earlier session had
already set it.

**Result:** pass.
- GPIOA read and written: the write set PA0-PA7 to analog inputs for a moment, then wrote back the reset value `44444444H`.
- RCC read, and its write refused with -20. The register read the same before and after.
- The flash controller and SDMMC refused with -9, for a read and a write.

### B8. Lock-down of the spare board with lockdown.py (19 October 2026)

The first lock-down. The spare board, UID `e52116647f9c7a22`, went from
fresh from WCH to read protection on, the V3F's sectors write-protected and
both boot ROM downloads off, with `scripts/admin/lockdown.py` and stage 1's
`lockdown`. Only a chip erase through the WCH-LinkE undoes it.

**Ports:** the cli port, COM6, is the V3F's USB terminal; the debug port,
COM3, is this board's WCH-LinkE UART.

**Firmware:** branch `lockdown` (PR #185), both cores at `SECURITY_LEVEL` 2:
- V3F `LiteForth_V3F.bin`, built Oct 19 2026 12:35:24, GCC 12.2.0: 62,904
  bytes, sha256 `8bdc3206008af9d955956064dea6be024cbf72ee21f53d6670fa8adfbfc72fd1`.
- V5F `LiteForth_V5F.bin`: 90,032 bytes, sha256
  `fdf7aedcad0c4d0a56bf09a191a6d4762836ea3d80dba1499573d9371a5979bb`.
- Both were flashed with wlink at 0x08000000 and 0x08010000, and read back,
  in the 13:06 run below.

**Starting state:** OBR `000003FC`, WPR `FFFFFFFF`: read protection off,
nothing write-protected, both downloads on. No SD card.

**Runs** (the lockdown.py reports, `lockdown-report-20261019-*.md`):

| Time | Command | What happened |
|---|---|---|
| 12:26 | `run` on the main board (UID `69a90e64...`) | Flashed level 1 images; stage 1 didn't stay in recovery, so the script stopped: not a level 2 build. Nothing locked. |
| 12:41 | `run --usb COM3` | Stopped before flashing: the debug port default (COM14) didn't exist. |
| 12:46 | `run --debug COM3` | Flashed and read back both level 2 images; stage 1 said recovery; no cli port after the reset. |
| 13:06 | `run --debug COM3 --usb COM6` | Flashed both images; the read-back's attach failed (`protocol error 0x55`). |
| 13:19 | the same | The flash's attach failed: nothing flashed. |
| 13:32 | `lock` | COM6 "not functioning": the script's wlink reads had left the V3F halted. |
| 13:43 | `lock` | After wlink's first two calls, every attach failed (`resume`, `reset`). |
| 13:49 | `lock`, wlink not used before the lock | Locked: see below. |

**13:49, the lock.** The recovery console, before:

```
LiteForth stage 1: recovery mode. The V5F stays off:
security level 2: this board isn't locked down, so the V5F won't start
read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
lockdown: lock this board down (it says what it will do first).
reset: start again.
recovery ok>
```

`LOCK` was typed, and the script sent `lockdown yes`. The debug port:

```
V3F: lockdown: programming the option bytes
V3F: lockdown: option bytes now RDPR 00FF USER C03F WRPR0 FF00: NOT AS WRITTEN
```

The data bytes were right: RDPR `FF`, so read protection on; USER `3F`, so
both downloads off; WRPR0 `00`, so sectors 0-7 protected. The chip had
written each complement byte itself, so the whole-halfword comparison
failed. Stage 1 therefore skipped its reset and the script stopped. This is
fixed in PR #185 (the data bytes only), and the finding is in
[ch32h417-notes.md](../ch32h417-notes.md).

**After a hard reset**, as reported: the V5F booted normally. A level 2
stage 1 starts the V5F only when its lock-down check (OBR and WPR) passes,
so the option bytes loaded. The debug port's text of that boot wasn't kept.

**Result:** pass, the board is locked down.
- **wlink flashes this dual-core part:** the V5F image it wrote at 0x10000
  runs.
- **The option bytes loaded at a hard reset.** It's not yet known whether a
  software reset alone would load them.
- **wlink halts the V3F to read it, and leaves it halted:** its USB stops
  answering, and a soft reset halts it again. On a board in recovery, the
  WCH-LinkE then often fails to attach (`0x55`) until a power cycle. So
  lockdown.py's `lock` uses no wlink before the lock, and lfmcp's `wlink`
  tool resumes the core after its reads.

**Still to do on this board:**
- `wlink status` under read protection;
- `enroll`, a flash write by the V5F under read protection;
- an update installed by stage 1, which runs from RAM_CODE: the reference
  manual says code running from RAM can't program flash under read
  protection;
- a rollback, and recovery over USB.

### B9. A flash write by the V5F under read protection: `enroll` (19 October 2026)

**Superseded:** by B22. The board record this wrote at 0x08070000 is gone: the board's key is stage 1's now, under PMP entry 0. Kept as the record of what was run.

**Board and firmware:** the spare board, locked down in B8, with the same
images.

**Why:** the reference manual says code running from RAM can't program
flash while read protection is on. `enroll` makes the board's X25519 key
from the RNG (level 2) and writes it to the board record at 0x08070000,
so it shows whether the V5F's flash writes still work.

**Run,** on the cli port:

```
enroll
enroll: new key made. boards.csv line:
e52116647f9c7a22,b2e2e4cc1b9b1627469204202135c3aac360573994bb984930ab34d32939055e
ok>reboot

[kwai: COM6 lost; waiting for it to come back]

[kwai: COM6 is back]
enroll
boards.csv line:
e52116647f9c7a22,b2e2e4cc1b9b1627469204202135c3aac360573994bb984930ab34d32939055e
ok>
```

**Result:** pass. The first `enroll` wrote a new key to flash. After the
reboot, the second found the record and printed the same line without
making a new key. So the V5F programs flash under read protection, and the
record survives a reset. Stage 1's own writes (the security record, an
update into the V5F slot) are still untested under read protection.

### B10. An update installed by stage 1 under read protection (19 October 2026)

**Board and firmware:** the spare board, locked down in B8 and enrolled in
B9 (its key's line: `e52116647f9c7a22,b2e2e4cc...055e`), with the same
images. The card came from the main board: its `lfblocks.fb4` is revision
6, sealed for the main board, and it holds `lfstage.bin` and
`lfbackup.bin`.

**Package:** `v5f-spare-1.lfu`, sha256
`b5052b030378c99a973757e55246371d631964de6b01a56259cb12547eb7c92c`. It is
the V5F image the board already runs (sha256 `fdf7aedc...79bb`) as version
1, sealed with the test release key 1 for this board's key only:
`sealblocks.py package LiteForth_V5F.bin v5f-spare-1.lfu --component v5f
--release release1.key --boards boards-spare.csv --revision 1`.
`sealblocks.py check` passed it against the test root.

**Why:** stage 1 runs from RAM_CODE, and the reference manual says code
running from RAM can't program flash while read protection is on. An
install writes the security record (0x72000, 0x74000) and the V5F slot
(0x10000) from stage 1.

**Run,** on the cli port:

```
ok>~x ..\mailbox\v5f-spare-1.lfu -2
ymodem: send the file(s) now (YMODEM)

[kwai: sending ..\mailbox\v5f-spare-1.lfu: 98304 bytes]
[kwai: 96/96]
[kwai: sent ..\mailbox\v5f-spare-1.lfu]
ymodem: v5f-spare-1.lfu: staged in lfstage.bin, 24 blocks, read back and checked
ymodem: package: V5F image, 90032 bytes, version 1: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>reboot

[kwai: COM6 lost; waiting for it to come back]

[kwai: COM6 is back]
```

The debug port, from the boot before the staging:

```
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V3F update: version 1 installed, on trial
V3F: starting the V5F
V5F crumbs: reset by software, boot 3; the previous run left 1795 crumbs
V3F: the V5F is running
V3F: built Oct 19 2026 12:35:24, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V5F: LiteForth starting, flash is blank
V3F update: version 1 confirmed
V3F: USB configured
```

**Result:** pass.
- **Stage 1 installed the package under read protection:** it wrote the
  new security record and the V5F slot from RAM_CODE. The image started on
  trial and LiteForth confirmed it. So the reference manual's limit on code
  running from RAM doesn't apply to stage 1 here.
- **A file sealed for another board was refused** on every boot: the card's
  revision 6, sealed for the main board.
- **The package was checked against this board's RNG key** (B9): both by
  LiteForth's `-2 ymodem` and by stage 1.

### B11. Install, rollback, recovery and lock-down on the locked board, run by hiltest.py (19 October 2026)

B6's scenarios again, on the spare board locked down in B8, with nobody at
the board. hiltest.py uses no wlink; every reset was `reboot`, `reset` in
recovery, or the V3F's own.

**Board and firmware:** the spare board, as in B8 to B10, version 1
installed and confirmed (B10). The card is still the main board's, so its
`lfblocks.fb4` (sealed for the main board) is refused at every boot.

**Packages,** both for this board's key only, sealed with the test release key 1:
- GOOD, `v5f-spare-3.lfu`: B10's V5F image (sha256 `fdf7aedc...79bb`) as
  version 3.
- HANG, `v5f-spare-2-hang.lfu`: B4's `j .` image (sha256
  `5778694d...3031`) as version 2.

**Command:** `py ..\scripts\admin\hiltest.py run --good ..\mailbox\v5f-spare-3.lfu --hang ..\mailbox\v5f-spare-2-hang.lfu --debug COM3 --cli COM6`

The report, as hiltest.py wrote it (`hil-report-20261019-1411.md`):

Run 2026-10-19 14:11:31; debug port COM3, cli port COM6, reset by `reboot`.

- GOOD: `v5f-spare-3.lfu`, sha256 b80dbc2116475734a41b63280d082ca047fdc5d301e5a6acffde89f62d9e1b60
- HANG: `v5f-spare-2-hang.lfu`, sha256 557c46da3be0b032681b05868cab17a87032cc9cfafa48a2cbf9b94b313b835f

#### install

GOOD.lfu staged by LiteForth's `-2 ymodem`, installed at the next
reset, started on trial and confirmed.

- PASS: staged: "waiting for stage 1 to install it"
Debug port:

```
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
```
Cli port:

```
ok>ymodem: send the file(s) now (YMODEM)
C.C<YMODEM>C.ymodem: v5f-spare-3.lfu: staged in lfstage.bin, 24 blocks, read back and checked
ymodem: package: V5F image, 90032 bytes, version 3: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>
```
- **FAIL**: installed (V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients)
- PASS: confirmed by LiteForth
Debug port:

```
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V3F update: version 3 installed, on trial
V3F: starting the V5F
V5F crumbs: reset by software, boot 4; the previous run left 1796 crumbs
V3F: the V5F is running
V3F: built Oct 19 2026 12:35:24, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V5F: LiteForth starting, flash is blank
V3F update: version 3 confirmed
```

#### rollback

HANG.lfu installed. It never starts, so the V3F resets the chip each
time it waits for it in vain: three trial boots, then the image before
it is rolled back and starts LiteForth, with no resets from here.

- PASS: the hanging image staged
Debug port:

```
V3F: USB configured
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
```
Cli port:

```
ok>ymodem: send the file(s) now (YMODEM)
C.C<YMODEM>C.ymodem: v5f-spare-2-hang.lfu: staged in lfstage.bin, 3 blocks, read back and checked
ymodem: package: V5F image, 4096 bytes, version 2: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>
```
- **FAIL**: installed (V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients)
- PASS: by itself: didn't start within (V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip)
- PASS: by itself: boot 1 of 3 (V3F update: version 2 on trial, boot 1 of 3)
- PASS: by itself: boot 2 of 3 (V3F update: version 2 on trial, boot 2 of 3)
- PASS: by itself: boot 3 of 3 (V3F update: version 2 on trial, boot 3 of 3)
- PASS: by itself: rolled back to version (V3F update: rolled back to version 3)
- PASS: by itself: LiteForth starting (V5F: LiteForth starting, flash is blank)
Debug port:

```
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V3F update: version 2 installed, on trial
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F update: version 2 on trial, boot 1 of 3
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F update: version 2 on trial, boot 2 of 3
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F update: version 2 on trial, boot 3 of 3
V3F: starting the V5F
V3F: the V5F didn't start within 3000 ms (1 in a row): resetting the chip
V3F update: rolled back to version 3
V3F: starting the V5F
V5F crumbs: reset by software, boot 5; the previous run left 278 crumbs
V3F: the V5F is running
V3F: built Oct 19 2026 12:35:24, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V5F: LiteForth starting, flash is blank
```

#### recovery

Ctrl+X x3, then Ctrl+X x3 again while the V5F restarts: the chip
resets into recovery. GOOD.lfu sent to stage 1 itself is installed
(the image marked as not working first, so no backup), on trial, and
confirmed.

- PASS: cli port open
- PASS: Ctrl+X x3 twice: "resetting into recovery"
- PASS: recovery mode (V3F: recovery mode: the V5F stays off; waiting for a package over USB)
- PASS: cli port open in recovery
- PASS: the recovery prompt
- PASS: GOOD.lfu sent to stage 1
- PASS: the image marked as not working
- PASS: installed
- PASS: restarted, on trial
- PASS: confirmed
Debug port:

```
V3F: USB configured
Ctrl+X x3: restarting the V5F in safe mode
V5F crumbs: reset by the V3F, boot 6; the previous run left 58 crumbs
V5F restarted
Ctrl+X x3 again: the V5F is stuck; resetting into recovery
V3F: recovery mode: the V5F stays off; waiting for a package over USB
V3F: built Oct 19 2026 12:35:24, GCC 12.2.0
V3F: USB configured
V3F LiteForth stage 1: recovery mode. The V5F stays off:
V3F Ctrl+X x3 twice: the V5F got stuck before LiteForth started
V3F In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
V3F reset: start again.
V3F ymodem: v5f-spare-3.lfu: 98304 bytes received
V3F update: the V5F image marked as not working
V3F update: version 3 installed, on trial
V3F recovery: installed; restarting to try it
V3F update: version 3 on trial, boot 1 of 3
V3F: starting the V5F
V5F crumbs: reset by software, boot 7; the previous run left 31 crumbs
V3F: the V5F is running
V3F: built Oct 19 2026 12:35:24, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V5F: LiteForth starting, flash is blank
V3F update: version 3 confirmed
```
Cli port:

```
ok>LiteForth stage 1: recovery mode. The V5F stays off:
Ctrl+X x3 twice: the V5F got stuck before LiteForth started
In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
reset: start again.
recovery ok> -2 ymodem
C.C<YMODEM>C.
```

#### lockdown

The lock-down check at boot: what this board reports.

- The board is locked down: `V5F: LiteForth starting, flash is blank`
- PASS: the boot reported
Debug port:

```
V3F: USB configured
V3F: starting the V5F
V5F crumbs: reset by software, boot 8; the previous run left 61 crumbs
V3F: the V5F is running
V3F: built Oct 19 2026 12:35:24, GCC 12.2.0
V5F blocks: lfblocks.fb4, 122 blocks at sector 13888 (exFAT)
V5F blocks: sealed file refused: sealed file refused: this board isn't one of its recipients
V5F: LiteForth starting, flash is blank
```

Build: `V3F: built Oct 19 2026 12:35:24, GCC 12.2.0`

**2 failed.**

**Result:** pass, all four scenarios. The report's two FAILs are hiltest's
mistake, not the board's.
- **Why "installed" failed:** the check took the first line that said
  "refused". That was the V5F refusing the card's `lfblocks.fb4`, which has
  nothing to do with the update. The next line says `version 3 installed,
  on trial` (and `version 2` for the rollback), and both were then
  confirmed or rolled back as planned. hiltest.py now reads only stage 1's
  `V3F update:` lines for this (PR #185).
- **Under read protection, stage 1 wrote flash every time:**
  - two installs;
  - three trial boot counts;
  - a rollback from `lfbackup.bin`;
  - marking an image as not working;
  - an install from recovery without a backup.
- **The lock-down scenario** found no lock-down warning, and LiteForth
  started: the board is locked down.

### B12. The V5F's PMP locked over stage 1's records, at power-on (20 October 2026)

**Superseded:** by B18 and B22. The records it fenced (at 0x08072000, by NAPOT entries) moved below the V5F's slot, under entry 0 (TOR). Kept as the record of what was run.

**Board and firmware:** the main board (UID `69a90e647f9cf6a9`), not
locked down, `SECURITY_LEVEL` 0. The V5F built from branch `pmp` at
`36629a4` (PR #194); the V3F built 20 October 2026 10:20:30 with GCC
12.2.0.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62248 | `52f524d38e6dcb40c454c4204488a66d711b866a51bdb7cf8cd9c5c87dcc1525` |
| `LiteForth_V5F.bin` | 90836 | `ce774d68f29c1b4c7dd62a57c10eb0f462ee85516eee4e96863981dde7f5e7dd` |

**Why:** `pmp_lock` (`V5F/User/pmp.c`), called by the V5F's start-up code
in machine mode before its `mret` into `main`, locks four NAPOT entries:
no access to 0x00070000-0x00077FFF and to stage 1's records at 0x08072000
and 0x08074000 (the device token), then all memory RWX. Locked entries
also apply in machine mode. It then tries to clear entry 0, and
`pmp_report` prints what the CSRs read back.

**Expected:** the V5F starts normally; the entries read back as set and
stay set after the attempt to clear entry 0.

**Run:** power-on reset; the debug port:

```
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: starting the V5F
V5F crumbs: reset by pin, boot 1; the previous run left 2 crumbs
V3F: the V5F is running
V5F PMP: cfg0 9F989898, addr 0001CFFF 0201CBFF 0201D3FF 3FFFFFFF: NOT as set, locked (clearing entry 0 was ignored)
V3F: built Oct 20 2026 10:20:30, GCC 12.2.0
V5F blocks: no SD card (error 3)
V5F: LiteForth starting, flash is blank
V3F: USB configured
```

**Result:** pass. The V5F started with all four entries locked, and
clearing entry 0 after the lock was ignored. "NOT as set" is the report
being too strict: `pmpaddr3` was written as `0xFFFFFFFF` and reads back
`0x3FFFFFFF`, because the core keeps only 30 bits of `pmpaddr`. In NAPOT
mode that's still a region covering all of memory. pmp.c now writes
`0x1FFFFFFF` (4 GB from 0). That an access to a fenced record faults is
B13.

**Again with the fix** (PR #194 at `c4faf99` and later, with no test
line), after a power-on:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62248 | `52f524d38e6dcb40c454c4204488a66d711b866a51bdb7cf8cd9c5c87dcc1525` (unchanged) |
| `LiteForth_V5F.bin` | 90852 | `e5914328e96affe5c368594daa57c10adeeaed518f32e6b137f658a13d484bd9` |

```
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: starting the V5F
V5F crumbs: reset by software power; none from before (power-up)
V3F: the V5F is running
V5F PMP: cfg0 9F989898, addr 0001CFFF 0201CBFF 0201D3FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 20 2026 10:20:30, GCC 12.2.0
V5F blocks: no SD card (error 3)
V5F: LiteForth starting, flash is blank
V3F: USB configured
```

**Result:** pass: all four entries as set and locked.

### B13. A read of stage 1's record from the V5F faults under the PMP (20 October 2026)

**Superseded:** by B18 and B22. The record it read (at 0x08072000) moved below the V5F's slot, under entry 0. Kept as the record of what was run.

**Board and firmware:** the main board, as in B12, with the V5F from PR
#194 and one test line added by hand to `lfRtcSet` in
`V5F/User/lftime.c`, deleted afterwards:

```c
int lfRtcSet(uint32_t seconds) {
    return *(volatile uint32_t *)0x08072000UL; // delete this line
```

Image hashes weren't recorded for this build.

**Why:** B12 showed the entries locked, not that they stop an access.
LiteForth's C code runs in user mode (WCH's start-up `mret`s into `main`
with MPP = 0), so this shows whether a user-mode load from a locked
no-access entry faults, and that the fault comes back to Forth through
`vmTrap` (`rtc!` runs inside a VM run).

**Expected:** `0 rtc!` gives -127 and a `-> Forth` fault line, with no
reset.

**Run,** on the cli port:

```
ok>0 rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
```

The debug port:

```
V5F fault: mcause 00000005 mepc 200AC6DE mtval 08072000 ra 200A24C6 sp 200FFCD0 -> Forth
```

**Result:** pass. The load faulted as a load access fault (mcause 5) with
mtval the record's address, and the terminal carried on. So the PMP
checks user-mode accesses, and the device token can't be read by the
V5F's code. Still open: whether a user-mode access that matches no entry
fails (the catch-all, entry 3, matches everything here).


### B14. The flash controller and the board record behind the PMP, with the ecall service (20 October 2026)

**Board and firmware:** the main board (UID `69a90e647f9cf6a9`), not
locked down, `SECURITY_LEVEL` 0. The V5F built from branch `pmp-ecall`
(PR #195), with one test line added by hand to `lfRtcSet` in
`V5F/User/lftime.c`, deleted afterwards:

```c
return *(volatile uint32_t *)0x40022000UL; // delete this line
```

The V3F as in B12.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62248 | `52f524d38e6dcb40c454c4204488a66d711b866a51bdb7cf8cd9c5c87dcc1525` (as in B12) |
| `LiteForth_V5F.bin` | 91196 | `7716869ad79ed966cfa8be56e788ed4df935a24490caa52c822a0a38c838dba0` (with the test line) |

**Why:** PR #195 adds two unlocked PMP entries: no user-mode access to
0x08070000-0x08077FFF (the board record and stage 1's records) or to the
flash controller at 0x40022000. LiteForth runs in user mode, so its flash
writes now go through an `ecall` service in machine mode
(`Ecall_U_Mode_Handler`, `V5F/User/pmp.c`). This checks the entries, that
the service programs flash (a fault in a trap handler would lock the core
up), and that user mode can't reach the flash controller.

**Expected:** the boot line shows `cfg0 9F181898`, as set and locked;
`close-flash` programs the page and `foo` runs; `0 rtc!` gives -127 with
mcause 5 and mtval 40022000, and no reset.

**Run,** on the cli port:

```
0 open-flash
ok>: foo 42 . ;
ok>close-flash
ok>foo
42 ok>
ok>0 rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
```

The debug port:

```
V5F PMP: cfg0 9F181898, addr 0001CFFF 0201CFFF 100089FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 20 2026 10:20:30, GCC 12.2.0
V5F blocks: no SD card (error 3)
V5F: LiteForth starting, flash is blank
V3F: USB configured

V5F fault: mcause 00000005 mepc 200AC69E mtval 40022000 ra 200A2474 sp 200FFCD0 -> Forth
```

Then `enroll`, which reads the board record through the service, and
writes it if there's none, on the cli port:

```
ok>enroll
enroll: new key made. (security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>reboot

[kwai: COM17 lost; waiting for it to come back]

[kwai: COM17 is back]
enroll
(security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>
```

The debug port at that reboot:

```
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: starting the V5F
V5F crumbs: reset by software, boot 1; the previous run left 11 crumbs
V3F: the V5F is running
V5F PMP: cfg0 9F181898, addr 0001CFFF 0201CFFF 100089FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 20 2026 10:20:30, GCC 12.2.0
V5F blocks: no SD card (error 3)
V5F: LiteForth starting, flash is blank
V3F: USB configured
```

**Result:** pass. All four entries as set; `close-flash` programmed the
page through the machine-mode service; a user-mode read of the flash
controller faulted (load access fault) and came back to Forth. The first
`enroll` found no record and wrote one through the service; after the
reboot the second read it back through the service and printed the same
line, without making a new key. So every flash write and record access
the V5F makes now goes through machine mode, and none faulted there.

### B15. Guarded DMA: PMP entry 2 over the DMA registers, RCC/CRC store emulation (20 October 2026)

**Board and firmware:** the main board (UID `69a90e647f9cf6a9`), not
locked down (OBR `000003FC`, WPR `FFFFFFFF`), `SECURITY_LEVEL` 0. The
V5F built in MounRiver from branch `guarded-dma` (commit `205455f`),
flashed with wlink from `LiteForth_V5F.hex` (94040 bytes at
0x08010000). The V3F as in B12. The card holds a sealed `lfblocks.fb4`
(block 0 starts `LITEFORTHS`), 121 blocks, so it's mounted read-only.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62248 | `52f524d38e6dcb40c454c4204488a66d711b866a51bdb7cf8cd9c5c87dcc1525` (as in B12) |
| `LiteForth_V5F.bin` | 94040 | `017737b138c5d86cdd9b562f7490559f9c481f3116f7e661fa34e47c5c0eac21` |

**Why:** a DMA controller isn't bound by the PMP, so user mode (all of
LiteForth) mustn't program one. PMP entry 2 now covers 0x40020000-0x40023FFF
(DMA1, DMA2, DMAMUX, RCC, the flash controller, CRC, USBFS, the RNG),
read-only to user mode. The SD card's DMA goes through the Guarded DMA
service (`gdma.c`) in machine mode, with its own 16 KB buffer, burned
after every transfer; a run over 32 sectors is split into several SD
commands. User-mode stores to RCC and CRC fault and are done by the
HardFault handler (`guard.c` decodes them). This checks that the image
boots (WCH's drivers write RCC from user mode during start-up), that
Forth's stores to the DMA registers and the RNG fault back to Forth,
that stores to RCC and CRC still take effect, and that card reads,
including a split 64-sector one, are right.

**Expected:** a normal start; `!` to DMA2 channel 1's CFGR and to the RNG
give -127 and no reset; a write to RCC's HBPCENR and to CRC take effect;
an 8-block `read-block` matches single-block reads of the same blocks.

**Run:** the crumb trail after the flash (read through wlink at
0x2017C000) showed `BOOT`, `CARD`, then each card read as `READ`
`CHNK` `STRT` (`PROG`...) `DEND` `CM12` `DONE`, the `MAP`, and `QUIT`
(capacity 0x79), with no `FLT5`, `TRP5`, `GDMA` or `DMA!` crumb, and
the V3F had seen `CDC_CONFIRM_MAGIC`. Then, on the cli port (after a
`wlink reset`: `wlink regs` and `dump` halt the V3F, and the USB port
stopped taking data after them). The I/O page is VM page 5, so register
r is at 0x280000 + (r - 0x40000000) / 4:

```
> hex
ok>
> 288102 @ .
0H ok>
> 288406 @ .
20007H ok>
> 0 288102 !
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
> hex
ok>
> 288102 @ .
0H ok>
> 288406 @ 40 + 288406 !
ok>
> 288406 @ .
20047H ok>
> 1 288C02 !
ok>
> 12345678 288C00 !
ok>
> 288C00 @ .
DF8A8A2BH ok>
> hex
ok>
> 288F00 @ .
0H ok>
> 0 288F00 !
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
> 1 2 + .
3 ok>
```

0x288102 is DMA2 channel 1's CFGR (0x40020408), 0x288406 RCC's HBPCENR
(0x40021018; bit 0x40 is CRC's clock), 0x288C00 and 0x288C02 CRC's
DATAR and CTLR, 0x288F00 the RNG (0x40023C00). DF8A8A2B is the
CRC-32/MPEG-2 of the word 12345678 (computed in Python). An error sets
`BASE` back to decimal, hence the `hex` after each.

Then, after a `reboot`, with flash page 3 opened as a 64 KB RAM cache
(never closed, so no flash write):

```
> hex
ok>
> 180000 open-flash
ok>
> capacity .
79H ok>
> 180000 0 8 read-block
ok>
> block-status .
1H ok>
> block-status .
0H ok>
...
> 180000 @ .
8:15:14494CH ok>
> 182000 3 1 read-block
...
> 180FFF @ . 1823FF @ .
4:0:202020H 4:0:202020H ok>
> 180F00 @ . 182300 @ .
5:14:2D2028H 5:14:2D2028H ok>
> 182000 4 1 read-block
...
> 181000 @ . 182000 @ .
D:5:C2028H D:5:C2028H ok>
> 181080 @ . 182080 @ .
D:11:262028H D:11:262028H ok>
> 182000 7 1 read-block
...
> 181FFF @ . 1823FF @ .
4:0:202020H 4:0:202020H ok>
> 181C00 @ . 182000 @ .
D:5:C2028H D:5:C2028H ok>
> hex
ok>
> 181010 @ . 181C10 @ . 182010 @ .
6:4:2F3031H 7:4:2F3031H 7:4:2F3031H ok>
```

(`...` is `block-status .` lines, `0H ok>` each.) Blocks 0-7 are 64
sectors, moved as two 32-sector commands; block 3's last cell and block
4's first are on either side of the split. Each read of the sealed file
was checked against its hash and decrypted after Guarded DMA copied it
out. A `write-block` of 8 blocks gave -96 (write-protected), as it
should for a sealed file, before anything reached the card.

The debug port at start-up:

```
V5F PMP: cfg0 9F191898, addr 0001CFFF 0201CFFF 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
```

Afterwards, loading the dictionary from the card (blocks read through
Guarded DMA, then committed to flash through the machine-mode flash
service, now reached by `pmp_call3`), on the cli port:

```
ok>1 load
value test: 11 10
Dictionary loaded and committed to flash memory.
If you need Flash writable again, use `0 open-flash`.
_udata used 110 unused 858 cells
_idata used 16 unused 1016 cells
_code  used 1165 unused 7027 cells
_text  used 1357 unused 6835 cells

Type `cold` to reboot, `try` to launch the clock demo.
ok>cold
ok>学如不及, 犹恐失之 try
ok>
```

**Result:** pass, for what it covers. The image starts with entry 2 over
the DMA registers; user-mode RCC writes at start-up and from Forth go
through the emulation; Forth's stores to DMA and the RNG come back as
-127 without a reset; Guarded DMA reads are right, split or not. Entry 2 reads back as
set (`cfg0 9F191898`, `addr` `100087FF`: 16 KB from 0x40020000, user R).
`1 load` and `close-flash` still work. **Not covered:** card writes
through Guarded DMA (needs a plain block file).

### B16. The board record's services through a mailbox (20 October 2026)

**Superseded:** by B22. The board record and its services in the V5F's machine mode are gone: stage 1 holds the key. Kept as the record of what was run.

**Board and firmware:** as in B15 (main board, UID `69a90e647f9cf6a9`,
not locked down, `SECURITY_LEVEL` 0, sealed `lfblocks.fb4` on the card),
with the V5F built in MounRiver from branch `mmode-buffers` (commit
`5022a8f`) and flashed with wlink (94168 bytes at 0x08010000). The V3F
as in B12.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62248 | `52f524d38e6dcb40c454c4204488a66d711b866a51bdb7cf8cd9c5c87dcc1525` (as in B12) |
| `LiteForth_V5F.bin` | 94168 | `a649b1e1435978d03cd8609dee2db38362e9ef04b95f117b3a672456f56b72d6` |

**Why:** `board_serve` wrote its results (a `board_info`, the X25519
shared secret) where user mode pointed it, checked only to lie in DTCM,
which includes the stack below the caller, where the ecall handler's own
frames are. Its services now read and write only a mailbox in `board.c`,
and the user-mode wrappers copy in and out. This checks all three
services: INFO and ENROLL (`enroll`), and X25519 (mounting the sealed
file).

**Expected:** `enroll` prints the same `boards.csv` line as in B14; a
second `enroll` finds the record and makes no key; the sealed file
mounts (capacity 121) and its blocks read as plain text.

**Run,** on the cli port, straight after the flash:

```
ok>学如不及, 犹恐失之
> enroll
enroll: new key made. (security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>
> enroll
(security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>
> capacity .
121 ok>
> hex 1 block 20 dump
200030: 694C2028 6F466574 20687472 656D6F68  ( LiteForth home
200034: 72637320 206E6565 5F5F5F5F 5F5F5F5F   screen ________
200038: 5F5F5F5F 5F5F5F5F 5F5F5F5F 5F5F5F5F  ________________
20003C: 5F5F5F5F 5F5F5F5F 5F5F5F5F 205F5F5F  _______________ 
200040: 312F3031 36322F37 454E4220 20202920  10/17/26 BNE )  
...
ok>
```

The first `enroll` found no valid record and wrote one. B14 had written
one; why it was gone before this run wasn't looked into. At this
security level the key comes from the UID, so it's B14's key either way.
The second read it back through the service.

**Result:** pass. The public key matches B14 (INFO), the record is
written and then found (ENROLL), and the sealed file mounts and decrypts
(X25519): all three services work through the mailbox.

### B17. Card writes through Guarded DMA, and a brown-out (20 October 2026)

**Board and firmware:** as in B16 (the same V5F image,
`a649b1e1435978d03cd8609dee2db38362e9ef04b95f117b3a672456f56b72d6`), but
with a plain block file on the card: `plain.fb4` renamed to
`lfblocks.fb4` (block 0 `LITEFORTHBLK`, 121 blocks, writable), and the
dictionary loaded with `1 load`. Blocks 60-99 were blank (spaces).

**Why:** B15 and B16 covered reads only; the card then held a sealed
file. This checks writes through Guarded DMA: a single block, and 8
blocks (64 sectors) in one `write-block`, which `blocks.c` moves as two
32-sector `CMD25`s, each copied into machine mode's buffer and burned
after.

**Expected:** each write's blocks read back as written, on both sides of
the split and at the ends; no reset.

**Run 1, LCD module plugged in.** Blocks 1-8 (text) were read into flash
page 3's RAM cache at 0x180000 (opened, never closed: no flash write) and
written to blocks 60-67 (0x3C) in one `write-block`; read back into
0x182000, both chunks matched. Blank blocks 70-77 written over 60-67 the
same way read back blank. The third 8-block write reset the chip; no
`V5F fault:` line, and the next boot's debug port said:

```
V5F crumbs: reset by software power, boot 1; the previous run left 919 crumbs
```

Power-down with the software flag, and no fault: the brown-out of
[ch32h417-notes.md](../ch32h417-notes.md) (The nanoCH32H417 board), not
an exception in machine mode, which would have printed its `mcause` and
`mepc` first. The LCD module's backlight runs from the same 3.3 V.

**Run 2, LCD module unplugged,** after a `wlink reset`, on the cli port
(each `block-status .` line until it printed 0 is shortened to `...`):

```
> hex 180000 open-flash
> 180000 1 8 read-block
...
> 180000 3C 8 write-block
...
> 182000 3C 8 read-block
...
> t{ 180000 @ 180200 @ 1803FF @ -> 182000 @ 182200 @ 1823FF @ }t
ok>
  (and the same for each of the 8 blocks: start, middle, last cell)
> t{ 181C00 @ 181E00 @ 181FFF @ -> 183C00 @ 183E00 @ 183FFF @ }t
ok>
> t{ 1 -> 2 }t
Error: ior=-106 assertion - wrong results
> hex
> 180000 44 1 write-block
...
> 182000 44 1 read-block
...
> t{ 180000 @ 180010 @ 180200 @ 1803FF @ -> 182000 @ 182010 @ 182200 @ 1823FF @ }t
ok>
```

(`t{ 1 -> 2 }t` shows that a mismatch does report.) Then the same
8-block write three more times, and a final check with the read-back
area cleared first, so a stale copy couldn't pass:

```
> 182000 46 8 read-block
...
> 183000 @ . 183C10 @ .
4:0:202020H 4:0:202020H ok>
> 182000 3C 8 read-block
...
> 183000 @ . 183C10 @ .
D:5:C2028H 6:4:2F3031H ok>
> t{ 180000 @ 180C00 @ 180FFF @ 181000 @ 181C10 @ 181FFF @ -> 182000 @ 182C00 @ 182FFF @ 183000 @ 183C10 @ 183FFF @ }t
ok>
```

Blocks 60-68 were then written blank again from 70-77 (8 blocks, then
1) and read back blank.

**Result:** pass, with the LCD module unplugged: five 8-block writes and
a single-block write through Guarded DMA, each read back right across
the 32-sector split, and no reset. With it plugged in, one 8-block write
in three browned the board out; that's the board's 3.3 V supply, not the
firmware.

### B18. PMP entry 0 over the V3F's flash, locked; Ctrl+X x3 as a chip reset (20 October 2026)

**Board and firmware:** the main board (UID `69a90e647f9cf6a9`), not
locked down (OBR `000003FC`, WPR `FFFFFFFF`), `SECURITY_LEVEL` 0, the
plain block file of B17 on the card. Built in MounRiver from branch
`pmp-entry0-probe`: `pmp-entry0` plus one test line at the top of
`lfRtcSet` (`V5F/User/lftime.c`), so `n rtc!` reads the word at `n` from
user mode and gives ok or -127. The probe branch is never merged.

```c
(void)*(volatile uint32_t *)(uintptr_t)seconds; return 0;   /* PROBE: delete this line */
```

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` (run 1, commit `20c1503`) | 62188 | `0bcd7c864403d1c7e4d0b2ca149a3c879656f75c9dcb4a246bbac70ed3665ea8` |
| `LiteForth_V3F.bin` (run 2, commit `0bff77e`) | 62296 | `ccf84da10641a70328c8b41dd9cbf3e9c3735c48b600844745f743d646b32693` |
| `LiteForth_V5F.bin` (both runs, with the probe) | 94016 | `4bd9ebcf77d026d2d0696ebe2cb0c8fbd29ed16b0165771ed4749a4f0aca50c6` |

**Why:** PMP entry 0 now covers everything below 0x08010000 as one locked
TOR entry: the code flash's whole low window, and the V3F's image (stage 0
and 1) in the high window, closed to the V5F's machine mode as well as its
user mode. The V5F's start-up code runs from the low window, so the V5F
must never run it again once the entry is locked: Ctrl+X x3, which
restarted the V5F alone (an HSEM interrupt jumping to `handle_reset`),
now resets the chip. With that, LiteForth clears `safe_boot` before USB is
configured again, so run 2 adds a mark for the reset Ctrl+X makes: Ctrl+X
x3 as the first input after it goes to recovery, healthy V5F or not.

**Expected:** the boot line shows `cfg0 9F191888` with entry 0 `02004000`,
locked; reads below 0x08010000 and of the records give -127, reads of the
V5F's image and LiteForth's pages work; Ctrl+X x3 resets the chip into a
safe boot; card, `enroll` and `1 load` (machine mode's flash service, with
entry 0 locked) work. Run 2: Ctrl+X x3 twice in a row reaches recovery,
and with a line typed in between it doesn't.

**Run 1** (V3F and V5F flashed with wlink), on the cli port:

```
> hex
> 8010000 rtc!
ok>
> hex
> 8030000 rtc!
ok>
> hex
> 0 rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
> hex
> 8000000 rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
> hex
> 800FFFC rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
> hex
> 7FFFC rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
> hex
> 8070000 rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
> 1 2 + .
3 ok>
```

Ctrl+X x3 (lfmcp's `forth_restart`): the port went, came back, and

```
Safe boot (Ctrl+X x3): the app is not running. `cold` starts it.
[幸运狐] v0.02
ok>
```

and the debug port:

```
Ctrl+X x3: resetting the chip; the V5F starts in safe mode
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: starting the V5F
V5F crumbs: reset by software, boot 8; the previous run left 63 crumbs
V3F: the V5F is running
V5F PMP: cfg0 9F191888, addr 02004000 0201CFFF 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 20 2026 19:58:22, GCC 12.2.0
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, booting from flash, safe boot (app not started)
V3F: USB configured
```

LiteForth (and so the clearing of `safe_boot`) comes before USB is
configured: a second Ctrl+X x3 couldn't reach recovery on a healthy V5F,
which four tries confirmed (each was another safe boot). Then:

```
> capacity .
121 ok>
> enroll
(security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>
> hex 1 block 10 dump
200030: 694C2028 6F466574 20687472 656D6F68  ( LiteForth home
...
> decimal
> 1 load
value test: 11 10
Dictionary loaded and committed to flash memory.
...
ok>
```

(A first `1 load` with `BASE` left in hex failed in screen 7, `se ?`: its
`\` uses `128`. Not a fault; the second, in decimal, is the run.)

**Run 2** (the V3F with the Ctrl+X mark flashed, the V5F as in run 1):
Ctrl+X x3, then `1 2 + .` typed, then Ctrl+X x3: two safe boots, no
recovery. Then, with nothing typed since that reset, Ctrl+X x3:

```
LiteForth stage 1: recovery mode. The V5F stays off:
Ctrl+X x3 twice: the V5F got stuck before LiteForth started
In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
adopt: take the V5F image in flash as it is (security levels 0 and 1 only).
reset: start again.
recovery ok> reset
[幸运狐] v0.02
ok>学如不及, 犹恐失之
> hex 8000000 rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
> hex 8030000 rtc!
ok>
```

(The recovery line's reason was then made neutral, "recovery on
request (or the V5F got stuck before LiteForth)", in `stage1.c`.)

**Then, without the probe:** both images built from `pmp-entry0`
(commit `fc22e10`) and flashed:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62316 | `9d9e5bd38bbf02458802517e9c0ca0e05e53062fb4675afbc887e642160c0ce3` |
| `LiteForth_V5F.bin` | 94056 | `955fe7d32c819105bd480e97c3cbafd15db75e4eebb8f926a7023cfd5a43f493` |

It booted (`1 2 + .` gives 3, capacity 121), and Ctrl+X x3 reset the chip
into a safe boot. (The V5F is 40 bytes bigger than the probe's: the
probe's early return let the compiler drop the rest of `lfRtcSet`.)

**A LOCKUP after each flash, and not from this change.** The boot after
flashing these images reported the run before as `reset by LOCKUP
software`. Checked:

- every wlink flash did it, of the V3F's sectors or of the V5F's alone,
  with the same image again;
- a plain `wlink reset` didn't (`reset by software`), and neither did the
  board running on its own (the trail before each ended cleanly, at
  `QUIT` or a terminal line);
- **`main`'s V5F (B16's image, `a649b1e1...`, the old entry 0 over just
  0x70000) flashed twice: the second boot also said `reset by LOCKUP
  software, boot 19`.**

So wlink's flash cycle (its reattach and the WCH-Link's flash loader,
closed firmware) does it to a running V5F whatever its PMP. Nothing in
the V5F's machine mode reaches the locked range: its handlers, its
vector table and WCH's default handler (`.text.vector_handler`) are all
in ITCM. The chip resets straight after, so nothing is lost; noted in
[ch32h417-notes.md](../ch32h417-notes.md).

**Result:** pass. Entry 0 is set and locked; neither the low window nor
stage 0 and 1 in the high window can be read from the V5F's user mode,
and machine mode's flash and record services still work with it locked;
Ctrl+X x3 resets the chip into a safe boot; Ctrl+X x3 twice in a row
reaches recovery on a healthy board, and only as the first input after
the reset.

### B19. An app package: the V5F image with LiteForth's flash pages (21 October 2026)

**Board and firmware:** the main board (UID `69a90e647f9cf6a9`), not
locked down (OBR `000003FC`, WPR `FFFFFFFF`), `SECURITY_LEVEL` 0, the
plain block file of B17 on the card, with `lfpages.bin` (262144 bytes)
added. Built in MounRiver from branch `app-packages` (commit `17ca2a0`)
with a new root key: the test keys of B10 and B11 were gone, so the
administrator (the developer, for this bench board) made a root key, a
release key (serial 1, product 0x4C46) and `sealed_root.h`
(`sealblocks.py root-key`, `release-key`, `root-header`), and both
projects were cleaned and rebuilt. The board's key comes from its UID at
level 0, so its `boards.csv` line didn't change.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62496 | `47d3d2974e1d5fa3b220e647478a03c1dc5def54c9bfba17e9a481b146cf7b90` |
| `LiteForth_V5F.bin` | 94364 | `2d004da332b659ef635259270f5adb6cecadfeaa14aafe58646dd5d8cb19f485` |

**Why:** with sealed block files on their way out, a Forth application
reaches a product in an update package: component 3, the V5F `.bin`
padded to its slot, then all of LiteForth's flash pages
([firmware-updates.md](firmware-updates.md) section 5). This checks the
developer's half (`save-pages`), the administrator's (`sealblocks.py
package --component app`), and stage 1 installing it, at level 0, where a
developer may still change the pages afterwards.

**Expected:** `save-pages` fills `lfpages.bin`; the package checks out on
the V5F as it's staged; stage 1 installs it and LiteForth's start
confirms it; a word that was in flash but not in the saved pages is gone
afterwards; a word saved after the install survives a reboot (level 0
doesn't check the pages).

**Run.** Before the root change, with the first build of the branch:
`1 load` (the new word moved the tools' API indices, so the dictionary in
flash had to be compiled again), then `save-pages`, then a marker word
saved into flash with the same steps `core.f` ends with:

```
> save-pages
ok>
> 0 open-flash
> : zzmark 4242 . ;
> _text here 32 bit  1 !
> save-wids save-idata
> _udata
> close-flash
> zzmark
4242 ok>
```

`zzmark` survived a `reboot`, and the rebuild for the new root (which
doesn't touch the flash pages). Then, on the administrator's PC, with the
new build's `LiteForth_V5F.bin` and `lfpages.bin` copied off the card:

```
> py sealblocks.py package LiteForth_V5F.bin app.lfu --component app --pages lfpages.bin --release release1.key --boards boards.csv --revision 10
app.lfu: app image, 393216 bytes in 96 block(s), version 10, 1 board(s), AES
```

and on the cli port, with kwai:

```
ok>~x app.lfu -2
...
ymodem: app.lfu: staged in lfstage.bin, 98 blocks, read back and checked
ymodem: package: V5F and app image, 393216 bytes, version 10: signature, every block and the image hash check out; waiting for stage 1 to install it
```

**The first attempt didn't install.** After `reboot` LiteForth came back
with `zzmark` still there, and stage 1 had said nothing. Both copies of
the security record read erased through wlink, so no record had been
made: stage 1 never saw the package flag (with a flag and no record, it
makes one before anything else, even to refuse a package). Not
explained; most likely the shared SRAM lost the flag in a power dip (the
board is powered through the WCH-LinkE, whose cable was unplugged and
plugged back around then). Nothing was installed, and the package stayed
in `lfstage.bin`.

**Second attempt:** staged again; the flag read `5A9E0F11`
(`CDC_UPDATE_MAGIC`) at 0x2017801C through wlink; `wlink reset`. The
debug port:

```
V3F update: version 10 installed, on trial
V3F: starting the V5F
...
V5F: LiteForth starting, booting from flash
V3F update: version 10 confirmed
V3F: USB configured
```

and the cli port:

```
> zzmark
zzmark ?
ok>
> 1 2 + .
3 ok>
```

Then a word saved after the install, as `zzmark` was (`: zzdev 777 . ;`),
and `reboot`: `zzdev` gave 777, and the debug port showed a normal start
with no rollback, so at level 0 the boot check left the changed pages
alone.

**Third attempt** (does `reboot` itself lose the flag?): staged again,
then `reboot`, without reading the flag first:

```
V3F update refused: lfbackup.bin needs 97 blocks
V3F: starting the V5F
```

So `reboot` delivers the flag. The refusal is by design: the installed
image is now the slot and the pages (393216 bytes), so its backup needs
1 + 96 blocks, more than this card's `lfbackup.bin` holds. Stage 1
refused before touching anything, and `zzdev` was still there.

**Result:** pass. `save-pages`, `sealblocks.py package --component app`
and stage 1 install an app package end to end; the pages come from the
package; level 0 leaves them editable. To note: after an app package
`lfbackup.bin` must hold the pages too (397312 bytes for today's layout),
and stage 1 reports a refused or lost package only on the debug port.

### B20. `revision`, and a debugger's V5F adopted at level 0 (21 October 2026)

**Board and firmware:** as in B19 (main board, level 0, B19's root key and
release key), built in MounRiver from branch `revision-word` (commit
`9cba9f0`) and flashed with wlink:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62772 | `350fa72fbb2994ecfc12a67c284256b2cf8fc130566dc3906ad72c03c4d3d78a` |
| `LiteForth_V5F.bin` | 94492 | `485f1383eef72c395d569d563c59dfbba135ccc71351cf1fab13a541b1450a54` |

**Why:** `revision ( -- u )` gives Forth the installed firmware's
version, which stage 1 publishes in the shared block at every start
(`revision`, `rev_magic` in `cdc_shared.h`). And at levels 0 and 1 a V5F
the debugger reprogrammed is now adopted, not rolled back: flashing an
earlier build of this branch over B19's app package had found the
opposite, the board quietly running the backup's version 0 with the
debugger's image gone (`revision ?`), since stage 1 checks the installed
image's MAC at every boot and had a backup to restore.

**Expected:** after the flash the debugger's V5F runs (adopted as
version 0) and `revision` gives 0; a V5F package with `--revision 11`
installs, and `revision` then gives 11.

**Run.** After the flash, a safe start (Ctrl+X x3), then on the cli port:

```
> decimal
> 1 load
...
> revision .
0 ok>
```

The V5F running was the debugger's (`revision` exists only in this
build), adopted as version 0. The debug port's adopt line for that boot
wasn't captured. Then, on the administrator's PC and in kwai:

```
> py sealblocks.py package LiteForth_V5F.bin v11.lfu --component v5f --release release1.key --boards boards.csv --revision 11
v11.lfu: v5f image, 94492 bytes in 24 block(s), version 11, 1 board(s), AES

ok>~x v11.lfu -2
...
ymodem: package: V5F image, 94492 bytes, version 11: signature, every block and the image hash check out; waiting for stage 1 to install it
```

then the board's reset button. The debug port:

```
V3F update: version 11 installed, on trial
V3F: starting the V5F
V5F crumbs: reset by pin, boot 13; the previous run left 2059 crumbs
...
V5F: LiteForth starting, booting from flash
V3F update: version 11 confirmed
V3F: USB configured
```

and kwai:

```
revision .
11 ok>
```

**Result:** pass. `revision` reports the installed version (0 for an
adopted debugger image, 11 after the package); a debugger-flashed V5F
runs at level 0 instead of being replaced; and the package flag survives
a reset by the pin, as it does `reboot`.

### B21. Sealed block files retired (21 October 2026)

**Board and firmware:** as in B20 (main board, level 0, B19's root key and
release key), built in MounRiver from branch `no-sealed-blocks` (commit
`d63e9a7`) and flashed with wlink (V3F at 0x08000000, V5F at 0x08010000):

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 62616 | `85a378cb9c5d036e26ba28c3d2148b2a5ca3fa6f49f93fbecba93c3d13ea3407` |
| `LiteForth_V5F.bin` | 93116 | `f49b487e8765ea29090d40e866ebff6500affba2e993496ccdb0f921796fc9be` |

**Why:** block files are plain at every level now; a product's Forth goes
in an app package. A block 0 that starts with `LITEFORTHS` (a sealed
block file) or `LITEFORTHF` (a package) gives no blocks, and `-1 ymodem`
refuses both. The V5F still checks packages for `-2 ymodem` with its
board key, through the trimmed `sealed.c`, which stage 1 shares.

**Expected:** the plain `lfblocks.fb4` mounts as before; a `-2 ymodem`
package checked by the new V5F installs; a sealed block file sent with
`-1 ymodem` is refused with nothing written.

**Run.** After the flash, on the cli port:

```
> revision .
0 ok>
> capacity .
121 ok>
> dir
System Volume Information/
lfblocks.fb4
sealed.fb4
lfstage.bin
lfbackup.bin
lfpages.bin
ok>
```

A first package, `v12.lfu`, was made by mistake from B20's V5F
(94492 bytes); it installed (`V3F update: version 12 installed, on
trial` ... `version 12 confirmed`, `revision` 12), but the old V5F did
the check. So today's V5F was flashed again with wlink (adopted as
version 0) and packaged from the build folder:

```
> py sealblocks.py package ...\V5F\obj\LiteForth_V5F.bin v13.lfu --component v5f --release ..\lfkeys\release1.key --boards boards.csv --revision 13
v13.lfu: v5f image, 93116 bytes in 23 block(s), version 13, 1 board(s), AES

ok>~x D:\Brad\adminLF\test\v13.lfu -2
...
ymodem: v13.lfu: staged in lfstage.bin, 25 blocks, read back and checked
ymodem: package: V5F image, 93116 bytes, version 13: signature, every block and the image hash check out; waiting for stage 1 to install it
ok>reboot
...
revision .
13 ok>
```

The debug port:

```
V3F update: version 13 installed, on trial
V3F: starting the V5F
V5F crumbs: reset by software, boot 18; the previous run left 1881 crumbs
V3F: the V5F is running
V5F PMP: cfg0 9F191888, addr 02004000 0201CFFF 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 21 2026 12:00:49, GCC 12.2.0
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, booting from flash
V3F update: version 13 confirmed
V3F: USB configured
```

Then the sealed block files. The old `sealed.fb4` (122 blocks) is larger
than the card's `lfblocks.fb4`, so the size check refused it first:

```
ok>~x ..\mailbox\sealed\sealed.fb4 -1
...
ymodem: sealed.fb4: it's bigger than the block file's space; nothing written
Error: ior=-92 Target block index exceeds allocated file capacity
ok>capacity .
121 ok>
```

So a 2-block file starting with `LITEFORTHS`, then zeros
(`sealed2.fb4`, sha256 `35d24f9cf71dc8c95e8dd9068c3092c566971e93afbad145f9703e67fa632ebb`):

```
~x D:\Brad\adminLF\test\sealed2.fb4 -1
ymodem: send the file(s) now (YMODEM)
[kwai: sending D:\Brad\adminLF\test\sealed2.fb4: 8192 bytes]
[kwai: 8/8]
[kwai: sent D:\Brad\adminLF\test\sealed2.fb4]
ymodem: sealed2.fb4: a sealed block file: no longer read (an app goes in an update package now); nothing written
Error: ior=-124 Bad file in a YMODEM transfer
ok>
```

**Result:** pass. The plain block file works as before, the new V5F
checks a package and stage 1 installs it, and a sealed block file is
refused before anything is written. Not tested on the board: a sealed
`lfblocks.fb4` already on the card at boot (it would give capacity 0);
`unit_tests/sealed` covers that on the desktop.

### B22. Stage 1's records under PMP entry 0; the board key in stage 1 (21 October 2026)

**Board and firmware:** as in B21 (main board, level 0, B19's root key and
release key), built in MounRiver from branch `records-under-entry0`
(commit `3987d8f`) and flashed with wlink, the V5F at its new slot first
(0x08014000), then the V3F (0x08000000):

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 63192 | `fb27c05e7a1ea938c20fa8c4440da095424461a6ca25f1fe3757ff5d927c442a` |
| `LiteForth_V5F.bin` | 92276 | `ea1fd86053235c7bd494fd3c41a08368c0b3c1f29c98ca8184bb092e630718e0` |

**Why:** stage 1's records move to 0x10000 and 0x12000 and the V5F's slot
to 0x14000, so PMP entry 0 (locked, to 0x08014000) covers the records with
stage 0 and 1, and entry 1 is free. Only stage 1 holds the board's key;
the V5F checks `-2 ymodem` packages without it, and Forth's `enroll` asks
stage 1 for the public key through the shared block.

**Expected:** the PMP line shows entry 0 to 0x08014000 and entry 1 off;
the board starts a fresh record (the old ones at 0x72000 are ignored) and
adopts the V5F; `enroll`, in Forth and in recovery, prints the board's
UID-key line; a V5F package checked by the keyless V5F installs.

**Run.** Between the two flashes the old V3F ran once, still reading its
old records, and woke the V5F at 0x10000, where its image was half
overwritten (`V5F crumbs: reset by LOCKUP software` at the next boot):
expected, since the V3F and V5F must change together. Then the new
build's start (debug port, from the last of several boots cut short by
the flashes):

```
V3F: starting the V5F
...
V5F PMP: cfg0 9F190088, addr 02005000 00000000 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, booting from flash
```

`v14.lfu` was packaged from this V5F build with this branch's
`sealblocks.py` (`--component v5f --revision 14`), sent with
`~x v14.lfu -2` (the V5F's keyless check passed it; kwai's lines weren't
kept), then `reboot`:

```
V3F update: version 14 installed, on trial
V3F: starting the V5F
V5F crumbs: reset by software, boot 20; the previous run left 1984 crumbs
V3F: the V5F is running
V5F PMP: cfg0 9F190088, addr 02005000 00000000 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 21 2026 13:10:43, GCC 12.2.0
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, booting from flash
V3F update: version 14 confirmed
V3F: USB configured
```

Forth's `enroll` (after a Ctrl+X safe start), then `revision`:

```
enroll
(security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>reboot
...
revision .
14 ok>
```

Recovery (Ctrl+X x3 twice):

```
LiteForth stage 1: recovery mode. The V5F stays off:
Ctrl+X x3 twice: recovery on request (or the V5F got stuck before LiteForth)
In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.
enroll: this board's line for boards.csv (at security level 2 it makes the board's key).
adopt: take the V5F image in flash as it is (security levels 0 and 1 only).
reset: start again.
recovery ok> enroll
enroll: security level below 2: the key comes from the UID, not the RNG
enroll: boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
recovery ok> reset
...
revision .
14 ok>
```

**Result:** pass. Entry 0 now covers stage 1's records and entry 1 is
free; stage 1 opened a package made for the board's `boards.csv` line
with the key only it holds, after the V5F's keyless check; `enroll`
prints the same line from Forth and from recovery. Level 2 (a key made by
`enroll` in recovery, then only installed images) was tested on the
simulated board (`stage1sim` `s_enroll`), not on this board.

### B23. PMP entry 1 fences the V3F's RAM (21 October 2026)

**Board and firmware:** as in B22 (main board, level 0), built in
MounRiver from branch `fence-probe` (commit `4130c50`): branch
`fence-v3f-sram` (commit `aff28d5`) plus one line, never to be merged,
that makes `rtc!` read the address it's given, from user mode. Flashed
with wlink, the V5F at 0x08014000, then the V3F at 0x08000000:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 63192 | `168b381dbaa5ba570736d6ff2f35ea38d2e845d374b1074ae09f441df00e2a38` |
| `LiteForth_V5F.bin` | 92252 | `0ea677fcf368065d28814ed53d2163e0cb5fbc34a7daecafe50bffa1266301d8` |

**Why:** `Link_v3f.ld` keeps all of the V3F's RAM (code, data, heap,
stack) in 0x20100000-0x2013FFFF, and the V5F's entry 1, a locked NAPOT
entry, gives neither of its modes any access there. Recovery's RAM
staging moved to 0x20140000, outside it; the shared block at 0x20178000
stays open.

**Expected:** the PMP line shows entry 1 as NAPOT 0x20100000/256K,
locked; the V3F runs as before (USB, `enroll` through the shared block);
reads of 0x20100000 and 0x2013FFFC fault (-127), reads of 0x20140000 and
0x20178000 don't.

**Run.** Debug port at the start:

```
V5F PMP: cfg0 9F199888, addr 02005000 08047FFF 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
```

On the cli port (through lfmcp):

```
> revision .
0 ok>
> enroll
(security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>
> hex
ok>
> 20178000 rtc!
ok>
> 20140000 rtc!
ok>
> 2013FFFC rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
> 20100000 rtc!
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
```

**Result:** pass. The V5F can't read the V3F's RAM, from either end of
the fenced 256K; the shared block and the space above the fence are
still reachable, and the V3F runs from its smaller RAM (the debugger's
V5F adopted as version 0, `enroll` answered). Not tested on the board:
recovery's RAM staging at its new address (`stage1sim`
`s_recovery_ram_and_ctrl_x` covers it), and the ITCM route documented in
firmware-updates.md section 8.

### B24. Machine mode's code runs from flash (21 October 2026)

**Board and firmware:** as in B23 (main board, level 0), built in
MounRiver (clean, since the V5F's compiler options changed) from branch
`mmode-in-flash` (commit `059f83a`) and flashed with wlink, the V5F at
0x08014000, then the V3F at 0x08000000:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 63192 | `e097a6843a763e82ab6fb112c021cf9e845673fea0eb4a0cdb36801e07f66709` |
| `LiteForth_V5F.bin` | 110632 | `9439242bc241eb32b34982993e090b401a64f9c6fa4e9dc0f2a5b8e0f96d0d66` |

**Why:** user mode can write ITCM, where the V5F's code runs, so its
machine-mode code moved to flash (`.mmode`, 0x0802E000, run in place):
the vector table and every handler. WCH's flash routines are copied into
ITCM's top 4 KB, with interrupts off, before each erase or write.
`mmodecheck.py` (run by `ch32build.sh`) found no call out of that code
and all 149 vectors inside it.

**Before this,** a test build (branch `xip-test`, not merged) answered
the questions: the V5F runs code from the high window (about 240 times
slower than from ITCM: a loop took 3.66 s from flash against 15 ms from
ITCM); traps and interrupts through a vector table in flash work (an
`ecall` round trip about 12 us); and WCH's `FLASH_ROM_ERASE`/`WRITE`
run from flash locked the chip up while programming 0x08070000 (the
WCH-LinkE couldn't attach either; the board needed a power cycle), but
copied into ITCM with interrupts off they erased, programmed and checked
0x08070000 and 0x08080000 (1.2 ms each), and `1 load` wrote all four
LiteForth pages. (The pages were blank before that test by accident: a
MounRiver download from the V3F project erases the whole chip.)

**Expected:** LiteForth starts and boots its dictionary; `enroll` and
block reads work; flash pages are written (`close-flash`, `1 load`); a
store to DMA1 from Forth faults to -127 through the handler in flash,
and a store to RCC is emulated by it.

**Run** (the cli port, through lfmcp):

```
[幸运狐] v0.02
ok>学如不及, 犹恐失之
> revision .
0 ok>
> enroll
(security level below 2: the key comes from the UID, not the RNG) boards.csv line:
69a90e647f9cf6a9,8bbde9b22c25fec0601398df2e45a8837e50ac374208ea9279b5a316a9c0d403
ok>
> 1 block drop
ok>
> 1 load
value test: 11 10
Dictionary loaded and committed to flash memory.
...
```

Then a word added to the pages and `save-wids save-idata close-flash`
(not a valid way to extend the boot image: the next boot's app failed,
-103, which showed the pages had been rewritten), `1 load` again, and
`reboot`:

```
[幸运狐] v0.02
ok>学如不及, 犹恐失之
> 2 3 + .
5 ok>
> hex 0 288000 !
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
> hex 288400 @ .
3336983H ok>
> 288400 @ 288400 !
ok>
> 288400 @ .
3336983H ok>
```

(A line after an error is read in decimal: QUIT resets the base, so
`288400` alone then gave -9.)

**Result:** pass. The board runs with every trap and interrupt handled
by code in flash, writes its flash pages through the routines copied in
for each write, and boots from them. Not tested: whether user mode can
reach the PFIC (its VTF registers), and machine mode's data in DTCM,
which user mode can still write (firmware-updates.md section 8).

### B25. Level 2: the boot check needs the release key's signature (22 October 2026)

**Board and firmware:** the spare board, UID `e52116647f9c7a22`. Its read
protection was removed (which erased it), then it was flashed with a
`SECURITY_LEVEL` 2 build of branch `signed-boot` (PR #213) and locked
down. The V3F was built from commit `4bea05a`, the V5F from `a765153`:
that commit changed only the V3F and the tests.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 64864 | `3704b37a12418cca2e0ec5704589f5e8fa20251f0904c521b46da5716d8cd941` |
| `LiteForth_V5F.bin` | 109496 | `39b50198b5d22402efb16b72fd17a1cefd92f1198b9d04adaa156f9c272a1bc8` |
| `lfpages.bin` (pages from the main board's `save-pages`) | 262144 | `3a82a66f5ef091ca22d4a0854d1b3be04075f8a75eca50176c5b78855bc68bd3` |
| `spare-app.lfu` (app package, version 1, for this board's key) | 385024 | `5854d7ebfcbdf8b5c3ecd86fa864389e852ab85a3c549e60c0aab4788e878ac0` |

**Why:** at level 2, stage 1 now keeps the installed package's block 0
(at 0x70000). At every boot it checks the root key's certificate, the
release key's signature and the version minimums, then hashes the V5F
slot and the flash pages against the image hash in that block 0. So a
changed image doesn't start, even if the device token, the MAC key, had
been read out.

**A finding along the way:** the first lock-down of this board used a
build from `a765153`. After the lock, that build started the V5F with
no security record:

```
V3F: lockdown: option bytes now RDPR 00FF USER C03F WRPR0 FF00: ok
V3F: starting the V5F
V5F: LiteForth starting, flash is blank
```

That was stage 1's "never updated: boot as before". At level 2 it
bypasses the signed check, since erasing the two record sectors would
start whatever is in the slot. Commit `4bea05a` sends a level 2 board
with no record, an unreadable record, or no key to recovery instead.
`lockdown.py` takes that recovery, with no lock-down warning, as a
locked start.

**Flashing:** `lockdown.py run` twice failed to attach the WCH-LinkE
(protocol error 0x55) once the level 2 stage 1 was running. The first
time the V3F was flashed first; the second time the V5F was. So the
images went in by MounRiver's download, and then `lockdown.py lock`
locked the board without the probe.

**Expected:** after the lock, recovery with no image installed. `enroll`
makes the key, and the app package for it installs from recovery. The
image starts, and starts again after every reset, so the signed check
passes each time.

**Run.** `lockdown.py lock` (the debug port, after `LOCK`):

```
V3F: lockdown: option bytes now RDPR 00FF USER C03F WRPR0 FF00: ok
V3F lockdown: restarting (if the lock-down warning comes again, power it off and on)
V3F: recovery mode: the V5F stays off; waiting for a package over USB (no V5F image installed: type enroll, then send an app package)
V3F: built Oct 22 2026 03:45:11, GCC 12.2.0
Stage 1 passed its check of OBR and WPR (no lock-down warning) and stays in recovery: no V5F image is installed yet, and level 2 starts only one it installed.
```

`wlink status` then showed `OBR 03FFFCFE, WPR FFFFFF00: read protection
on, USB download off, USART download off, write protection as stage 1
wants`. After a power cycle, `enroll` (cli port, through lfmcp):

```
recovery ok> enroll
enroll: new key made (a new security record and device token)
enroll: boards.csv line:
e52116647f9c7a22,044974c7a23fe018fdb9f2182e9d71344c290ade351700d2057d83625b8a9104
```

The package, made and checked on the PC:

```
spare-app.lfu: app image, 376832 bytes in 92 block(s), version 1, 1 board(s), AES
spare-app.lfu: ok. Product 0x4c46, revision 1, release key 1, AES, 92 content blocks, boards: e52116647f9c7a22
```

Installed from recovery (kwai):

```
recovery ok> ~x D:\Brad\adminLF\test\spare-app.lfu -2
-2 ymodem
[kwai: sent D:\Brad\adminLF\test\spare-app.lfu]
ymodem: spare-app.lfu: 385024 bytes received
update: version 1 installed, on trial
recovery: installed; restarting to try it
[kwai: COM6 is back]
ok>revision .
1 ok>
```

Then `reboot` three times, with `revision .` giving `1` each time. The
debug port showed the same start after each reboot:

```
V3F: starting the V5F
V5F crumbs: reset by software, boot 3; the previous run left 90 crumbs
V3F: the V5F is running
V5F PMP: cfg0 9F199888, addr 02005000 08047FFF 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 22 2026 03:45:11, GCC 12.2.0
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, booting from flash
V3F: USB configured
```

**Result:** pass. On a locked level 2 board, stage 1 started the V5F
only once an app package signed by the release key had been installed.
After that it checked the signature and the image hash over the slot
and pages at every boot, and passed each time. Not tested on the board:
- A changed image or page. The debugger can't write a locked board, so
  `unit_tests/update` and `stage1sim` cover it, including a record
  forged with a valid MAC.
- The refusal of a V5F-only package.
- The time the check adds, which wasn't measured.

### B26. The V5F's memory pool in general-purpose SRAM (22 October 2026)

**Board and firmware:** the main board, UID `69a90e647f9cf6a9`, at
`SECURITY_LEVEL` 1. It was built in MounRiver from branch `pool-sram`
(PR #215) and flashed by the user:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 64000 | `ede6bcea52960d06eb1833e1b4afb982cfcc57063d43fd763f41e0c651d3710c` |
| `LiteForth_V5F.bin` | 110708 | `3d62a082b12a8fb753d9de7ee2e1f9bf1d6115a0945baa6f2da17d119ab25eb0` |

**Why:** the pool (`memalloc.c`) moved from DTCM to general-purpose SRAM
at 0x20140000 (224 KB), to make room for the data logs' slots
([data-logs.md](data-logs.md)). Guarded DMA and the flash service now
take buffers from it. Some static buffers were also replaced by buffers
taken from the pool only while they're needed: `ymodem`'s, `sealed.c`'s
while a package is mounted, and the V5F `blocks.c`'s block 0 check and
package check. The VM's RAM page is the pool's first allocation, so it
moved too.

**Expected:** LiteForth starts and finds its block file. `1 load` works,
which opens a flash page in the pool and commits it. `-2 ymodem` stages and
checks a package. `ymodem` receives a file. The VM is a little slower with
its RAM page outside DTCM.

**Speed:** this word, a million fetches and stores to a variable in the
RAM page, timed with the microsecond counter:

```
: bench  capusec x@  1000000 for v @ 1+ v ! next  capusec x@ swap - . ;
```

| Build | `bench` |
|---|---|
| Before (pool in DTCM) | 801490 us (804004 the first time) |
| After (pool in general SRAM) | 814049 us |

So the VM is 1.6% slower on a loop that's all RAM accesses.

**Run** (kwai):

```
1 load
value test: 11 10
Dictionary loaded and committed to flash memory.
...
ok>bench
814049 ok>
~x D:\Brad\adminLF\test\spare-app.lfu -2
ymodem: send the file(s) now (YMODEM)
[kwai: sent D:\Brad\adminLF\test\spare-app.lfu]
ymodem: spare-app.lfu: staged in lfstage.bin, 94 blocks, read back and checked
ymodem: package refused: sealed file refused: this board isn't one of its recipients; stage 1 won't install it
Error: ior=-124 Bad file in a YMODEM transfer
ok>~x lfblocks.fb4 0
ymodem: send the file(s) now (YMODEM)
[kwai: sent lfblocks.fb4]
ymodem: lfblocks.fb4: 0 blocks written
note: 528 non-blank line(s) outside blocks ignored
ok>reboot
```

`spare-app.lfu` is the spare board's package from B25, so this board
refusing it is the right answer: the check read every block's hash through
pool buffers before saying so. The second transfer sent a block file as
source, which has no `( BLOCK n )` lines, so nothing was written. The
transfer itself went through `ymodem`'s pool buffers. The debug port,
from flashing to after `reboot`:

```
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, flash is blank
...
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, booting from flash
```

**Result:** pass. Starting up (the block 0 check), `1 load`, the
package check and `ymodem` all work with their buffers in the pool, and
the VM is 1.6% slower. Not run this time: `-1 ymodem`, `save-pages` and
`ecdc-test`.

### B27. ECDC, the RTC and standby, a CPU fault in the app, Ctrl+X in a transfer (22 October 2026)

**Board and firmware:** the main board, UID `69a90e647f9cf6a9`, as the
user had last built and flashed it in MounRiver: the V3F reports `built
Oct 22 2026 11:06:37, GCC 12.2.0`, and the V5F's PMP line is as in B26.
The hashes weren't recorded and the boot log didn't give the security
level. The level is taken as 0: a store to DMA1 reached the CPU, which a
level 1 build's I/O allowlist would have refused first (-9), and the user
recalls building it at 0. Since then, stage 1 prints the level and the
installed version at every boot. `revision` 0: an image the debugger
flashed, not a package. The steps ran on the cli port through lfmcp,
except step 4, which ran in kwai.

**Why:** to close gaps in the traceability matrix (`rtm.py --gaps`):
MOD-ECDC, SYS-TIME-01/02 and UN-06, UN-03 and SYS-LANG-09, and SYS-TERM-04.

**1. The ECDC engine.** Expected: every unit matches software, except the
deliberate case without the pause after key expansion.

```
> ecdc-test
AES, out of place: 256/256 units match software; 174 us, software 2446 us
AES, in place: 256/256 units match software; 172 us, software 2445 us
AES, no pause after key expansion: 253/256 units match software; 164 us, software 2444 us
    wrong (unit.word^difference): 0.3^ca3e2641 1.3^ca3e2641 2.3^ca3e2641
SM4, out of place: 256/256 units match software; 176 us, software 770 us
AES through ctr_crypt_block (first use: checks the engine): 256/256 units match software; 484 us, software 2438 us
AES through ctr_crypt_block again: 256/256 units match software; 172 us, software 2444 us
AES through ctr_crypt_block, a buffer not 32-byte aligned: 256/256 units match software; 261 us, software 2439 us
SM4 through ctr_crypt_block (first use): 256/256 units match software; 378 us, software 771 us
SM4 through ctr_crypt_block again: 256/256 units match software; 176 us, software 769 us
SM4 through ctr_crypt_block, a buffer not 32-byte aligned: 256/256 units match software; 260 us, software 769 us
ctr_crypt_block uses the engine for AES: yes, SM4: yes
The block buffers are 32-byte aligned (no copy): yes
ok>
```

**2. The RTC and standby.** Expected: `clock!` sets the clock, and the
calendar words read it back. `standby` sleeps until the alarm, then the
chip restarts with `alarm?` true and the clock still counting.

```
> alarm? .
0 ok>
> rtc@ dup . .date space rtc@ .time
29709 1970-01-01 08:15:09ok>
> 0 20 2 23 10 2026 clock!
ok>
> rtc@ dup . dup .date space .time
1792722000 2026-10-23 02:20:00ok>
> rtc@ 5 + dup alarm! .time
02:20:05ok>
> standby
```

The port went away, then LiteForth's banner came back:

```
[幸运狐] v0.02
ok>学如不及, 犹恐失之
> alarm? .
-1 ok>
> rtc@ dup . dup .date space .time
1792722018 2026-10-23 02:20:18ok>
```

A second run started from the boot the alarm had woken:

```
> rtc@ 15 + dup alarm! .time
02:20:38ok>
> standby
```

The PC took about a minute to show the port again. To check that the
chip hadn't slept past the alarm, the boot time was worked out from the
microsecond counter:

```
> alarm? .
-1 ok>
> rtc@ .time
02:21:43ok>
> rtc@ capusec x@ 1000000 u/mod nip - .time
02:20:39ok>
```

So this boot started at 02:20:39, a second after the 02:20:38 alarm. The
debug port, from the second `standby`:

```
V3F: standby
V3F security: not locked down: read protection off, the V3F's sectors not all write-protected, boot ROM USB download on, boot ROM USART download on (OBR 000003FC, WPR FFFFFFFF)
V3F: starting the V5F
V5F crumbs: reset by software, boot 3; the previous run left 84 crumbs
V3F: the V5F is running
V5F PMP: cfg0 9F199888, addr 02005000 08047FFF 100087FF 1FFFFFFF: as set, locked (clearing entry 0 was ignored)
V3F: built Oct 22 2026 11:06:37, GCC 12.2.0
V5F blocks: lfblocks.fb4, 121 blocks at sector 12864 (exFAT)
V5F: LiteForth starting, booting from flash
V3F: USB configured
```

**3. A CPU fault in the running app.** The demo app runs at boot. A task
in its ring stores to DMA1, which PMP entry 2 makes read-only in user
mode. Expected: the app's yeet handler (core.f) reports the fault and
parks the app, and the terminal still works.

```
> counter @ . counter @ .
1573341068 1573341068 ok>
> 0 open-flash
ok>
> : boom ( -- ) t1 activate  0 2654208 ! ;
ok>
> close-flash
ok>
> boom
ok>
App error -127 at 901H
> counter @ .
1573406135 ok>
> counter @ .
1573406135 ok>
```

The debug port:

```
V5F fault: mcause 00000007 mepc 200AAABA mtval 40020000 ra 200AA562 sp 200FFD80 -> Forth
```

`counter` stopped, so the app was parked. Then `1 load` rebuilt the
dictionary without `boom`, and after a `reboot` `counter` moved again
(1576029766, then 1576853265).

**4. Ctrl+X inside a YMODEM transfer.** The file `ctrlx.lfu` is 8192
bytes: `LITEFORTHF`, then 8182 bytes of 0x18. Its sha256 is
`318ff85fc39643850516fbc841c6638ca43858d9cc60dd6713fa4937cf35af69`.
Expected: the whole file arrives, the board doesn't reset (three Ctrl+X
in a row would reset it outside a transfer), and the package is refused.
In kwai:

```
ok>~x ctrlx.lfu -2
ymodem: send the file(s) now (YMODEM)

[kwai: sending ctrlx.lfu: 8192 bytes]
[kwai: 8/8]
[kwai: sent ctrlx.lfu]
ymodem: ctrlx.lfu: staged in lfstage.bin, 2 blocks, read back and checked
ymodem: package refused: sealed file refused: unknown format version; stage 1 won't install it
Error: ior=-124 Bad file in a YMODEM transfer
ok>1 2 + .
3 ok>revision .
0 ok>
```

**Result:** pass.
- The ECDC engine matches software for AES and SM4: in place, out of
  place, and through `ctr_crypt_block`, aligned or not.
- The RTC keeps time through standby and a reset, and the alarm wakes the
  chip on time, twice in a row.
- A CPU access fault in the app goes to the app's own handler.
- Ctrl+X in a transfer's data doesn't reset the board.

Not tested:
- A misaligned access (-23) and an illegal instruction (-128): Forth's VM
  makes only whole-cell accesses and runs no machine code, so neither can
  be made to happen from Forth.
- Standby's current draw.

### B28. Level 2: the flash pages can't be opened or saved (22 October 2026)

**Board and firmware:** the spare board, UID `e52116647f9c7a22`, as in
B25: locked down, level 2, `spare-app.lfu` version 1 installed. The V3F
reports `built Oct 22 2026 03:45:11, GCC 12.2.0`, the B25 build. The
steps ran on the cli port through lfmcp.

**Why:** SYS-STO-07: at level 2 only update packages write LiteForth's
flash pages.

**Expected:** `open-flash` and `save-pages` refuse with -20.

```
> revision .
1 ok>
> enroll
boards.csv line:
e52116647f9c7a22,044974c7a23fe018fdb9f2182e9d71344c290ade351700d2057d83625b8a9104
ok>
> 0 open-flash
Error: ior=-20 write address prohibited
ok>
> save-pages
Error: ior=-20 write address prohibited
ok>
> close-flash
ok>
> reboot
```

After the reboot, the signed image started again:

```
[幸运狐] v0.02
ok>学如不及, 犹恐失之
> revision .
1 ok>
> 1 2 + .
3 ok>
```

**Result:** pass for `open-flash` and `save-pages`.

**A finding:** `close-flash` answered `ok`. With no page open it does
nothing, and machine mode's flash service refuses every page write at
level 2 anyway. Still, it shouldn't say `ok`. Commit `be529e1` makes
`close-flash` and `save-pages` refuse whenever the pages are locked, and
`unit_tests/locked` tests that on the desktop (`lf -L 2`). The fix hasn't
been on a board yet.

### B29. The fault handler on machine mode's own stack (28 October 2026)

**Board and firmware:** the main board, UID `69a90e647f9cf6a9`, at
`SECURITY_LEVEL` 0, built in MounRiver from branch `rot-plan` (commit
`bec7026`: the fault handler on the machine-mode stack, SYS-SEC-11 part 1,
`doc/admin/firmware-updates.md` section 8) and flashed by the user:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 64144 | `ad8d3d121472c505f7801a32e15604070c6405a37412bff9ba3222d5039f3c47` |
| `LiteForth_V5F.bin` | 110716 | `d0ad837109132c229d44a16d06f03781ba779795af8101994a6dcc9d73ae3287` |

(The V3F is unchanged from `main` on this branch; it reports `built Oct 22
2026 11:06:37`.)

**Why:** `HardFault_Handler` used to save the faulting registers at the
faulting `sp`, in machine mode. User mode sets its own `sp`, so it could
point it at a peripheral and fault, and have the handler store there past
PMP entry 2. The handler now swaps `sp` with `mscratch` (a 1 KB
machine-mode stack in DTCM, set by the start-up code before its `mret`)
before it stores anything. This is a regression test on the board: the
rewrite must keep the chip booting, keep the machine-mode `ecall` services
working, and keep returning Forth faults to Forth.

**Expected:** the board boots and runs; `1 load` writes the flash pages
through the `ecall` service; a store to an address that isn't there faults
to Forth (`-127`, `-> Forth` on the debug port) and the terminal carries
on; a second fault behaves the same (the handler restored `mscratch`); and
`reboot` starts cleanly.

**Run.** Cli port (COM17, through lfmcp):

```
> revision .
0 ok>
> 1 load
value test: 11 10
Dictionary loaded and committed to flash memory.
...
ok>
> hex 0 288000 !
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
> decimal 2 3 + .
5 ok>
> .s
ok>
> hex 0 288000 !
Error: ior=-127 CPU access fault: no memory or peripheral there
ok>
> decimal 7 8 + .
15 ok>
> reboot
[the board restarts]
> revision .
0 ok>
```

Debug port (COM14), the two faults:

```
V5F fault: mcause 00000007 mepc 200AAABA mtval 40020000 ra 200AA562 sp 200FFD20 -> Forth

V5F fault: mcause 00000007 mepc 200AAABA mtval 40020000 ra 200AA562 sp 200FFD20 -> Forth
```

The reported `sp` (0x200FFD20) is the faulting code's stack, the user C
stack near the top of DTCM, not the machine-mode stack (0x200FBC00-
0x200FC000): the handler switched to its own stack internally and recorded
the `sp` the fault had, for the report and the store emulation.

**Result:** pass. The board boots with the start-up `mscratch` set-up and
the `.mstack` carve, `1 load` commits to flash through the `ecall`
service, a store fault returns to Forth and the terminal keeps its stacks,
a second fault behaves identically (so `mscratch` is swapped back and the
one machine-mode stack is reusable), and `reboot` starts cleanly. Not
tested: a store aimed by `sp` into a peripheral, which needs native code,
not Forth; the fast-interrupt handlers still save at user mode's `sp`
(SYS-SEC-11 is finished with part 3).

### B30. The V5F's time base on TIM6, and standby (28 October 2026)

**Board and firmware:** the main board, UID `69a90e647f9cf6a9`, at
`SECURITY_LEVEL` 0, built in MounRiver from branch `basic-timers`
(commit `7fdbc44`: the time base on TIM6, SYS-SEC-15's prerequisite,
`doc/admin/firmware-updates.md` section 8 part 3; and commit `fbf9e08`:
the prescaler fix below) and flashed by the user:

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V3F.bin` | 64160 | `404af7a3c96d8f5dfd4cbb9804300b85a555527a71cb1c87c93d19527c62ed7c` |
| `LiteForth_V5F.bin` | 110712 | `1be6635a295f97a6943c494ab46f899d71058b9923d12017b16c7845fffde27f` |

(The V3F carries the `basic-timers` TIM2->TIM7 change but is otherwise
as on `main`.)

**Why:** the V5F's microsecond time base moved from SysTick1 to TIM6, a
basic timer user mode can read, so the PMP plan can later fence off the
0xE000xxxx region (PFIC and SysTick). Two things to confirm on the
board: that the microsecond rate is right, cross-checked against the
RTC (an independent 32.768 kHz crystal), and that `standby` (Stop mode,
woken by the RTC alarm, which resets the chip) still works with TIM6 as
the base. `capusec` captures the counter; `x@` reads its low 32 bits.

**A rate bug, found and fixed.** The first flash ran the microsecond
counter at a quarter rate. Over ~24 s, `capusec x@` advanced ~3.93 s
then ~6.0 s against 16 s and 24 s of RTC: about 250 kHz, not 1 MHz.
`lfTimeInit` set the prescaler from `SystemCoreClock`, but on the V5F
that is the core clock (SYSCLK >> HPRE), while TIM6 counts HCLK (that
>> FPRE, FPRE = /4 here); `system_ch32h417.c` makes `SystemCoreClock`
the core clock on the V5F and HCLK on the V3F, which is why the V3F's
TIM7/TIM2 prescaler (same formula) is right and this one wasn't. Fixed
to take the prescaler from `RCC_GetClocksFreq`'s `HCLK_Frequency`
(commit `fbf9e08`, `doc/ch32h417-notes.md`), and reflashed.

**Expected:** after the fix, the microsecond counter tracks the RTC at
~1 MHz (one RTC second per 1,000,000 counts); `standby` with an alarm a
few seconds ahead sleeps, the alarm wakes and resets the chip, it
reboots, `alarm?` reports the alarm wake, and `rtc@` has advanced by the
sleep (the RTC counts through Stop mode and the reset); TIM6 is set up
again on the reboot and keeps the right rate.

**Run.** Cli port (COM17, through lfmcp). Rate, after the fix: two
captures of `rtc@` and `capusec x@`, ~31 s of wall clock apart:

```
> rtc@ . capusec x@ .
85 85345019 ok>
> rtc@ . capusec x@ .
116 116610936 ok>
```

d(rtc) = 31 s, d(us) = 31,265,917 us = 31.27 s: 1,008,578 us per RTC
second, 1 MHz within the RTC's +/-1 s integer quantization. (The first
flash, before the fix, gave ~250,000 us per RTC second.)

Standby. The alarm is an absolute RTC second and must be at least 2 s
ahead when `standby` runs, so it is set and `standby` called on one
line (set too early, the round-trip latency ate the margin and `standby`
returned `-24`):

```
> rtc@ .
477 ok>
> rtc@ 20 + alarm!
ok>
> standby
[no ok>: the board drained its output, slept in Stop mode, the RTC
 alarm woke it ~20 s later and reset the chip, and it rebooted]
[幸运狐] v0.02
ok>
> alarm? .
-1 ok>
> rtc@ .
530 ok>
```

`alarm?` is true (`-1`): the reboot came from the RTC alarm. `rtc@` went
477 -> 530 across the sleep and reset, so the RTC counted through Stop
mode and the backup domain survived. TIM6 was set up again on the
reboot and keeps the rate:

```
> capusec x@ .
56930873 ok>
> rtc@ .
555 ok>
```

56.93 s on TIM6 against 57 s of RTC since the reboot (the alarm reset
was at rtc ~498).

**Result:** pass. The microsecond base runs at 1 MHz against the RTC
after the prescaler fix, `standby` sleeps and wakes on the alarm with a
clean reboot, the RTC keeps time through Stop mode and the reset, and
TIM6 re-inits at the right rate on the reboot. Not tested here: the
full SYS-SEC-15 PMP shrink and the interrupt-enable move, which this
time base is only the prerequisite for.

**Follow-up (TX timeout, 28 October 2026).** The V3F's CDC tx timeout,
moved from TIM2 to TIM7 on this build, was exercised from the host with
`scripts/admin/cdc_txtimeout.py` on COM17. A large `dump` read straight
through delivers the whole output (303,107 bytes in 1.25 s); the same
dump with the port left open but unread for 2 s comes back 95.9% shorter
(12,356 bytes), and the board still answers afterward. A dead timeout
would instead hold the ring and the V5F would block, so a resumed read
would deliver the dump intact; the 95.9% loss is the bridge discarding
the unread ring on TIM7's 100 ms timeout, so the V5F never waits forever.
Pass.

### B31. PMP entry 3 fences the PFIC and SysTick from user mode (29 October 2026)

**Board and firmware:** the main board, UID `69a90e647f9cf6a9`, at
`SECURITY_LEVEL` 0, built in MounRiver from branch `sys-sec-15-pfic` (the
restructure, then commit `adf7de3`: PMP entry 3 shrunk to 2 GB) and
flashed by the user. Only the V5F changed; the V3F is as on `main`.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V5F.bin` | 111016 | `8d5f8d90cda0dcd090f4b52a65a02598a040cd993e517cadfb1ca7c5f0a34b7e` |

**Why:** part 3 of the PMP plan (SYS-SEC-15,
`doc/admin/firmware-updates.md` section 8): the PFIC, SysTick and the
system control register (0xE0000000 up) become machine mode's alone, so
user mode -- even a signed but malicious V5F image -- can't steer the
interrupt controller or the system timer. PMP entry 3 shrinks from all
memory (NAPOT `0x1FFFFFFF`) to the 2 GB from 0 (`0x0FFFFFFF`); from
0x80000000 up no entry matches, which the hardware allows for machine
mode and denies for user mode. This needs everything that touches that
region moved into machine mode first: the time base (TIM6, B30), the
interrupt enables (to `v5f_irq_setup`, run at start-up), and `reboot`
and `standby` (to ecall services, `standby`'s Stop-and-reset tail in
`.mmode`); `Delay_Init` (SysTick) is dropped on the V5F.

Tested in two rounds: round 1 with entry 3 still open, to confirm the
restructure didn't break anything; round 2 with the fence in place.

**Expected:** both rounds boot and run -- the microsecond base tracks
the RTC, `dir` reads the card, `reboot` resets, `standby` sleeps and
wakes on the RTC alarm. In round 2 `pmp_report` shows entry 3 as
`0FFFFFFF`, set and locked, and nothing user mode does faults.

**Run (round 1, restructure, entry 3 open).** Cli port (COM17):

```
> revision .
0 ok>
> decimal rtc@ . capusec x@ .
41 41609408 ok>
> rtc@ . capusec x@ .            ( ~13 s later )
54 55014789 ok>                  ( d-us/d-rtc ~= 1.03 MHz )
> dir
System Volume Information/
lfblocks.fb4
...
ok>
> reboot
[the board resets and reboots]
> rtc@ 20 + alarm!  standby
[sleeps; the RTC alarm wakes it ~20 s later and resets; reboots]
> alarm? .
-1 ok>
```

**Run (round 2, the fence).** Debug port (COM14), at boot:

```
V5F PMP: cfg0 9F199888, addr 02005000 08047FFF 100087FF 0FFFFFFF: as set, locked (clearing entry 0 was ignored)
```

Entry 3's `pmpaddr` reads back `0FFFFFFF` (2 GB from 0) and the config is
locked (the test write that tried to clear entry 0 was ignored), so the
fence is in force in hardware. Cli port (COM17), behind the fence:

```
> revision .
0 ok>
> decimal rtc@ . capusec x@ .
28 29011680 ok>
> rtc@ . capusec x@ .            ( ~13 s later: ~1 MHz )
41 41797047 ok>
> dir
System Volume Information/
lfblocks.fb4
...
ok>
> reboot
[resets and reboots]
> rtc@ 20 + alarm!  standby
[sleeps; alarm wakes and resets ~20 s later; reboots]
> alarm? .
-1 ok>
> rtc@ .                         ( 63 before standby )
111 ok>                          ( RTC counted through Stop and the reset )
```

**Result:** pass. With the PFIC and SysTick fenced from user mode, the
microsecond base still runs at 1 MHz (TIM6's interrupt is delivered to
the machine-mode handler), `dir` reads the card (SDIO polled, its
interrupt left off at start-up), `reboot` resets through the ecall
service (the SCB is machine mode's now), and `standby` sleeps, wakes on
the RTC alarm and resets through `lf_standby_service` in machine mode
(the SLEEPDEEP bit and `NVIC->CSTAR` are both in the fenced region). No
user-mode access faulted. `pmp_report` confirms entry 3 is `0FFFFFFF`,
set and locked. Not tested: a deliberate user-mode poke at the PFIC to
force the fault (a writability probe, left out by project rule); the
locked `pmpaddr3` readback is the hardware proof instead. Part 4 (the
machine-mode root signature, SYS-SEC-14) is in B32.

### B33. No bus master runs for user mode: the clock fence and the ECDC service (31 October 2026)

**Board and firmware:** the main board, UID `69a90e647f9cf6a9`, at
`SECURITY_LEVEL` 0, built in MounRiver from branch `sys-sec-13-busmasters`
(commit `92ef0db`: the start-up clock fence and the machine-mode ECDC
service, on top of the store guard in `c4cbaa1`) and flashed by the user.
Only the V5F changed; the V3F is as on `main`.

| Image | Size | sha256 |
|---|---|---|
| `LiteForth_V5F.bin` | 112372 | `fa8f503229d3ae12a03512d5b5d335f8586c8e9af0b71ed000fa75ac0e0ae60b` |

**Why:** part 2 of the PMP plan (SYS-SEC-13, `doc/admin/firmware-updates.md`
section 8): a bus master isn't bound by the PMP, so user mode must not run
one. A peripheral without its clock neither takes register writes nor runs,
so the clock is the fence for a master outside PMP entry 2 (the ECDC at
0x40016C00, whose registers user mode can otherwise write). The store guard
refuses any user-mode RCC write that would turn a master's clock on
(`guard_rcc_turns_on_master`, 279 checks in `unit_tests/guard`); the start-up
code turns them all off (`clock_fence`); and the machine-mode services turn
on only the master they use, for the one transfer, and off again -- Guarded
DMA in `burn`, and a new ECDC service (`PMP_SVC_ECDC`, `ecdc_serve`) that
`ctr_hw_crypt` calls, driving the engine on its own DTCM buffer and copying
to and from a checked user buffer.

**Expected:** `ecdc-test` encrypts a 4 KB block with AES and SM4 and through
`ctr_crypt_block` (the path packages take), all matching software, with the
engine in use -- so crypto works even though user mode can no longer clock
the engine, because the service runs it in machine mode. The card still
reads (Guarded DMA, now dropping the DMA clock after each transfer). At
idle every fenced master's clock reads off in RCC, including after the
engine and the card have been used.

**Run.** Cli port (COM17):

```
> revision .
0 ok>
> ecdc-test
AES via the service: 256/256 units match software; 548 us, software 2458 us
SM4 via the service: 256/256 units match software; 1553 us, software 815 us
AES via ctr_crypt_block (first use: checks the engine): 256/256 units match software; 2230 us, software 2461 us
AES via ctr_crypt_block again: 256/256 units match software; 1535 us, software 2464 us
SM4 via ctr_crypt_block (first use): 256/256 units match software; 1133 us, software 810 us
ctr_crypt_block uses the engine for AES: yes, SM4: yes
ok>
> dir
lfblocks.fb4              495616
lfstage.bin              1048576
lfbackup.bin             1048576
System Volume Information/
ok>
> 100 block drop .s
ok>
```

The fenced clocks read off at idle, after the engine (ecdc-test) and the
card (dir, block) have both been used. RCC is read-only to user mode (PMP
entry 2), so these are plain reads, through VM page 5: a register at
`0x40000000 + 4c` is at VM address `(5 << 19) + c`. `RCC->CTLR` (offset 0,
VM `288400`) is the addressing check -- its HSION bit is always set:

```
> hex 288400 @ .                 ( RCC->CTLR: HSION et al. )
3336983H ok>
> 288407 @ .                     ( RCC->HB2PCENR: ECDC bit 04000000 )
440FDH ok>                       ( 440FD & 04000000 = 0: ECDC clock off )
> 288406 @ . decimal             ( RCC->HBPCENR: DMA1 1, DMA2 2 )
20004H ok>                       ( 20004 & 3 = 0: DMA clocks off )
```

**Result:** pass. `ecdc-test` matches software for AES and SM4 on every
path, with the engine in use for both -- so the machine-mode ECDC service
decrypts correctly, and user mode gets its crypto without ever clocking the
engine itself. The card reads through Guarded DMA with the new per-transfer
clock drop. With the addressing confirmed by `RCC->CTLR` (HSION set), both
clock-enable registers show every fenced master off at idle -- and the ECDC
and DMA clocks read off *after* the engine and the card were used, so the
services turn their master's clock on only while they use it. Not tested: a
deliberate user-mode write to turn a master's clock on, to force the guard's
refusal (an escalation probe, left out by project rule as in B31); the
fence's logic is covered by `unit_tests/guard` (279 checks, including
`HB2PCENR enables ECDC`), its wiring into the fault handler by inspection
(`emulate_store`, `ch32h417_it.c`), and the readback above shows the clocks
off. This completes the four-part PMP plan (parts 1-4: B29, B33, B30/B31,
B32).

### B32. Level 2: the machine-mode region's root signature (30 October 2026)

**Board and firmware:** a second board, UID `e52116647f9c7a22`, at
`SECURITY_LEVEL` 2, locked down by the recovery `lockdown` command (option
bytes RDPR `00FF`, USER `C03F`, WRPR0 `FF00`), built in MounRiver from
branch `sys-sec-14-mmode-sign` and flashed by the user (V3F built
Oct 29 2026 13:27:00, GCC 12.2.0). Enrolled in recovery: `enroll` made the
key, `boards.csv` line
`e52116647f9c7a22,2f372551baf1b78c2d197bf9a71d1767ffe08f7637a8b0cd95cab530c9e9cd73`.

| Image | sha256 |
|---|---|
| `LiteForth_V5F.bin` (the signed machine-mode region is its slot's last 8 KB) | `90a2168c1e858893ab82424ca56b30b8d50f0134c4ca1c934963429876568ea7` |

Packages built with `sealblocks.py` (app component, 385024 bytes each,
376832-byte image) for this board's key, with the user's root and release
keys:

| Package | Version | Machine-mode signature | sha256 |
|---|---|---|---|
| `mmapp.lfu` | 1 | yes, root-signed (`mmode-sign` then `package --mmode-sig`) | `8a62593fd96dad60b03828900c9d97351905b19d5cdfd86fe558cea0b47efaf4` |
| `nomm3.lfu` | 3 | none (same image, no `--mmode-sig`) | `b115933c8e1f577134ba0313c35cc3b2971362ecbc2811e46f4f870a57c83456` |

**Why:** part 4 of the PMP plan (SYS-SEC-14,
`doc/admin/firmware-updates.md` section 8). The machine-mode region -- the
slot's last 8 KB, the `MMODE` window with the vector table, the bring-up to
its `mret`, the handlers and `.ramfunc`'s flash copy, all run in place --
carries a root Ed25519 signature over its BLAKE2s hash in block 0
(encrypted-blocks.md section 3). Stage 1 verifies it at every boot beside
the release signature (`sealed_check_signed`) and hashes the flash window
against it (`update.c`, pinning the window). A stolen release key can
re-sign block 0 but can't forge the root signature over changed
machine-mode code.

**Expected:** a machine-mode-signed app package installs, passes the root
and machine-mode checks, and the V5F starts and confirms. An otherwise
identical package with no machine-mode signature installs on trial but is
rejected by stage 1's boot check and rolled back to the signed image.

**Run (positive, `mmapp.lfu`, version 1), installed from recovery.** Debug
port:

```
ymodem: mmapp.lfu: 385024 bytes received
update: version 1 installed, on trial
recovery: installed; restarting to try it
update: version 1 on trial, boot 1 of 3
V3F stage 1: security level 2, installed version 1
V3F: starting the V5F
V5F: LiteForth starting, security level 2, booting from flash
V3F update: version 1 confirmed
```

The root and machine-mode signatures checked out, the V5F started on trial
and confirmed; the terminal ran LiteForth.

**Run (negative, `nomm3.lfu`, version 3).** Staged over the running v1,
then a pushbutton reset:

```
ymodem: nomm3.lfu: staged in lfstage.bin, 94 blocks, read back and checked
ymodem: package: V5F and app image, 376832 bytes, version 3: signature, recipient and blocks check out; waiting for stage 1 to install it
V3F update: rolled back to version 1
V3F stage 1: security level 2, installed version 1
V3F: starting the V5F
V5F: LiteForth starting, security level 2, booting from flash
```

The keyless staging check passed (release signature, recipient, block
hashes), but stage 1's boot check found no machine-mode signature
(`pkg.mmode_addr == 0`, which it requires at level 2), failed the trial and
rolled back to the signed version 1. The unsigned image never ran.

**Result:** pass, both ways. `nomm3.lfu` is byte-identical to the signed
image except for the missing root signature, and its release signature and
hashes were valid, so the rejection is the machine-mode check alone. A
first round earlier was refused with `lfbackup.bin needs 94 blocks` (the
test card's backup file was undersized); re-imaging the card with a 1 MB
`lfbackup.bin` fixed that and is unrelated to the signature check. Not
tested: a forged root signature (the root key is required to make one; the
desktop `unit_tests/seal` and `unit_tests/update` cover a wrong-root and a
mismatched-region signature).
