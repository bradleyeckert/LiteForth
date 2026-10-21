# The sealed format: encrypted, signed update packages

The sealed format encrypts and signs a run of 4 KB blocks so that only
the boards it was made for can read it, and none of them will use it
unless the vendor signed it. Firmware update packages use it
([firmware-updates.md](firmware-updates.md) section 5): stage 1 opens
them, and `-2 ymodem` checks them first.

It was designed (2026-10-17) for **sealed block files**: an `lfblocks.fb4`
whose content blocks were encrypted, mounted read-only and decrypted
block by block as `block` read them. Those were retired on 2026-10-21,
once stage 1 could install packages: two ways to install updates is one
too many. A product's Forth now goes in an **app package** (component 3):
developers build the dictionary from plain block files, `save-pages`
copies LiteForth's flash pages to `lfpages.bin`, and the administrator
packages them with the V5F image. Block files are plain at every
`SECURITY_LEVEL`. A block 0 that starts with `LITEFORTHS` (a sealed
block file) or `LITEFORTHF` (a package) is never mounted as a block file,
and `-1 ymodem` never installs one (`sealed_not_blocks` in `src/sealed.h`).

Goals:

- **No master key.** Nothing secret is shared between boards or kept by
  every board. A board holds its own private key and the vendor's public
  root key; the vendor holds the private root and release keys and a list
  of the boards' public keys.
- **Random access.** Any block can be checked and decrypted by itself, in
  hardware (AES or SM4 in CTR mode on the ECDC engine), so stage 1 can
  install a package a block at a time.
- **Standard parts.** X25519, Ed25519, BLAKE2s (RFC 7693) and AES-128 or
  SM4, so the PC side is a short Python script on `cryptography` and
  `hashlib`.

Security holes we know about and will plug later are listed at the end
(section 9), not fixed here.

## 1. Keys

### The vendor's keys

| Key | Type | Kept | Used to |
|---|---|---|---|
| Root key | Ed25519 | Offline: an air-gapped PC or a hardware token, with two backups | Certify release keys. Its public half is built into every board's firmware |
| Release key | Ed25519, with a serial number | The build PC | Sign each package's block 0 |
| Content key (CEK) | 128-bit, random | Nowhere, after the package is built (or an offline archive, see 6.4) | Encrypt one package's content blocks. Never used for two packages |
| Release ephemeral key | X25519, random | Nowhere: thrown away once the package is built | Wrap the CEK for each board |
| Board list | UID + X25519 public key per board | The build PC, under version control | Choose who can read a release |

### Each board's keys

| Item | Kept | Notes |
|---|---|---|
| Board key | X25519 private key, 32 bytes, in protected flash | Made on the board from its RNG when it's provisioned. Never leaves it |
| UID | `ESIG_UNIID1` and `UNIID2`, 8 bytes | Finds the board's entry in a file. Not secret. (`UNIID3` reads as erased flash, see [ch32h417-notes.md](../ch32h417-notes.md)) |
| Root public key | Ed25519, 32 bytes, in the firmware | Changes only with a new firmware image |
| Lowest version | Stage 1's record ([firmware-updates.md](firmware-updates.md) section 4) | Anti-rollback: older V5F images are refused |
| Lowest release key serial | 4 bytes in protected flash | A replaced release key can't sign again |

A package only ever carries public keys and wrapped CEKs. The release
public key in block 0 proves nothing by itself, since anyone can sign with
their own key and include it. It counts only because the root key signed
it (the certificate in section 3).

## 2. The package

```
block 0          header: plain text, signed
blocks 1..C-1    content: encrypted (the image, padded with 0xFF to whole blocks)
blocks C..C+T-1  hash tables: plain, each checked against block 0
```

### Encryption

Each 4 KB content block is 256 cipher blocks of 16 bytes, so the counter
steps by **256** per block, not 128. The 16-byte counter block for the
first 16 bytes of block n is

```
nonce (8 bytes, from block 0) || n * 256 (8 bytes, big-endian)
```

and it counts up by one for each 16 bytes, which is how AES-CTR is
normally defined (a 128-bit big-endian counter) and what Python's
`cryptography` does. The count never reaches the nonce: that would take
2^56 blocks. The ECDC engine steps its counter by one per 16 bytes in its
RAM-to-RAM mode, matching this for whole blocks (measured with
`ecdc-test`; its byte order and other quirks are in
[ch32h417-notes.md](../ch32h417-notes.md)).

The CEK is new for every release, and so is the nonce. A key and counter
pair is therefore never used on two different plaintexts.

### Integrity

CTR doesn't detect changes: flipping a bit of the file flips the same bit
of the plaintext. So every content block has a hash, made over the
**encrypted** block so it can be checked before decrypting, and by a PC
without any key:

