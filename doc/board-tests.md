# Board tests: firmware updates and isolation

This is the record of security tests run on real hardware, kept as
evidence for a later cybersecurity document. Each entry gives what was
tested, the firmware and keys involved, how it was run, what was expected,
and what the board printed, word for word. The design these tests check is
in [firmware-updates.md](firmware-updates.md), and the PMP findings are in
its section 8. The desktop tests (`unit_tests/update`, `sealed`, `seal`)
cover every refusal and a power cut at every step. This log covers what
only the board can show.

## The setup

| | |
|---|---|
| Board | nanoCH32H417 (WCH CH32H417, V3F + V5F cores), UID `69a90e647f9cf6a9` |
| Protection | RDP 0, no write protection (WRPR): everything can be undone with the WCH-Link ([firmware-updates.md](firmware-updates.md) section 9, step 2) |
| Board key | `INSECURE` build: the X25519 private key is BLAKE2s of the UID (person `LFinsec1`). For testing only; a production board uses the random key from `enroll` |
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

