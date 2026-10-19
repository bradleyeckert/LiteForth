# Updating a board's blocks

A LiteForth application lives in `lfblocks.fb4`, the block file on the
board's microSD card: its Forth source, fonts and data. The firmware (the
C code you build in MounRiver) changes rarely; the block file is what you
update. This page covers both kinds of block file, how to set up a product
so that it takes only files you have signed, how to send a new file to a
board over its USB terminal, and the build options involved.

- **Plain** files are for development. Anyone can read and change them,
  and the board can write to them (the editor, `ymodem`, `update`).
- **Sealed** files are for products ([encrypted-blocks.md](encrypted-blocks.md)).
  They are encrypted for a list of boards and signed by you. A board checks
  every block it reads, and nothing on it can be changed: a new release
  replaces the whole file.

Either kind goes onto a board in one of two ways: copy it onto the card in
a PC ([sdcard.md](sdcard.md)), or send it over the terminal with
`-1 ymodem`, below, with the card left in the board.

## 1. Development

Nothing to set up beyond [sdcard.md](sdcard.md): a plain `lfblocks.fb4` on
the card. While working:

| To change | Use |
|---|---|
| A few blocks of source | `~x core.f` in kwai (`0 ymodem`): writes the blocks its `( BLOCK n )` lines name |
| A font or other binary | `~x cjk24.fnt 100` (`100 ymodem`): from byte 32 of block 100 |
| One block, by hand | the block editor ([editor.md](editor.md)) |
| The whole file | `~x lfblocks.fb4 -1` (`-1 ymodem`, section 4) |
| The C firmware | `~x update.lfu -2` (`-2 ymodem`): stages a package from `sealblocks.py package` in the card's `lfstage.bin` for stage 1 to install ([firmware-updates.md](firmware-updates.md)). Stage 1 isn't built yet, so for now this only stages and checks it |

Then `1 load` rebuilds the dictionary. [blocks.md](blocks.md) has the
details of each.

## 2. Setting up a product, once

Do this on an offline machine for the root key, and keep the root's
private key there. The tool is `scripts/sealblocks.py` (it needs
`pip install cryptography`).

1. **The root key.** `sealblocks.py root-key root` makes `root.key`
   (private: stays offline) and `root.pub`. Every board built for the
   product trusts it, and it can't be changed without new firmware.
2. **A product ID.** A 32-bit number for this product, so that files made
   for one product don't open on another. LiteForth's own tests use
   `0x4C46`.
3. **A release key.** `sealblocks.py release-key root.key 1 release1.key
   --product 0x4C46` makes release key 1, certified by the root. This is the
   key you sign releases with day to day, so the root can stay offline.
   Serial numbers count up when a release key is replaced.
4. **The firmware's `sealed_root.h`.** `sealblocks.py root-header root.pub
   --product 0x4C46 --sealed-only -o src/target/CH32H417/V5F/User/sealed_root.h` writes the
   header the V5F build reads (section 6). Git ignores it, since it belongs
   to your application, not to LiteForth. Leave out `--sealed-only` while
   you still want the board to take plain files.
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

A board built with `INSECURE` (section 6) prints the same line every time,
since its key comes from its UID.

Treat `boards.csv` like code, with reviewed commits: whoever can add a line
decides which boards can read your releases. Running `enroll` again only
prints the line (without `new key made`). If it says `new key made` on a
board that was enrolled before, its key was erased (an *Erase All*
download), so its line in `boards.csv` must be replaced and the current
release sealed again before that board can read it.

## 4. Making and installing a release

1. **Build the plain file** as in development (`forth/build.bat`, or
   `putblocks.py` and `putbin.py` into a copy of `bin/lfblocks.fb4`), and
   test it on a development board.
2. **Seal it** with the next revision number:

   ```
   sealblocks.py seal lfblocks.fb4 release.fb4 --release release1.key --boards boards.csv --revision 2
   sealblocks.py check release.fb4 --root root.pub
   ```

   `check` verifies everything a board would, except the parts that need a
   board's private key. `--cipher sm4` uses SM4 instead of AES-128.
3. **Install it.** In kwai, connected to the board: `~x release.fb4 -1`.
   With another terminal program, type `-1 ymodem` and send the file by
   YMODEM within a minute. kwai prints its progress and LiteForth reports
   when it's done:

   ```
   ok>~x release.fb4 -1
   ymodem: send the file(s) now (YMODEM)
   [kwai: sending release.fb4: 499712 bytes]
   [kwai: 488/488]
   [kwai: sent release.fb4]
   ymodem: release.fb4: installed, 122 blocks, read back and checked; capacity now 121
   ymodem: sealed file, revision 2: all 120 content blocks pass their hashes
   ```

4. **`1 load`** builds the dictionary from it, and `cold` starts the
   application.

Copying the file onto the card as `lfblocks.fb4` in a PC works too; the
board checks it the same way when it starts.

### What `-1 ymodem` checks

1. **Before writing anything**, block 0, which arrives first. A plain file
   must start with `LITEFORTH` (and is refused on a `SEALED_ONLY` build).
   A sealed file must be one this board would open: the release key's
   certificate matches the built-in root key, block 0's signature matches
   the release key, the product ID matches, the revision and release key
   serial aren't below the board's minimums, and the board is one of the
   recipients and can unwrap the content key. The file must also be whole
   4 KB blocks and fit in the card's `lfblocks.fb4` (below). If any of this
   fails, the old file stays as it was.
2. **While writing**, block 0 on the card says the install hasn't finished
   (a plain block 0 reading `LITEFORTH ( an install by -1 ymodem didn't
   finish: send the block file again )`), and the real block 0 goes in
   last. A transfer cut short leaves that, so the board never starts with
   half of one file and half of another: send the file again.
3. **After the transfer**, the whole file is read back from the card and
   compared with what arrived (BLAKE2s), and the block file is mounted
   again, which sets `capacity`. For a sealed file, every content block is
   read and checked against its hash.

The file goes into the space the card's `lfblocks.fb4` already has: the
board can't make a file on the card longer. A smaller file leaves the
blocks after it as they were. So when preparing a card, make its
`lfblocks.fb4` larger than the releases it will get, with room for the
sealed file's hash table blocks (one per 256 content blocks): add blank blocks to
the plain file before copying it (`putblocks.py` extends a file it writes
past the end of).

### When it refuses

Each refusal ends with `nothing written` and leaves the board's file alone.

| Message | Cause | What to do |
|---|---|---|
| `it's bigger than the block file's space` (-92) | The file is longer than the card's `lfblocks.fb4` | Copy a bigger `lfblocks.fb4` to the card once |
| `a block file is a whole number of 4 KB blocks` (-124) | Not a block file, or truncated | Send the right file |
| `block 0 doesn't start with LITEFORTH` (-124) | Not a block file | Send the right file |
| `a plain block file, but this build takes only sealed files` (-124) | Plain file to a `SEALED_ONLY` board | Seal it first |
| `this board isn't one of its recipients` (-124) | The board's line wasn't in `boards.csv` when it was sealed | Add the board, seal again |
| `the content key doesn't unwrap (CEK check)` (-124) | Sealed for this board's old key (it was enrolled again since) | Replace its line in `boards.csv`, seal again |
| `this board has no key: run enroll` (-124) | The board isn't enrolled, or its key was erased | `enroll`, then as above |
| `no root key built in` (-124) | The firmware was built without `sealed_root.h` | Section 2, steps 4 and 5 |
| `made for another product` (-124) | Different product ID | Seal with this product's release key |
| `the release key's certificate doesn't match the root key` (-124) | Signed under another root | Use this product's release key |
| `an older revision than this board takes` (-124) | Below the board's lowest revision | Seal with a higher revision |
| `the release key has been replaced` (-124) | Signed with a retired release key | Use the current one |
| `a firmware update package, not a block file` (-124) | A `.lfu` made by `sealblocks.py package` | Packages are for firmware updates ([firmware-updates.md](firmware-updates.md)), not blocks |
| `the transfer stopped` (-123) | The connection broke or the sender cancelled | Send it again |

