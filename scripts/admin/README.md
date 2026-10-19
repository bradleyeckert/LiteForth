# Admin scripts

The release side of LiteForth: making keys, sealed block files and
firmware update packages. They need Python 3 and the `cryptography`
package. A board only checks what these make. The examples use `py`,
the Windows launcher, run in this folder; on Linux and macOS use
`python3`.

| Script | What it does |
|---|---|
| `sealblocks.py` | Makes and checks sealed (encrypted, signed) block files and firmware update packages, and their keys |

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
