# Updating a board's blocks

A LiteForth application starts in `lfblocks.fb4`, the plain block file on
the board's microSD card: its Forth source, fonts and data. `1 load`
compiles it into LiteForth's flash pages. This page covers developing
with block files, sending a new one to a board over its USB terminal, how
to set up a product so that it takes only updates you have signed, and
the build options involved.

- **Block files** are plain: anyone can read and change them, and the
  board can write to them (the editor, `ymodem`, `update`). Copy one onto
  the card in a PC ([sdcard.md](../sdcard.md)), or send it over the
  terminal with `-1 ymodem` (section 4), with the card left in the board.
- **A product's release** is an **app package**
  ([firmware-updates.md](firmware-updates.md) section 5): the V5F firmware
  and LiteForth's flash pages, as a developer built them from block files,
  encrypted for a list of boards and signed by you. Stage 1 installs it,
  backs up what it replaces, and at `SECURITY_LEVEL` 2 checks the pages
  at every boot.

Sealed block files (encrypted, signed `lfblocks.fb4`,
[encrypted-blocks.md](encrypted-blocks.md)) were the first way to ship an
application; they are retired, and a board refuses one.

## 1. Development

Nothing to set up beyond [sdcard.md](../sdcard.md): a plain `lfblocks.fb4` on
the card. While working:

| To change | Use |
|---|---|
| A few blocks of source | `~x core.f` in kwai (`0 ymodem`): writes the blocks its `( BLOCK n )` lines name |
| A font or other binary | `~x cjk24.fnt 100` (`100 ymodem`): from byte 32 of block 100 |
| One block, by hand | the block editor ([editor.md](../editor.md)) |
| The whole file | `~x lfblocks.fb4 -1` (`-1 ymodem`, section 4) |
| The C firmware, or firmware and flash pages | `~x update.lfu -2` (`-2 ymodem`): stages a package from `sealblocks.py package` in the card's `lfstage.bin` for stage 1 to install ([firmware-updates.md](firmware-updates.md)). At the next reset the V3F's stage 1 backs up what it replaces to `lfbackup.bin`, installs the new image on trial, and the V5F confirms it when LiteForth starts |

Then `1 load` rebuilds the dictionary. [blocks.md](../blocks.md) has the
details of each.

## 2. Setting up a product, once

Do this on an offline machine for the root key, and keep the root's
private key there. The tool is `scripts/admin/sealblocks.py` (it needs
`pip install cryptography`).

1. **The root key.** `sealblocks.py root-key root` makes `root.key`
   (private: stays offline) and `root.pub`. Every board built for the
   product trusts it, and it can't be changed without new firmware.
2. **A product ID.** A 32-bit number for this product, so that packages
   made for one product don't open on another. LiteForth's own tests use
   `0x4C46`.
3. **A release key.** `sealblocks.py release-key root.key 1 release1.key
   --product 0x4C46` makes release key 1, certified by the root. This is the
   key you sign releases with day to day, so the root can stay offline.
   Serial numbers count up when a release key is replaced.
