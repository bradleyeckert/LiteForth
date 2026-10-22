# Encrypted data logs on the microSD card

Status: **a design, not built yet.** Decisions still open are in
[section 9](#9-to-decide).

A board records 24-bit stereo audio from CODECs on the CH32H417's I2S
ports and writes it to its microSD card as a data log, at 0.4 to
0.9 MB/s. A server reads the log later. Requirements:

- **Confidential.** Only the server can read a log, and nothing is
  negotiated with the server before the card is written.
- **Authenticated.** The server can prove which board wrote a log, and
  that nothing in it was changed or reordered.
- **Private UID.** The board's UID never appears in the clear on the card.
- **Raw writes.** Logs are written in 32 KB blocks through `blocks.h`
  (`blk_sector_write_imm`, then `blk_status`), not through C's file
  functions, so the firmware does other work while the card is busy.
- **One file, one boot.** A log file doesn't span reboots.
- **An ordinary exFAT file.** The board makes the file itself, under a
  name it's given, already 4 GB long and contiguous (section 6). Closing
  it trims it to what was written.
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

The board makes the file under the name it's given (section 6): 4 GB long
(DataLength 2^32, ValidDataLength 0), contiguous, with NoFatChain set. It
is a run of 32 KB blocks: 131072 of them, 64 sectors each, so a block
starts at sector `first + 64 n`.

**Block 0, the header.** Its first three sectors carry the following; the
rest of block 0 is zeros:

| Offset | Size | Contents |
|---|---|---|
| 0 | 8 | `LFLOG001` |
| 8 | 4 | Format version 1, cipher 1 (AES-128-CTR), flags |
| 12 | 4 | Server key ID |
| 16 | 8 | `file_id`: random, so blocks from an earlier log are told apart |
| 24 | 32 | `E` |
| 56 | 32 | Identity, encrypted by XOR with `BLAKE2s(k_id, person="LFlogidx")`: the UID (8 bytes), the RTC time at the start (8), the firmware version (4), the security level (1), reserved |
| 88 | 40 | Reserved, 0 |
| 128 | 1024 | Application header, encrypted with AES-128-CTR under `k_enc` (counters `nonce \|\| i`, i = 0 to 63, which no record uses): the CODEC settings, sample rate, channel map, and whatever else the application adds later. Unused bytes are 0 |
| 1152 | 16 | Header tag: `BLAKE2s(bytes 0-1151, key=k_hdr, size 16)` |

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
being `nonce || n * 2048 + i` for its 16-byte blocks `i` (n starts at 1,
so the application header's counters are never reused). The ECDC
engine does this. The tag is Poly1305 over bytes 0-31 and the ciphertext,
keyed by the CTR keystream at counter `n * 2048 + 2046` and `+ 2047`.
That gives a fresh one-time key per record, as ChaCha20-Poly1305 does, so
no BLAKE2s runs per record.

The frame at both ends of a block (`n` and `file_id` in its first and
last sectors) lets the board find the end of the log without any key
(section 5). The payload's own format is the application's: 32704 bytes
are 4088 stereo frames of two 32-bit slots, or 5450 frames packed to
3 bytes a sample with 4 bytes over.

## 4. Writing

- **Opening** is section 2, then block 0. The keys stay in RAM, never in
  flash.
- **Records:** see the pipeline below. Writes go strictly in order, one
  at a time, so the log on the card is always a complete prefix plus at
  most one torn block.
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

### From the CODEC to the card

```
I2S RX --DMA--> CODEC ring --encrypt (ECDC), copy--> record slot --tag--> SD write
               (pool, small)                         (pool, 32 KB each)
```

1. **I2S to RAM.** The I2S (or SAI) receivers fill a small ring in the
   memory pool by DMA, in circular mode, with half and full interrupts:
   say 2 x 8 KB, about 9 ms at 0.9 MB/s. User mode can't program DMA
   (PMP entry 2), so machine mode starts and stops these channels: an
   `ecall` service like Guarded DMA's, with the channel, the peripheral and
   the ring fixed when the log opens. The ring isn't bounced or checked
   on every transfer as the card's buffers are; the service checks once
   that it lies in the pool, and stops the channel when the log closes or
   anything aborts it. The pool is LIFO, so the ring must not be freed
   while the channel runs.
2. **Encrypt into a record slot.** On each half-ring interrupt (or
   polled), copy the new samples into the open slot's payload, packing
   them if the application wants, and run AES-CTR over them in place on
   the ECDC engine. The slots are 32-byte aligned, as the engine wants.
   Then the copy is the only extra pass over the data, at about 1% of the
   V5F.
3. **Tag and frame.** When a slot's payload is full, Poly1305 over its
   ciphertext, then the frame.
4. **Write.** `blk_sector_write_imm` from the slot, 64 sectors; `blocks.c`
   sends them as two 32-sector commands through Guarded DMA's 16 KB
   buffer. `blk_status`, polled from the same loop, says when the card is
   done and the slot is free again.

This may well be reworked once it runs: the copy in step 2 could go if
the I2S DMA wrote straight into the slots' payload areas, and the bounce
through Guarded DMA's buffer could go too, if the SDIO channel took the
slots directly.

### How much RAM the slots get

A card can stall a write for 100 ms or more, and at 0.9 MB/s each 100 ms
is 90 KB that must wait in the slots. With k slots the logger survives a
stall of about (k - 1) x 32 KB / rate: 36 ms with two slots, 213 ms with
seven, at 0.9 MB/s.

The slots and the CODEC ring come from the memory pool (`memalloc.h`),
which LiteForth shares with them: while no flash page is open, the
open-flash cache's share of the pool is free. No flash page is opened
while data is being collected, so `POOL_CAPACITY` should be as big as the
RAM allows. The V5F's DTCM today (256 KB):

| Use | Size |
|---|---|
| The pool (`POOL_CAPACITY` 24576 cells): the RAM page (16 KB) and, during logging, the slots and the ring | 96 KB |
| `flash.c`'s shadow of flash page 0 (`FLASH_SHADOW_PAGES` 1) | 64 KB |
| Guarded DMA's buffer | 16 KB |
| 4 KB buffers for packages, `ymodem`, the ECDC check and blocks (`sealed.c`, `ymodem.c`, `ecdc.c`, ...) | about 50 KB |
| Other `.data` and `.bss` | about 8 KB |
| The stack | 16 KB |
| Free | about 6 KB |

So today the pool leaves 80 KB for logging: two slots and a 16 KB ring,
which is too little. Ways to make it bigger:

- **Move the 4 KB buffers into the pool**, allocated only while a package
  or `ymodem` runs (they never run during logging): about +40 KB.
- **Put the pool, or a second region for the slots, in the SRAM above
  the V3F's fence** (0x20140000-0x20177FFF, 224 KB). The V5F can reach it
  (PMP entry 3), and stage 1 uses it only in recovery, while the V5F is
  off. Guarded DMA would have to take card buffers from it, and the VM's
  RAM page would be slower there than in DTCM, so it suits the slots
  better than the whole pool.
- **Drop the flash page 0 shadow** (64 KB) if the VM can live with
  reading page 0 from flash: to be measured.

With the first two, the slots get about 270 KB: eight slots, about
250 ms at 0.9 MB/s. Whatever the RAM, choose cards by their worst write
latency for 32 KB writes at the target rate (a video speed class such as
V30, or "high endurance" ones), and measure it.

## 5. After a power cut

`log-recover` (run at start-up, before a new log opens) walks the root
directory for files with DataLength 4 GB and ValidDataLength 0, whose
block 0 starts with `LFLOG001`. A closed log never looks like that. For
each one:

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

### Making the file

`log-open` takes a name and makes the file, the steps of
[exfat.md](../exfat.md) Part 3, "Making a file and pre-expanding it":

1. **Find space:** a run of free clusters for 4 GB in the allocation
   bitmap. With 128 KB clusters (a 64 GB card) that's 32768 clusters, 4 KB
   of bitmap. Scanning a whole card's bitmap is 64 KB at most on a 64 GB
   card, and 512 KB on a 1 TB one.
2. **Allocate:** set the run's bits in the bitmap.
3. **The entry set:** three free consecutive entries in the root
   directory (a name of up to 15 characters), all in one sector: the
   stream entry with DataLength 4 GB, ValidDataLength 0, FirstCluster,
   AllocationPossible and NoFatChain; the file entry with the RTC's
   timestamps; NameHash and SetChecksum.
4. **Block 0** (section 3), then the records.

That's reading the bitmap and writing a few sectors, nothing that touches
the 4 GB itself (ValidDataLength 0 means the PC reads zeros past what was
written, so nothing has to be cleared). It should take well under a
second, even on a large card, so files are made on the board, not
pre-made on the PC. If measuring shows otherwise, the PC can pre-make
them and `log-open` takes an existing one. A cut during making leaves at
worst bits set that no file owns, the same lost space as in closing: the
bitmap goes first, the entry set second. A root directory with no free
entries in one sector would need another cluster and the FAT; the first
version refuses with a clear message instead.

The same name twice: `log-open` refuses an existing name, so a log is
never overwritten.

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
| exFAT making, closing and recovery | `src/exfat.c` (writing the entry set and bitmap), the first file-system writes in C |
| Forth words | `log-open ( c-addr u -- ior )`, `log-close`, `log-recover`, and the application header |
| I2S capture | A machine-mode service to start and stop the I2S DMA channels into the pool (section 4), and the I2S/SAI and CODEC setup |
| More RAM for the pool | The 4 KB buffers moved into the pool; the slots in the SRAM above the V3F's fence, with Guarded DMA taking card buffers from there (section 4) |
| PC tool | `scripts/admin/lflog.py` |
| Tests | `unit_tests/datalog`: round trips with `lflog.py`, every tamper (byte, reorder, a record from another file, a forged header), power cuts at every write including during the close, and recovery on `mkexfat.py` images |

## 9. To decide

- **Where the slots live** (section 4): the pool grown in DTCM, the SRAM
  above the V3F's fence, or both; and whether the flash page 0 shadow
  stays.
- **The CODEC data:** how many CODECs, the sample rate, and 32-bit slots
  or packed 24-bit samples. These set the rate and so how many slots are
  needed.
- **The first version's flow** (section 4): with the copy into the slots
  and the bounce through Guarded DMA's buffer, or straight to the slots.