```
entry n = BLAKE2s(encrypted block n, digest_size=16, salt=n as 8 bytes LE, person="LFblock")
```

The salt ties each hash to its block number, so moving or swapping
blocks is caught. A 16-byte (128-bit) BLAKE2s digest is ample against
forging a block for a given hash.

A hash table block holds 256 entries (4096 / 16): table block t covers
content blocks 256t to 256t + 255 (entry 0 of table 0, for block 0, is
zero). Block 0 holds a 32-byte hash of each table block:

```
BLAKE2s(table block t, digest_size=32, salt=t as 8 bytes LE, person="LFtable")
```

So one table block covers 1 MB of content, and block 0 has room for the
tables of a few tens of MB (section 3). No package is bigger than 1 MB
today, so packages have one table block.

## 3. Block 0

All numbers little-endian.

| Offset | Size | Contents |
|---|---|---|
| 0 | 9 | `LITEFORTH`, as in every block file |
| 9 | 1 | `F`: a firmware update package. (`S` was a sealed block file, now refused; plain block files have `B` here, from `LITEFORTHBLK`) |
| 10 | 1 | Format version, 1 |
| 11 | 1 | Cipher: 1 = AES-128-CTR, 2 = SM4-CTR |
| 12 | 4 | Flags, 0 |
| 16 | 4 | Product ID |
| 20 | 4 | Revision: the image's version |
| 24 | 4 | C: the first block after the content (content is blocks 1 to C-1) |
| 28 | 2 | T: hash table blocks, at blocks C to C+T-1 |
| 30 | 2 | R: recipient entries |
| 32 | 8 | CTR nonce, random |
| 40 | 32 | Release ephemeral X25519 public key |
| 72 | 8 | CEK check: `BLAKE2s(nonce, digest_size=8, key=CEK, person="LFcheck")` |
| 80 | 4 | Release key serial |
| 84 | 32 | Release Ed25519 public key |
| 116 | 64 | Certificate: the root key's Ed25519 signature over bytes 16-19 and 80-115 (product ID, serial, release public key) |
| 180 | 12 | The image's description ([firmware-updates.md](firmware-updates.md) section 5): component, flags, hardware revision, length, new minimum version |
| 192 | 32 T | Table hashes, one per hash table block |
| 192 + 32 T | 24 R | Recipients: UID (8 bytes), then the wrapped CEK (16 bytes) |
| ... | | Zeros |
| 4000 | 32 | The plaintext image's hash: `BLAKE2s(image, digest_size=32, person="LFimage")` |
| 4032 | 64 | The release key's Ed25519 signature over bytes 0-4031 |

The signature is plain Ed25519 over the 4032 bytes (Ed25519 hashes its
message with SHA-512 itself), so any Ed25519 tool can check it.

Space: 3808 bytes are shared by table hashes and recipients. For example,
one table block leaves room for 157 boards. More boards
would need recipient blocks of their own, with their hash in block 0; not
in version 1.

### Wrapping the CEK for one board

On the PC, for each board in the list:

```
shared  = X25519(ephemeral private key, board public key)
KEK     = BLAKE2s(ephemeral public || board public, digest_size=16,
                  key=shared, salt=revision (4 bytes) || product ID (4 bytes),
                  person="LFwrap")
wrapped = CEK XOR KEK
```

On the board, `shared` is `X25519(board private key, ephemeral public
key)`, the same value. Since the ephemeral key is new for each release,
each board's KEK is used once, so XOR is enough. The board knows it got
the right CEK when the CEK check (offset 72) matches; a wrong entry, a
wrong board or a damaged file then fails cleanly instead of decrypting
garbage. An all-zero `shared` (a malicious ephemeral key of small order)
is refused.

## 4. On the board

### Opening a package

`sealed_mount_package` (`src/sealed.c`) checks block 0:

1. Block 0 starts with `LITEFORTHF`.
2. Check the format version, cipher, product ID, the description, and
   that C, T and R fit in the file and in block 0, and that nothing is in
   its reserved bytes.
3. Check the certificate with the root public key, then the release key's
   serial against the lowest serial, then the signature over block 0 with
   the release key.
4. Check the version against the lowest version the board takes.
5. Find the board's UID among the recipients, compute `shared` and the
   KEK, unwrap the CEK and check it against the CEK check.
6. Keep the CEK and nonce in RAM, never in flash.

Then `sealed_open_block` checks each content block n as it's read: it
reads the table block that covers n (`C + n / 256`), checking its hash
against block 0 unless it's the one it read last, hashes block n with
BLAKE2s and compares it with its entry (a mismatch is a read error, -33),
and decrypts it in place with the counter for block n. The caller checks
the decrypted image against the image hash in block 0.

