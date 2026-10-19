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

The first run of [scripts/admin/hiltest.py](../scripts/admin/hiltest.py) on the board. Nobody touched the board during the run: every reset was `reboot` typed to LiteForth, `reset` typed to the recovery console, or the V3F's own reset when the V5F didn't start.

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
[ch32h417-notes.md](ch32h417-notes.md).

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