4. **The firmware's `sealed_root.h`.** `sealblocks.py root-header root.pub
   --product 0x4C46 -o src/target/CH32H417/V5F/User/sealed_root.h` writes the
   header the V5F build reads (section 6). Git ignores it, since it belongs
   to your application, not to LiteForth.
5. **Build the V5F firmware clean.** After adding or changing
   `sealed_root.h`, use *Project > Clean* before building: MounRiver doesn't
   notice a header that didn't exist at the last build, and an incremental
   build keeps the old `board.o` (the board then says `no root key built in`).
6. **Download with Clear CodeFlash off** (`"clearcodeflash": false` in
   `LiteForth_V5F.wvproj`, as the repository has it). With it on, or with
   *Erase All*, a download erases the board's key along with everything
   else, and the board must be enrolled again (section 3).

## 3. Enrolling each board, once

On each board, type `enroll`:

```
ok>enroll
enroll: new key made. boards.csv line:
69a90e647f9cf6a9,0129f3d8f018b205a74f0bd9bc7cec8debc4e815a7bfda37768baf7cd5803166
```

The board made an X25519 key pair from its random number generator and
kept the private half in flash (the 8K sector at `0x08070000`); it never
leaves the board. The line is its unique ID and public key. Add it to
`boards.csv` with a name of your choosing:

```
69a90e647f9cf6a9,0129f3d8f018b205a74f0bd9bc7cec8debc4e815a7bfda37768baf7cd5803166,nano1
```

A board built below `SECURITY_LEVEL` 2 (section 6) prints the same line
every time, since its key comes from its UID. That key is for development
only: it's no secret, and a level 2 build never uses it, so a board moved
to level 2 runs `enroll` again and gets a new line.

Treat `boards.csv` like code, with reviewed commits: whoever can add a line
decides which boards can install your releases. Running `enroll` again only
prints the line (without `new key made`). If it says `new key made` on a
board that was enrolled before, its key was erased (an *Erase All*
download), so its line in `boards.csv` must be replaced and the current
release packaged again before that board can install it.

## 4. Making and installing a release

1. **Build the application** as in development (`forth/build.bat`, or
   `putblocks.py` and `putbin.py` into a copy of `bin/lfblocks.fb4`), on a
   development board (`SECURITY_LEVEL` 0 or 1): `1 load`, cache its fonts,
   and test it.
2. **Save its flash pages:** `save-pages` copies them into `lfpages.bin`
   on the card (made beforehand like `lfstage.bin`: 262144 bytes,
   contiguous).
3. **Package them** with the V5F build and the next version number:

   ```
   sealblocks.py package LiteForth_V5F.bin app.lfu --component app --pages lfpages.bin --release release1.key --boards boards.csv --revision 2
   sealblocks.py check app.lfu --root root.pub
   ```

   `check` verifies everything a board would, except the parts that need a
   board's private key. `--cipher sm4` uses SM4 instead of AES-128.
4. **Install it.** In kwai, connected to the board: `~x app.lfu -2`, then
   `reboot`. Stage 1 installs it at the reset
   ([firmware-updates.md](firmware-updates.md) section 6 has the details,
   the card files it needs, and what it prints).

### Sending a block file: `-1 ymodem`

In kwai, connected to the board: `~x lfblocks.fb4 -1`. With another
terminal program, type `-1 ymodem` and send the file by YMODEM within a
minute. kwai prints its progress and LiteForth reports when it's done:

```
ok>~x lfblocks.fb4 -1
ymodem: send the file(s) now (YMODEM)
[kwai: sending lfblocks.fb4: 495616 bytes]
[kwai: 484/484]
[kwai: sent lfblocks.fb4]
ymodem: lfblocks.fb4: installed, 121 blocks, read back and checked; capacity now 121
```

Then `1 load` builds the dictionary from it. Copying the file onto the
card as `lfblocks.fb4` in a PC works too.

### What `-1 ymodem` checks

1. **Before writing anything**, block 0, which arrives first. It must
   start with `LITEFORTH` and not be sealed: a firmware update package
   (`LITEFORTHF`) or a retired sealed block file (`LITEFORTHS`) is
   refused. The file must also be whole 4 KB blocks and fit in the card's
   `lfblocks.fb4` (below). If any of this fails, the old file stays as it
   was.
2. **While writing**, block 0 on the card says the install hasn't finished
   (a plain block 0 reading `LITEFORTH ( an install by -1 ymodem didn't
   finish: send the block file again )`), and the real block 0 goes in
   last. A transfer cut short leaves that, so the board never starts with
   half of one file and half of another: send the file again.
3. **After the transfer**, the whole file is read back from the card and
   compared with what arrived (BLAKE2s), and the block file is mounted
   again, which sets `capacity`.

The file goes into the space the card's `lfblocks.fb4` already has: the
board can't make a file on the card longer. A smaller file leaves the
blocks after it as they were. So when preparing a card, make its
`lfblocks.fb4` larger than the files it will get: add blank blocks to the
file before copying it (`putblocks.py` extends a file it writes past the
end of).

### When it refuses

Each refusal ends with `nothing written` and leaves the board's file alone.

| Message | Cause | What to do |
|---|---|---|
| `it's bigger than the block file's space` (-92) | The file is longer than the card's `lfblocks.fb4` | Copy a bigger `lfblocks.fb4` to the card once |
| `a block file is a whole number of 4 KB blocks` (-124) | Not a block file, or truncated | Send the right file |
| `block 0 doesn't start with LITEFORTH` (-124) | Not a block file | Send the right file |
| `a firmware update package, not a block file` (-124) | A `.lfu` made by `sealblocks.py package` | Send it with `-2 ymodem` ([firmware-updates.md](firmware-updates.md)) |
| `a sealed block file: no longer read` (-124) | A sealed `lfblocks.fb4` from before 2026-10-21 | Send the plain file; ship the application in an app package |
| `the transfer stopped` (-123) | The connection broke or the sender cancelled | Send it again |

One more outcome needs a second look: **`what was read back differs from
what came`**: the card didn't store what was sent. Send it again; if it
happens twice, suspect the card.

## 5. Replacing keys and rolling back

- **A new release key** (a leaked one, or routine): make release key 2 with
  `release-key root.key 2 ...`, and package with it from then on.
- **A board leaving the product:** leave its line out of `boards.csv`. It
  keeps what it could already install, not anything newer.
- **Rolling back** is stage 1's: it keeps a backup of what it replaced,
  and the lowest version and release key serial a board takes are in its
  record ([firmware-updates.md](firmware-updates.md) sections 4 and 6).

