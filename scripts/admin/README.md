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
py hiltest.py check --debug COM15 --wlink D:\Apps\wlink-win-x64\wlink.exe
py hiltest.py run --debug COM15 --wlink D:\Apps\wlink-win-x64\wlink.exe --good v5f-1.lfu --hang v5f-2-hang.lfu
```

Tests stage 1 on a real CH32H417 board from the PC
([doc/firmware-updates.md](../../doc/firmware-updates.md) section 9). It
reads the debug UART through the WCH-LinkE (9600 baud), drives the board's
USB terminal as kwai does (the port is found by its USB IDs, 1A86:FE0C, or
named with `--usb`), and resets the chip with
[wlink](https://github.com/ch32-rs/wlink) (`--wlink`, or the `WLINK`
environment variable; `--reset-args`, default `reset`).

- `--list` shows every serial port with its USB IDs, maker and product,
  so you can tell the WCH-LinkE's UART from the board's own port.
- `check` resets the board once and shows that the debug UART, the USB
  terminal and LiteForth all answer.
- `run` plays these scenarios, or only one with `--only`:
  - `install`: GOOD.lfu staged with `-2 ymodem`, installed at the next
    reset, on trial, confirmed.
  - `rollback`: HANG.lfu, an image that never starts, through three trial
    boots, then rolled back.
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
