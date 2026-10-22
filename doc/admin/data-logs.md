# Encrypted data logs on the microSD card

Status: **a design, not built yet.** Decisions still open are in
[section 9](#9-to-decide).

A board writes a data log to its microSD card at 0.4 to 0.9 MB/s. A
server reads it later. Requirements:

- **Confidential.** Only the server can read a log, and nothing is
  negotiated with the server before the card is written.
- **Authenticated.** The server can prove which board wrote a log, and
  that nothing in it was changed or reordered.
- **Private UID.** The board's UID never appears in the clear on the card.
- **Raw writes.** Logs are written in 32 KB blocks through `blocks.h`
  (`blk_sector_write_imm`, then `blk_status`), not through C's file
  functions, so the firmware does other work while the card is busy.
- **One file, one boot.** A log file doesn't span reboots.
- **An ordinary exFAT file.** The file is made on the PC, already
  4 GB long and contiguous. Closing it trims it to what was written.
- **Recoverable after a power cut.** If the power goes before the file
  is closed, the board can step through the file, find where the log
  ends, and close it there.

## 1. Keys

| Key | Where | Lifetime |
|---|---|---|
| Server key pair `s`, `S` (X25519) | `s` only on the server; `S` built into the V5F image (`sealed_root.h`, with a key ID) | Years; rotated by an app package carrying a new `S` |
| Board key pair `b`, `B` (X25519) | `b` only in stage 1 (the board key from `enroll`); `B` in the server's `boards.csv` | The board's life (re-enrolling changes it) |
| Ephemeral pair `e`, `E` | `e` in V5F RAM until the keys are derived, then wiped; `E` in the file's header | One file |
| File keys (`k_enc`, the nonce, `k_hdr`) | V5F RAM only, while the file is open | One file |

The board holds no long-term secret for logging: `b` is already stage 1's,
and `S` is public. At level 2, `S` comes in the signed image, so nobody
without the release key can swap in their own server key.

The board key `b` is reused rather than adding a second key, so the
server needs nothing beyond what `boards.csv` already holds. Each use is
separated by its BLAKE2s personalization. The X25519 shared secrets are
only ever used through those derivations, never output.

At levels 0 and 1, `b` comes from the UID and isn't secret. The server
must not trust which board wrote a development board's logs.

## 2. Deriving the keys

When it opens a file, the V5F:

1. Draws `e` from the RNG and computes `E`.
2. Computes `es = X25519(e, S)`, then
   `k_id = BLAKE2s(E || S, key=es, person="LFlogid")`.
3. Asks stage 1 for `k_auth`, through the shared block, as `enroll` asks
   for the public key. Stage 1 computes
   `k_auth = BLAKE2s(E || S, key=X25519(b, S), person="LFlogbrd")` and
   refuses an all-zero shared secret.
4. Computes `master = BLAKE2s(es || E || S, key=k_auth, person="LFlogkey")`.
5. Derives `k_enc` (16 bytes, AES-128), `nonce` (8 bytes) and `k_hdr`
   (32 bytes) from `master` with BLAKE2s, personalizations `LFlogenc`,
   `LFlognon` and `LFloghdr`.
6. Wipes `e`, `es`, `k_auth` and `master`.

Stage 1 takes `S` from the V5F rather than holding its own copy. For any
`S` someone else chooses, they know its private key and can already
compute `X25519(b, S) = X25519(s, B)` from the public `B`. So the service
leaks nothing, and `S` stays rotatable by app package without touching
stage 1, which can't be updated yet.

The server reverses it: `es = X25519(s, E)`, then `k_id`. It decrypts the
identity in the header (section 3) to get the UID, looks up `B` in
`boards.csv`, and computes `X25519(s, B)` and `k_auth`, then `master`
and the file keys. The header's tag then shows that the board holding
`b` wrote the file: without `b` (or `s`) nobody can make `k_auth`.

What each compromise gives:

| Compromised | Gives |
|---|---|
| `s` (server) | Every log written to `S`. Keep `s` offline or in an HSM, backed up: losing it loses every log |
| A board's stage 1 (`b`) | Forged logs in that board's name. Under PMP entry 0 and read protection |
| The V5F, while it runs | What it logs from then on, and forged logs while the attacker controls it, since it can ask stage 1 for `k_auth`. Not files written before it was taken over: their `e` and keys are gone |
| The card | Nothing readable. Changes, reordering and records added from another file all fail their tags; only cutting the tail off goes undetected (section 5) |

## 3. The file

The file is `lflog.bin` (or several names: section 9), made on the PC:
4 GB long (DataLength 2^32, ValidDataLength 0), contiguous, with
NoFatChain set. `card_find_file` finds it as it finds `lfstage.bin`. It
is a run of 32 KB blocks: 131072 of them, 64 sectors each, so a block
starts at sector `first + 64 n`.

**Block 0, the header.** Its first sector carries the following; the
rest of block 0 is zeros:

| Offset | Size | Contents |
|---|---|---|
| 0 | 8 | `LFLOG001` |
| 8 | 4 | Format version 1, cipher 1 (AES-128-CTR), flags |
| 12 | 4 | Server key ID |
| 16 | 8 | `file_id`: random, so blocks from an earlier log are told apart |
| 24 | 32 | `E` |
| 56 | 32 | Identity, encrypted by XOR with `BLAKE2s(k_id, person="LFlogidx")`: the UID (8 bytes), the RTC time at the start (8), the firmware version (4), the security level (1), reserved |
| 88 | 16 | Header tag: `BLAKE2s(bytes 0-87, key=k_hdr, size 16)` |

**Blocks 1 on, the records.** Each 32 KB block is one record:

| Offset | Size | Contents |
|---|---|---|
| 0 | 4 | `LFLB` |
| 4 | 8 | `file_id` |
| 12 | 4 | Sequence number `n` (the block's index) |
| 16 | 4 | Payload bytes used, 0 to 32704 |
| 20 | 4 | Flags: bit 0, the last record (a clean close) |
| 24 | 8 | Reserved, 0 |
| 32 | 32704 | Payload, encrypted |
| 32736 | 16 | Tag |
| 32752 | 4 | `n` again |
| 32756 | 8 | `file_id` again |
| 32764 | 4 | `LFLE` |

The payload is encrypted with AES-128-CTR under `k_enc`, the counter
being `nonce || n * 2048 + i` for its 16-byte blocks `i`. The ECDC
engine does this. The tag is Poly1305 over bytes 0-31 and the ciphertext,
keyed by the CTR keystream at counter `n * 2048 + 2046` and `+ 2047`.
That gives a fresh one-time key per record, as ChaCha20-Poly1305 does, so
no BLAKE2s runs per record.

The frame at both ends of a block (`n` and `file_id` in its first and
last sectors) lets the board find the end of the log without any key
(section 5). The payload's own format, its records and timestamps, is the
application's.

## 4. Writing

- **Opening** is section 2, then block 0. The keys stay in RAM, never in
  flash.
- **Records:** fill a 32 KB buffer, encrypt it, tag it, and start the
  write with `blk_sector_write_imm` (64 sectors, which `blocks.c` sends as
  two 32-sector commands through Guarded DMA). Fill the next buffer
  meanwhile; `blk_status` says when the card is done. Writes go strictly
  in order, one at a time, so the log on the card is always a complete
  prefix plus at most one torn block.
- **Closing:** the last record has flag bit 0 set. Then the file system
  is updated as in section 6.
- **Fence:** the logger writes only inside its file's extent and, when
  closing, the directory sector and the bitmap sectors. `blk_write_fence`
  allows the whole volume, so the logger checks the tighter range itself.

**Speed.** At 0.9 MB/s that's about 28 records a second. AES-CTR runs on
the ECDC engine. Poly1305 ran about 2.8 times faster than BLAKE2s on the
PC. Scaled from BLAKE2s's 1.2 ms per 4 KB at 400 MHz
([encrypted-blocks.md](encrypted-blocks.md)), that's an estimated
under 10% of the V5F for the tags, still to be measured on the board.
With BLAKE2s instead, about 26%.

**Buffers: the real limit.** A card can stall a write for 100 ms or
more, and at 0.9 MB/s each 100 ms is 90 KB that must wait in RAM.
- **Today's RAM:** with the flash pages closed, the memory pool has about
  80 KB beyond the RAM page: two 32 KB buffers. While one is written, the
  other fills in 36 ms at 0.9 MB/s, so a longer stall loses data.
- **More RAM:** the SRAM above the V3F's fence (0x20140000-0x20177FFF,
  224 KB) is unused while the V5F runs; stage 1 uses it only in recovery,
  when the V5F is off. Guarded DMA would have to allow it as a buffer
  region (today it takes only DTCM's `.data` and `.bss`). That gives about
  seven buffers, roughly 200 ms at 0.9 MB/s.
- **The card:** measure its worst write latency for 32 KB writes at the
  target rate, and choose cards by it (a video speed class such as V30, or
  "high endurance" ones).

## 5. After a power cut

At the next start, the board finds `lflog.bin` with ValidDataLength still
0 and DataLength 4 GB, which is never a closed file, and recovers it
before opening a new one:

1. Read block 0's first sector; check `LFLOG001`; take `file_id`.
2. Block n is written if its first sector holds `LFLB`, `file_id` and `n`,
   and its last sector holds `n`, `file_id` and `LFLE`. Since blocks are
   written in order, binary search finds the last written block in about
   17 steps of two sector reads. Blocks left over from an earlier log have
   another `file_id`, so they never count as written, and a torn last
   block fails its frame.
3. Close the file after that block (section 6). No key is needed, and
   none is available: the file keys died with the power.

The server sees the file ends without the last-record flag and reports
"ended without a close". That can't be told apart from someone cutting
the tail off the file, so the server should treat an unclosed log's end
as unproven, but every record before it is still checked by its tag.

A block that is torn in its middle sectors (both frame sectors written,
one in between not) passes the board's scan but fails its tag at the
server, which then reports where the damage is.

## 6. Closing the file in exFAT

The file is contiguous with NoFatChain set, so the FAT is never touched,
only the directory entry and the allocation bitmap. This follows
[exfat.md](../exfat.md) Part 3, "Closing (trimming)". The order keeps a
power cut during the close safe:

1. **The directory entry set:** DataLength and ValidDataLength set to the
   bytes used (`(last + 1) * 32 KB`), and SetChecksum recomputed. One
   sector, provided the entry set doesn't cross a sector boundary; the
   board checks that when it opens the file and refuses a file whose
   entry set does.
2. **The bitmap:** clear the bits of the clusters after the last one
   used.

A cut between the two leaves clusters marked used that no file owns:
lost space, which `chkdsk` reclaims, never two files on one cluster. The
reverse order could give a cluster to the next file while this one still
claims it.

The next log needs a 4 GB file again. Either the board re-expands it
(sets the bits back, if all those clusters are still free, then the entry
set), or a card carries one log and goes back to the PC. That's a
decision (section 9).

## 7. On the server

A PC tool (`scripts/admin/lflog.py`, with `cryptography`, like
`sealblocks.py`):

- `open LOG --server-key s.key --boards boards.csv`: checks the header,
  names the board, then checks and decrypts every record, writing the
  payload out. It reports gaps, bad tags, and whether the log was closed.
- `server-key`: makes `s`/`S` and the key ID, and adds `S` to
  `sealed_root.h` (`root-header`).

The server keeps every `s` it has ever used, by key ID, for old logs.

## 8. What to build

| Part | Where |
|---|---|
| Log format and key derivation, shared by board and tests | `src/datalog.c/.h`, plain C, through callbacks like `update.c` |
| `k_auth` service | Stage 1 (`stage1_poll`), a request in the shared block like `enroll` |
| exFAT close and re-expand | `src/exfat.c` (writing the entry set and bitmap), the first file-system writes in C |
| Forth words | `log-open`, `log-write ( addr u -- )`, `log-close`, `log-recover` |
| Guarded DMA buffer region | `gdma.c`, if the SRAM above the V3F's fence is used (section 4) |
| PC tool | `scripts/admin/lflog.py` |
| Tests | `unit_tests/datalog`: round trips with `lflog.py`, every tamper (byte, reorder, a record from another file, a forged header), power cuts at every write including during the close, and recovery on `mkexfat.py` images |

## 9. To decide

- **One log per card**, or the board re-expands the file for the next
  one (section 6). Re-expanding means the board writes the bitmap at open
  as well as at close.
- **File names:** one `lflog.bin`, or a set (`lflog000.bin`, ...)
  pre-made on the PC so a card holds several logs.
- **Buffer RAM:** two buffers in the pool, or Guarded DMA extended to the
  SRAM above the V3F's fence (section 4). This decides how long a card
  stall the logger survives.
- **What goes in the encrypted identity:** UID and start time at least;
  anything else the server needs before reading the records.