## 6. Build options

### Both cores: `Common/security.h`

`SECURITY_LEVEL` (default 0) sets the security of both cores' builds. Set
it in the header, or with `-DSECURITY_LEVEL=n` in both projects. Each level
adds to the one below:

| | Core | 0 | 1 | 2 |
|---|---|---|---|---|
| Forth reaches the peripherals | V5F | all | the allowlist | the allowlist |
| The board's key (`enroll`) | both | from the UID | from the UID | from the RNG |
| A board that isn't locked down | V3F | a warning | a warning | no V5F: recovery, `reset` only |
| Stage 1's `adopt` | V3F | yes | yes | no |
| Flash pages written by | both | Forth | Forth | packages only |
| Forth starts with | V5F | `root` | `root` | `root`; later a limited security wordlist |

Both cores work out the board's key, the V5F in `board.c` (`enroll`,
checking a staged package) and the V3F's stage 1 in `keys_get`
(installing it), so build both projects at the same level.

- **0, development:** the regular Forth from a plain `lfblocks.fb4`, and
  every peripheral register.
- **1, development with the product's I/O:** Forth reaches only the
  peripherals in `V5F/User/io_allow.h`: GPIO, timers, serial ports and
  the like, but not the flash controller, DMA, SDIO, the ECDC, the RNG,
  RCC/PWR writes or USB. Anything else reads as an invalid address (-9),
  and a write to a read-only one is write-protected (-20).
- **2, a product:** the key from the RNG, kept in the board's flash
  record; only packages write LiteForth's flash pages (`open-flash` and
  `save-pages` refuse), and stage 1 checks them at every boot; no
  `adopt`; and the V5F starts only on a locked board.

The UID key at levels 0 and 1 is BLAKE2s of the board's unique ID
(personalization `LFinsec1`), so `enroll` prints the same line every time,
the board needs no flash record to open packages, and a download that
erases the flash loses nothing. `sealblocks.py board-key OUT --uid UID
--insecure` gives the same key, and so the boards.csv line, without the
board. It's no secret: anyone who knows the UID can open packages made
for it. A level 2 build never uses it, so moving a board to level 2 means
`enroll` (a new key), a new line in `boards.csv`, and packaging its
releases again.

The old flags `SEALED_ONLY`, `INSECURE`, `STRICT_RDP`,
`STAGE1_DEVELOPMENT` and `DEVELOPER_MODE` are gone; defining one (in
`sealed_root.h` too) stops the build.

### The V5F firmware: `V5F/User/sealed_root.h`

Your application's file, beside `board.c`, written by
`sealblocks.py root-header` or copied from `sealed_root.example.h`. Git
ignores it. Clean the project after changing it (section 2).

| Setting | Meaning |
|---|---|
| `SEALED_ROOT_KEY` | The root's Ed25519 public key, 32 bytes. Without `sealed_root.h` the V5F can't check packages (`no root key built in`). |
| `SEALED_PRODUCT` | The 32-bit product ID that release keys and packages must name. |

### The MounRiver projects

| Setting | Where | Meaning |
|---|---|---|
| Clear CodeFlash | `"clearcodeflash"` in `LiteForth_V5F.wvproj` (download settings) | `false` (the repository's setting): a download erases only the sectors the image covers, keeping LiteForth's flash (the dictionary) and the board's key. `true`, or *Erase All*, wipes both. |

### Source options

Set them in `options.h` or on the compiler's command line.

| Setting | File | Default | Meaning |
|---|---|---|---|
| `YMODEM_START_SECONDS` | `src/ymodem.c` | 60 | How long `ymodem` waits for the sender to start |
| `BLK_DMA` | `V5F/User/blocks.c` | 1 | 0: WCH's polling card transfers instead of DMA |

### The desktop `lf`

`lf -K board.lfb` plays a board for `-2 ymodem`, which then checks a
staged package as the board would: the file holds what a board keeps
(`sealblocks.py lf-board`, format in `src/target/desktop/board.h`). `-k`
names the block file or a card image, which `-1 ymodem` installs into the
same way.

## 7. Known limits

- Anyone at the Forth console can install a block file and `1 load` it,
  and below `SECURITY_LEVEL` 2 rewrite the flash pages. A secure login to
  keep users out of the console is planned.
- An install cut short leaves the board without its block file until the
  file is sent again; there's only one copy on the card.
- Below level 2 the board's key follows from its UID, which any code on
  the board, or anyone who has seen `enroll`'s output, can read. It's for
  development only.
- The V5F holds the board's key, in machine mode behind the PMP, to check
  staged packages ([encrypted-blocks.md](encrypted-blocks.md) section 9);
  stage 1 is to hold it instead.
