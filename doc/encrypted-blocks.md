# Sealed block files: encrypted, signed lfblocks.fb4

Plan, 2026-10-17. Steps 1 to 3 of section 8 are done: BLAKE2s in `src/`,
`scripts/sealblocks.py`, and the desktop `lf` reading sealed files
(`lf -K`). The CH32H417 can't read them yet.

A sealed block file is an `lfblocks.fb4` whose content blocks are encrypted
and signed, so the same file can go on every unit's card as a firmware
update. Only boards it was made for can read it, and none of them will use
it unless the vendor signed it. Development keeps using plain block files.

Goals:

- **No master key.** Nothing secret is shared between boards or kept by
  every board. A board holds its own private key and the vendor's public
  root key; the vendor holds the private root and release keys and a list
  of the boards' public keys.
- **Random access.** Any block can be checked and decrypted by itself, in
  hardware (AES or SM4 in CTR mode on the ECDC engine), as `block` reads it.
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
| Release key | Ed25519, with a serial number | The build PC | Sign each sealed file's block 0 |
| Content key (CEK) | 128-bit, random | Nowhere, after the file is built (or an offline archive, see 6.4) | Encrypt one release's content blocks. Never used for two releases |
| Release ephemeral key | X25519, random | Nowhere: thrown away once the file is built | Wrap the CEK for each board |
| Board list | UID + X25519 public key per board | The build PC, under version control | Choose who can read a release |

### Each board's keys

| Item | Kept | Notes |
|---|---|---|
| Board key | X25519 private key, 32 bytes, in protected flash | Made on the board from its RNG when it's provisioned. Never leaves it |
| UID | `ESIG_UNIID1` and `UNIID2`, 8 bytes | Finds the board's entry in a file. Not secret. (`UNIID3` reads as erased flash, see [ch32h417-notes.md](ch32h417-notes.md)) |
| Root public key | Ed25519, 32 bytes, in the firmware | Changes only with a new firmware image |
| Lowest revision | 4 bytes in protected flash | Anti-rollback: older files are refused |
| Lowest release key serial | 4 bytes in protected flash | A replaced release key can't sign again |

A file only ever carries public keys and wrapped CEKs. The release
public key in block 0 proves nothing by itself, since anyone can sign with
their own key and include it. It counts only because the root key signed
it (the certificate in section 3).

## 2. The file

```
block 0          header: plain text, signed
blocks 1..C-1    content: encrypted (source, fonts, flash images)
blocks C..C+T-1  hash tables: plain, each checked against block 0
blocks C+T...    nothing yet (device data later, section 9)
```

Block numbers of the content are the same as in the plain file it was
made from, so `1 load` works the same way.

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
2^56 blocks. **Verify** how the ECDC engine increments its counter (all
128 bits, or only the low 32; either works while n * 256 + 255 fits in 32
bits, so up to 16 million blocks), and its byte order.

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
tables of a few tens of MB (section 3).

## 3. Block 0

All numbers little-endian.

| Offset | Size | Contents |
|---|---|---|
| 0 | 9 | `LITEFORTH`, as in every block file |
| 9 | 1 | `S`: a sealed file. (Plain files have `B` here, from `LITEFORTHBLK`) |
| 10 | 1 | Format version, 1 |
| 11 | 1 | Cipher: 1 = AES-128-CTR, 2 = SM4-CTR |
| 12 | 4 | Flags, 0 |
| 16 | 4 | Product ID |
| 20 | 4 | Revision: must be at least the board's lowest revision |
| 24 | 4 | C: the first block after the content (content is blocks 1 to C-1) |
| 28 | 2 | T: hash table blocks, at blocks C to C+T-1 |
| 30 | 2 | R: recipient entries |
| 32 | 8 | CTR nonce, random |
| 40 | 32 | Release ephemeral X25519 public key |
| 72 | 8 | CEK check: `BLAKE2s(nonce, digest_size=8, key=CEK, person="LFcheck")` |
| 80 | 4 | Release key serial |
| 84 | 32 | Release Ed25519 public key |
| 116 | 64 | Certificate: the root key's Ed25519 signature over bytes 16-19 and 80-115 (product ID, serial, release public key) |
| 180 | 12 | Reserved, 0 |
| 192 | 32 T | Table hashes, one per hash table block |
| 192 + 32 T | 24 R | Recipients: UID (8 bytes), then the wrapped CEK (16 bytes) |
| ... | | Zeros |
| 4032 | 64 | The release key's Ed25519 signature over bytes 0-4031 |

The signature is plain Ed25519 over the 4032 bytes (Ed25519 hashes its
message with SHA-512 itself), so any Ed25519 tool can check it.

