# Admin scripts

The release side of LiteForth: making keys, sealed block files and
firmware update packages. They need Python 3 and the `cryptography`
package. A board only checks what these make. The examples use `py`,
the Windows launcher, run in this folder; on Linux and macOS use
`python3`.

| Script | What it does |
|---|---|
| `sealblocks.py` | Makes and checks sealed (encrypted, signed) block files and firmware update packages, and their keys |
| `hiltest.py` | Runs stage 1's update, rollback and recovery tests on a real board, with nobody at it, and writes the report |
| `lockdown.py` | Flashes a board with a `SECURITY_LEVEL` 2 build and locks it down (read protection, write protection, boot ROM downloads off), and writes the report |

## sealblocks.py

```
py sealblocks.py root-key root
py sealblocks.py release-key root.key 1 release1.key --product 0x4C46
py sealblocks.py seal lfblocks.fb4 sealed.fb4 --release release1.key --boards boards.csv --revision 1
py sealblocks.py check sealed.fb4 --root root.pub
py sealblocks.py open sealed.fb4 opened.fb4 --root root.pub --board-key board.key
py sealblocks.py package LiteForth_V5F.bin v5f-1.lfu --component v5f --release release1.key --boards boards.csv --revision 1
py sealblocks.py lf-board board.lfb --board-key board.key --root root.pub --product 0x4C46
py sealblocks.py root-header root.pub --product 0x4C46 -o ..\..\src\target\CH32H417\V5F\User\sealed_root.h
```

Makes a sealed block file from a plain one, as planned in
[doc/encrypted-blocks.md](../../doc/encrypted-blocks.md): the content blocks
encrypted (AES-128 or SM4 in CTR mode) with a new key for each release,
that key wrapped for each board in `boards.csv` (`UID,public key,name` in
hex), the block hashes in hash table blocks after the content, and block 0
signed by a release key that the root key certifies. `check` checks
everything that needs no board key; `open` also unwraps and decrypts, as a
board would, and refuses what a board would refuse (`--min-revision`,
`--min-serial`, `--product`). `board-key` makes a board key pair on the PC
for testing; a real board will make its own. `lf-board` writes a board
file for the desktop `lf -K`, which then reads the sealed file as that
board would, and `root-header` the `sealed_root.h` a CH32H417 build reads
them with. On the board, `enroll` prints its `boards.csv` line.

Key files are JSON; `.key` files hold private keys. Keep the root's
offline: `release-key` is the only command that needs it. Needs the
`cryptography` package (`py -m pip install cryptography`).
`unit_tests/seal` tests it: round trips with both ciphers, the layout
against the formulas in the plan, and each refusal.

## hiltest.py

```
py hiltest.py --list
py hiltest.py check
py hiltest.py run --good v5f-1.lfu --hang v5f-2-hang.lfu
```

Tests stage 1 on a real CH32H417 board from the PC
([doc/firmware-updates.md](../../doc/firmware-updates.md) section 9). It
needs only two serial ports: the **cli port**, the board's USB terminal
(the V3F's USB CDC port, found by its USB IDs, 1A86:FE0C, or named with
`--cli`), and the **debug port**, the debug UART at 9600 baud (the
WCH-LinkE's UART, found by its USB IDs, or named with `--debug`; COM14
otherwise). It resets the chip by typing
`reboot` to LiteForth, or `reset` to stage 1's recovery console. A V5F
that doesn't start makes the V3F reset the chip by itself. So there's no
debug probe and no reset button. `--reset-cmd "COMMAND"` (or the
`HIL_RESET` environment variable) runs a command to reset instead.

- `--list` shows every serial port with its USB IDs, maker and product.
- `check` resets the board once and shows that the debug port, the cli
  port and LiteForth all answer.
- `run` plays these scenarios, or only one with `--only`:
  - `install`: GOOD.lfu staged with `-2 ymodem`, installed at the next
    reset, on trial, confirmed.
  - `rollback`: HANG.lfu, an image that never starts. The board goes
    through three trial boots and is rolled back, all by itself.
  - `recovery`: Ctrl+X x3 twice while the V5F restarts, which gives
    recovery. Then GOOD.lfu goes to stage 1 itself, is installed without
    a backup, and is confirmed.
  - `lockdown`: what the boot's lock-down check reports.

  It writes a Markdown report (`--report`, default `hil-report-DATE.md`)
  with each step, both ports' output and the packages' sha256, for
  [doc/board-tests.md](../../doc/board-tests.md).

The card needs `lfstage.bin` and `lfbackup.bin`. The packages write the
V5F's flash slot and the security record. Needs `pyserial`
(`py -m pip install pyserial`). `unit_tests/stage1sim/test_hil.py` runs
it against the simulated board.

## lockdown.py

```
py lockdown.py check
py lockdown.py run --v3f LiteForth_V3F.bin --v5f LiteForth_V5F.bin
py lockdown.py lock
```

Flashes a board with a `SECURITY_LEVEL` 2 build and locks it down: read
protection on, the V3F's sectors write-protected, the boot ROM's USB and
USART downloads off. The debug port stays, so a chip erase through the
WCH-LinkE (`wlink unprotect`, done by hand) always takes a board back; this
script never runs it, or any erase. `check` only reads: the probe, the
UID, the option bytes, and whether the board is locked down. `lock` is
`run` without the flashing (steps 3 to 5), for a board that already runs a
level 2 build and sits in recovery.

The WCH-LinkE sometimes fails to attach (`underlying protocol error:
0x55`) and works a second later, so the script pauses before each wlink
call and tries `status`, `dump` and `reset` up to four times, and a flash
again if it failed before writing anything; each retry is in the report.

`run`:
1. Refuses a board that's read- or write-protected already, since wlink's
   flash would unprotect it, and so wipe it or clear its write protection.
2. Flashes both images with wlink (a `.bin` at 0x08000000 and 0x08010000,
   or `--v3f-address`, `--v5f-address`; a `.hex` or `.elf` carries its
   own), without erasing anything else, reads the start of each back, and
   resets the chip.
3. Checks that stage 1 stays in recovery and that its console offers
   `lockdown`: a level 2 build on an open board.
4. Asks you to type `LOCK`, then sends `lockdown yes`. Stage 1 programs the
   option bytes (one erase, one pass), reads them back and resets.
5. Waits for LiteForth to start with no lock-down warning, asking you to
   power the board off and on if the option bytes didn't load at that
   reset, and checks that `wlink status` says the flash is protected.

The report (`--report`, default `lockdown-report-DATE.md`) has every step,
both ports' output, the images' sha256 and the UID, for
[doc/board-tests.md](../../doc/board-tests.md). Then run `enroll` (a level
2 board's key comes from the RNG), add its line to `boards.csv`, and seal
the card's `lfblocks.fb4` and packages for it.

It needs wlink (see `scripts/README.md`, which `lfmcp.py`'s `wlink` tool
uses too) and `pyserial`. It finds the debug port (the WCH-Link's UART)
and the cli port (the board's USB terminal) by their USB IDs, or name them
with `--debug` and `--cli` (`hiltest.py --list` shows which is which). Try it on a spare board first.
`unit_tests/stage1sim/test_lockdown.py` runs it against the simulated
board with a fake wlink.