Stage 1 does this when it installs a package (`src/update.c`). Before
that, `-2 ymodem` on the V5F does the same checks on the staged package
(`sealed_check_package`, minus the board's minimum version, which is
stage 1's), so a package the board can't use is refused at the terminal.

### On the CH32H417

- **The board's record** is the 8 KB flash sector at `0x08070000`, after
  LiteForth's flash pages (`V5F/User/board.c`): the X25519 private key and
  the two minimums, with a BLAKE2s check value. Erased flash reads
  `0xE339E339` on this chip, so only a record with the right magic and
  check counts.
- **`enroll ( -- )`** makes the key from the RNG (64 words, each checked
  for data ready, seed and clock errors and repeats, hashed together with
  BLAKE2s), stores it, and prints the board's line for `boards.csv`. A
  board that already has a key just prints its line. The desktop's
  `enroll` prints the line from its `-K` board file.
- **The root key and product ID** come from `V5F/User/sealed_root.h`,
  which the application supplies, since LiteForth is a platform and each
  product has its own root. `sealblocks.py root-header ROOT.pub --product P
  -o sealed_root.h` writes it; git ignores it; without it the board can't
  check packages.

### What the board needs in code

| Part | Source |
|---|---|
| Opening and reading | `src/sealed.c`: `sealed_mount_package` checks block 0 and unwraps the CEK; `sealed_open_block` checks and decrypts each block as it's read; `sealed_check_package` does both for a whole package. Stage 1 (`src/update.c`) and the targets' `blocks.c` (`-2 ymodem`) call them |
| BLAKE2s | `src/blake2s.c`, from [bradleyeckert/blake2s](https://github.com/bradleyeckert/blake2s), with `b2s_init` for salt and personalization |
| X25519, Ed25519 check (with SHA-512) | `src/monocypher.c`, `src/monocypher-ed25519.c`: [Monocypher](https://monocypher.org) 4.0.2 (CC0 or BSD-2, `src/monocypher-LICENCE.md`), with one cast added for MSVC |
| AES-128 / SM4 CTR | `src/ctr.c` in software; on the CH32H417 the ECDC engine (`V5F/User/ecdc.c`, through `ctr_hw_crypt`), checked once against software |
| Protected storage for the board key and minimums | The security sector of [security-plan.md](security-plan.md) 5.1; a board file on the desktop (`lf -K`) |

Built for RV32IMAC at `-Os` and linked with unused code dropped, all of
it takes about 22 KB of code, with software AES and SM4. Its RAM is
static: 8 KB in `sealed.c` (block 0 and the last hash table block) plus
the caller's 4 KB block buffer.

Counted in an RV32IMC emulator (clang `-Os`, instructions retired):

| Work | Instructions | At 400 MHz, about |
|---|---|---|
| Mounting (two Ed25519 checks, X25519, unwrap) | 12.3 million | 31 ms |
| Reading a block: BLAKE2s of 4 KB | 0.47 million | 1.2 ms |
| Reading a block: AES-128-CTR of 4 KB, software | 1.19 million | 3 ms |
| Reading a block: SM4-CTR of 4 KB, software | 0.50 million | 1.2 ms |
| The first block under a hash table block: the table's BLAKE2s too | +0.49 million | +1.2 ms |

On the nanoCH32H417 (V5F at 400 MHz, MounRiver's GCC at `-Os`), measured
on a sealed block file in October 2026: mounting took 18 ms. Reading 40
blocks took 148 ms with software AES, 3.7 ms a block including the card
transfer, and 55 ms with the ECDC engine, 1.4 ms a block: what's left is
the BLAKE2s hash and the card. On the engine a 4 KB block takes 30 us of
AES (35 of SM4); with the copies into and out of its aligned buffer,
112 us (software: 2.4 ms AES, 0.8 ms SM4).

## 5. On the PC

One script, `scripts/admin/sealblocks.py` (needs `cryptography`; BLAKE2s is in
`hashlib`):

| Command | Does |
|---|---|
| `root-key ROOT` | Makes the root key pair, `ROOT.key` and `ROOT.pub` (on the offline machine) |
| `release-key ROOT.key SERIAL OUT --product P` | Makes a release key pair and its certificate; the root's private key signs it, so this runs on the offline machine and only the release key file leaves it |
| `board-key OUT [--uid HEX] [--insecure]` | Makes a board key pair on the PC, for testing, and prints its `boards.csv` line. A real board makes its own (6.1). `--insecure` gives the key a board built below `SECURITY_LEVEL` 2 derives from its UID ([updates.md](updates.md) section 6) |
| `package IMAGE PACKAGE --component v5f\|stage1\|app --release R --boards CSV --revision N [--hw-rev N] [--min-version N] [--cipher aes\|sm4] [--pages lfpages.bin]` | Makes a firmware update package, with the image as its content ([firmware-updates.md](firmware-updates.md) section 5) |
| `check PACKAGE --root ROOT.pub` | Checks everything a board would that needs no board key: certificate, signature, header fields, table hashes, block hashes |
| `open PACKAGE IMAGE --root ROOT.pub --board-key B [--min-revision N] [--min-serial N] [--product P]` | Checks and decrypts as a board would, refusing what it would refuse, and writes the image |

`boards.csv`: one line per board, `UID,public key,name`, in hex; `#`
lines are comments. Key files are JSON, and `.key` files hold private
keys. `unit_tests/seal` tests the script: round trips with AES and SM4,
the layout against the formulas above, and each refusal. There's no
`seal` command any more: it made sealed block files.

### The desktop lf

`lf -K board.lfb` gives the desktop what a board keeps: its UID and X25519
private key, the root public key, its product ID and the two minimums, in
a text file that `sealblocks.py lf-board` writes (format in
`src/target/desktop/board.h`):

```
python3 scripts/admin/sealblocks.py lf-board a.lfb --board-key a.key --root root.pub --product 0x4C46
./bin/lf -k card.img -K a.lfb
```

`-2 ymodem` then checks a staged package as the board would; without
`-K` it's staged but not checked. A block file or card image whose block 0
is sealed offers capacity 0 and is never written, with or without `-K`:

```
blocks: a sealed block file: no longer read (an app goes in an update package now)
blocks: a firmware update package, not a block file
```

`unit_tests/sealed` checks those refusals, `-1 ymodem` refusing both,
and `-2 ymodem` staging packages and refusing a board that isn't a
recipient, a replaced release key, another product or root.
`unit_tests/update` tests stage 1 opening packages.
`unit_tests/ctr` checks AES-128 and SM4 against their standard test
vectors and the CTR mode against the `cryptography` package.

## 6. Running it

### 6.1 Provisioning a board

1. The board makes its X25519 key pair from the RNG (with the RNG checks
   in [security-plan.md](security-plan.md) 5.4) and stores the private key.
2. `enroll` prints the UID and public key.
3. They go into `boards.csv`, through a reviewed commit: whoever can edit
   the list decides who can install new packages, so the list deserves the
   same care as code.

### 6.2 A release

`package` with the current release key, the board list and the next
version number ([firmware-updates.md](firmware-updates.md) sections 5
and 6 walk through it). It goes to a board by YMODEM: `-2 ymodem` (kwai:
`~x app.lfu -2`) writes it into the card's `lfstage.bin`, reads it back,
checks it and tells stage 1, which installs it at the next reset.

### 6.3 Replacing a release key

On the offline machine, make a release key with the next serial and its
certificate. The first package signed with it raises every board's lowest
serial as it installs, after which the old key can't sign anything those
boards will take. A leaked release key is handled the same way, as soon as
possible.

### 6.4 Adding a board to a release

A board added to the list can install the next release. To let it install
the current one too, either package it again as a new version (simplest), or
keep each release's CEK in an encrypted archive on the offline machine
and rebuild only block 0 with one more recipient. The archive holds only
old content keys, never anything a board holds, so it isn't a master key;
**Decide** whether it's worth keeping.

### 6.5 A lost or retired board

Leave it out of the next release. It keeps whatever it could already
install, but not anything newer.

## 7. Choosing AES or SM4

Both are 128-bit block ciphers that the ECDC engine runs in CTR mode, so
the format only records which one. AES-128 is the default. SM4 is
there for products that need Chinese commercial cryptography; strictly,
those would also want SM2 signatures and SM3 hashes instead of Ed25519 and
BLAKE2s, which this format doesn't do (a format version 2 could).

## 8. History

1. BLAKE2s in `src/` (`src/blake2s.c`, `unit_tests/blake2s`).
2. `sealblocks.py`, with its own round-trip tests (`unit_tests/seal`).
3. The desktop `lf` mounting and reading sealed block files (`lf -K`).
4. The CH32H417 doing the same, with the board key in flash, `enroll`,
   and CTR on the ECDC engine (2026-10-18), tested on a nanoCH32H417.
5. Firmware update packages (`F`) for stage 1.
6. App packages (component 3), then sealed block files retired
   (2026-10-21): block files are plain, and a product's Forth comes in
   an app package.

## 9. Known holes, for later

- **The V5F holds the board key** today (`V5F/User/board.c`, in machine
  mode behind the PMP), only so that `-2 ymodem` can check a package
  before stage 1 does. Stage 1 is to hold it instead, so the V5F needs
  no key and its PMP entry for the record is free.
- **Below `SECURITY_LEVEL` 2** ([updates.md](updates.md) section 6) a
  board derives its key from its UID, so anyone who knows the UID can open
  packages made for it. It's for development boards whose flash gets
  erased by downloads; a level 2 build never uses it.
- **Side channels** on the engine and in the software X25519 aren't
  considered (security-plan.md section 1 rules them out for now).