Space: 3840 bytes are shared by table hashes and recipients. For example,
8 table blocks (8 MB of content) leave room for 149 boards. More boards
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

### Mounting (at startup, after `card_find_blocks`)

1. Block 0 starts with `LITEFORTHS`: it's sealed. (`LITEFORTHB` or
   anything else: a plain file, as today.)
2. Check the format version, cipher, product ID and that C, T and R fit
   in the file and in block 0.
3. Check the certificate with the root public key, then the release key's
   serial against the lowest serial, then the signature over block 0 with
   the release key.
4. Check the revision against the lowest revision.
5. Find the board's UID among the recipients, compute `shared` and the
   KEK, unwrap the CEK and check it against the CEK check.
6. Keep the CEK and nonce in RAM (never in flash: the board can unwrap
   again at every startup). The blocks can now be read.
7. If the revision or key serial is higher than the stored minimum,
   raise the minimum (**Decide** when: at once, or after the app confirms
   it runs, as in [security-plan.md](security-plan.md) 5.5).

A failure at any step leaves the file unmounted with a message on the
debug port, like the messages in [sdcard.md](sdcard.md): capacity 0, so
nothing from it runs.

### Reading a block

`block n` for 1 <= n < C:

1. Read the table block that covers n (`C + n / 256`) through the block
   buffers, checking its hash against block 0 the first time it's read
   after it was evicted.
2. Read block n's sectors by DMA as today.
3. Hash it with BLAKE2s and compare with its entry. A mismatch is a read
   error (-33, as for a bad card read).
4. Decrypt it in place on the ECDC engine, with the counter for block n.

Blocks C and up can't be read as content; block 0 can be read as usual.
Nothing in a sealed file can be written (-96), so `update` and the editor
work only with plain files.

Cost per block: a BLAKE2s hash of 4 KB in software (well under a
millisecond at 400 MHz) and a 4 KB CTR pass on the engine. The table
block costs one extra read when it isn't already in a buffer; with only
two block buffers (`SYSTEM_BLOCKS`), a third buffer for tables may be
worth having.

### What the board needs in code