Two more outcomes need a second look:

- **`N of M content blocks fail their hashes` (-33):** the file was written
  but some of its blocks don't match their hashes, so it was damaged after
  sealing (the hash tables come last, so this can only be found once it's
  written). Send a good copy.
- **`what was read back differs from what came`:** the card didn't store
  what was sent. Send it again; if it happens twice, suspect the card.

## 5. Replacing keys and rolling back

- **A new release key** (a leaked one, or routine): make release key 2 with
  `release-key root.key 2 ...`, and seal with it from then on.
- **A board leaving the product:** leave its line out of `boards.csv`. It
  keeps what it could already read, not anything newer.
- **Not yet done (step 5 of [encrypted-blocks.md](encrypted-blocks.md)):**
  the board's lowest revision and lowest release key serial are stored but
  never raised, so today a board will take an older release, and a file
  signed with a retired release key, if they were made for it.

## 6. Build options

### The V5F firmware: `V5F/User/sealed_root.h`

Your application's file, beside `board.c`, written by
`sealblocks.py root-header` or copied from `sealed_root.example.h`. Git
ignores it. Clean the project after changing it (section 2).

| Setting | Meaning |
|---|---|
| `SEALED_ROOT_KEY` | The root's Ed25519 public key, 32 bytes. Without `sealed_root.h` the board reads no sealed files (`no root key built in`) but still takes plain ones. |
| `SEALED_PRODUCT` | The 32-bit product ID that release keys and sealed files must name. |
| `SEALED_ONLY` | Optional. `1`: the board takes only sealed files. A plain `lfblocks.fb4` offers no blocks (`capacity` 0; the debug UART says why), and `-1 ymodem` refuses plain files. A sealed file can still be installed over it. Leave it out for development boards. |
| `INSECURE` | Optional, development only. `1`: the board's private key is BLAKE2s of its unique ID (personalization `LFinsec1`) instead of coming from the RNG, so `enroll` prints the same line every time, the board needs no flash record to open sealed files, and a download that erases the flash loses nothing. `sealblocks.py board-key OUT --uid UID --insecure` gives the same key, and so the boards.csv line, without the board. Anyone who knows the UID can read what is sealed for it: never in a product. |

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

`lf -K board.lfb` plays a board: the file holds what a board keeps
(`sealblocks.py lf-board`, format in `src/target/desktop/board.h`). Its
`sealed-only 1` setting (`lf-board --sealed-only`) acts like a
`SEALED_ONLY` build, so you can try a product's behavior before building
it. Without `-K`, `lf` takes only plain files. `-k` names the block file
or a card image, which `-1 ymodem` installs into the same way.

## 7. Known limits

- Anyone at the terminal of a board without `SEALED_ONLY` can install a
  plain file. That gives away nothing sealed, but it can replace the
  application.
- An install cut short leaves the board without its application until the
  file is sent again; there's only one copy on the card. On a `SEALED_ONLY`
  board the unfinished file offers no blocks, but `-1 ymodem` still works.
- An `INSECURE` build's key follows from its UID, which any code on the
  board, or anyone who has seen `enroll`'s output, can read.
- Code running on the board can read its key and the content key
  ([encrypted-blocks.md](encrypted-blocks.md) section 9).