| Part | Source |
|---|---|
| Mounting and reading | `src/sealed.c`: `sealed_mount` checks block 0 and unwraps the CEK; `sealed_open_block` checks and decrypts each block as it's read. The targets' `blocks.c` call them |
| BLAKE2s | `src/blake2s.c`, from [bradleyeckert/blake2s](https://github.com/bradleyeckert/blake2s), with `b2s_init` for salt and personalization |
| X25519, Ed25519 check (with SHA-512) | `src/monocypher.c`, `src/monocypher-ed25519.c`: [Monocypher](https://monocypher.org) 4.0.2 (CC0 or BSD-2, `src/monocypher-LICENCE.md`), with one cast added for MSVC |
| AES-128 / SM4 CTR | `src/ctr.c` in software; the ECDC engine on the CH32H417 (step 4) |
| Protected storage for the board key and minimums | The security sector of [security-plan.md](security-plan.md) 5.1; a board file on the desktop (`lf -K`) |

Built for RV32IMAC at `-Os` and linked with unused code dropped, all of
it takes about 22 KB of code, with software AES and SM4. Its RAM is
static: 8 KB in `sealed.c` (block 0 and the last hash table block) plus a
4 KB buffer in `blocks.c` at mount. X25519 and the two signature checks
run once per mount: to measure on the board.

## 5. On the PC

One script, `scripts/sealblocks.py` (needs `cryptography`; BLAKE2s is in
`hashlib`):

| Command | Does |
|---|---|
| `root-key ROOT` | Makes the root key pair, `ROOT.key` and `ROOT.pub` (on the offline machine) |
| `release-key ROOT.key SERIAL OUT --product P` | Makes a release key pair and its certificate; the root's private key signs it, so this runs on the offline machine and only the release key file leaves it |
| `board-key OUT [--uid HEX]` | Makes a board key pair on the PC, for testing, and prints its `boards.csv` line. A real board makes its own (6.1) |
| `seal PLAIN SEALED --release R --boards boards.csv --revision N [--cipher aes\|sm4]` | Builds a sealed file from a plain one |
| `check SEALED --root ROOT.pub` | Checks everything a board would that needs no board key: certificate, signature, header fields, table hashes, block hashes |
| `open SEALED PLAIN --root ROOT.pub --board-key B [--min-revision N] [--min-serial N] [--product P]` | Checks and decrypts as a board would, refusing what it would refuse, and writes a plain file (with a new block 0) |

`boards.csv`: one line per board, `UID,public key,name`, in hex; `#`
lines are comments. Key files are JSON, and `.key` files hold private
keys. `unit_tests/seal` tests the script: round trips with AES and SM4,
the layout against the formulas above, and each refusal.

### The desktop lf

`lf -K board.lfb` gives the desktop what a board keeps: its UID and X25519
private key, the root public key, its product ID and the two minimums, in
a text file that `sealblocks.py lf-board` writes (format in
`src/target/desktop/board.h`):

```
python3 scripts/sealblocks.py lf-board a.lfb --board-key a.key --root root.pub --product 0x4C46
./bin/lf -k sealed.fb4 -K a.lfb
```

A file whose block 0 starts with `LITEFORTHS` is then mounted as a board
would mount it, read-only, and stderr says how it went:

```
blocks: sealed file, revision 5, release key 1, AES-128, 121 blocks
blocks: sealed file refused: this board isn't one of its recipients
```

A refused file, or one opened without `-K`, has capacity 0. A block that
fails its hash reads as zeros with error -33, and any write gives -96.
The same works for a sealed `lfblocks.fb4` inside a card image.

`unit_tests/sealed` seals a copy of `bin/lfblocks.fb4` for a made-up
board, with AES and SM4, as a file and in a card image, and checks that
`lf -K` loads `core.f` from it and reads every block as from the plain
file. It also checks each refusal: no board file, a board that isn't a
recipient, newer minimums, another product or root, a changed header,
version or length, a damaged block or hash table, and writes.
`unit_tests/ctr` checks AES-128 and SM4 against their standard test
vectors and the CTR mode against the `cryptography` package.

## 6. Running it

### 6.1 Provisioning a board

1. The board makes its X25519 key pair from the RNG (with the RNG checks
   in [security-plan.md](security-plan.md) 5.4) and stores the private key.
2. A terminal word (`enroll`, say) prints the UID and public key.
3. They go into `boards.csv`, through a reviewed commit: whoever can edit
   the list decides who can read new releases, so the list deserves the
   same care as code.

### 6.2 A release

`seal` with the current release key, the board list and the next
revision number. The file goes onto cards, or to a board by YMODEM
(`~x`).

### 6.3 Replacing a release key

On the offline machine, make a release key with the next serial and its
certificate. The first file signed with it raises every board's lowest
serial as it mounts, after which the old key can't sign anything those
boards will take. A leaked release key is handled the same way, as soon as
possible.

### 6.4 Adding a board to a release

A board added to the list can read the next release. To let it read the
current one too, either seal it again as a new revision (simplest), or
keep each release's CEK in an encrypted archive on the offline machine
and rebuild only block 0 with one more recipient. The archive holds only
old content keys, never anything a board holds, so it isn't a master key;
**Decide** whether it's worth keeping.

### 6.5 A lost or retired board

Leave it out of the next release. It keeps whatever it could already
read, but not anything newer.

## 7. Choosing AES or SM4

Both are 128-bit block ciphers that the ECDC engine runs in CTR mode, so
the file format only records which one. AES-128 is the default. SM4 is
there for products that need Chinese commercial cryptography; strictly,
those would also want SM2 signatures and SM3 hashes instead of Ed25519 and
BLAKE2s, which this format doesn't do (a format version 2 could).

## 8. Steps

1. Add BLAKE2s to `src/` (with `b2s_init`) and its tests. **Done**
   (`src/blake2s.c`, `unit_tests/blake2s`).
2. `sealblocks.py`, with its own round-trip tests (`unit_tests/seal`).
   **Done.**
3. Desktop `lf`: mount and read sealed files (software AES), with the
   failure tests in section 5. **Done** (`src/sealed.c`, `src/ctr.c`,
   `lf -K`, `unit_tests/ctr`, `unit_tests/sealed`).
4. CH32H417: ECDC CTR decryption after the DMA read, X25519 and Ed25519,
   the board key in protected flash, `enroll`.
5. Anti-rollback minimums, and key replacement.

## 9. Known holes, for later

- **Code on the board can read the board key and the CEK.** They're in
  flash and RAM that any C code can reach; read protection (RDP) only
  keeps the debugger out. That's the point of the PMP and stage 0 in
  [security-plan.md](security-plan.md) 5.6.
- **Data the board writes** isn't protected. A plain file can still hold
  it, or later a device-data range in the sealed file, with a MAC per
  block keyed by a board-only key (`b2s_init` with the block number and a
  write counter in the salt), and encryption under a separate key with a
  counter that changes on every write: reusing a CTR counter for a
  rewritten block would leak both versions.
- **Rollback rests on the stored minimums.** An old copy of one block
  fails its hash, but a whole older file is stopped only by the lowest
  revision in flash, which code on the board could lower.
- **Side channels** on the engine and in the software X25519 aren't
  considered (security-plan.md section 1 rules them out for now).
