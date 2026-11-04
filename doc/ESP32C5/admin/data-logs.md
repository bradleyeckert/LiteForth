# Encrypted data logs and the security-event log on ESP32-C5

Status: **a design, not built yet** (no board). Decisions still open are in
[section 7](#7-to-decide).

One of the main applications of the ESP32-C5 target is the same as the
CH32H417's: record CODEC audio and other data streams to the microSD card as
**encrypted, authenticated log files that only the data owner's server can read
and verify**. In addition, the cybersecurity system records **penetration
attempts and other security-relevant events into a log of the same form, using
the same cryptographic resources** — which is also the answer to the FDA
mapping's weakest control category, event detection and logging
([fda.md §5.6](fda.md)).

This document is the ESP32-C5 counterpart of the CH32H417's
[data-logs.md](../../CH32H417/admin/data-logs.md). The **on-card format, the
key-derivation structure, the record framing and the power-cut recovery are the
same** — they are the shared `src/datalog.c` / `src/logfile.c`, already built
and desktop-tested (`unit_tests/datalog`, `scripts/CH32H417/admin/lflog.py` on
the server side). So this document does not repeat the format; **read the
CH32H417 data-logs.md for it** and treat this as the delta: how that format maps
onto the ESP32-C5's cryptographic hardware, and how the security-event log
reuses it.

---

## 1. What is reused unchanged

The shared, portable core carries straight over (same source, same bytes on the
card, same server tool):

- The **file and record layout** (CH32 data-logs.md §3): a contiguous 4 GB
  exFAT file of 32 KB blocks, block 0 the header, blocks 1.. the records, each
  record framed at both ends so the end of a log is found after a power cut with
  no key.
- The **key-derivation shape** (§2): a one-off X25519 ephemeral `e,E` to the
  server's public `S`, mixed with the board's long-term key, into per-file
  `k_enc`, `nonce`, `k_hdr`. The **format is cipher-agile** — block 0 carries a
  `cipher` id — which is what lets the C5 select hardware-friendly primitives
  without changing the structure.
- The **recovery and exFAT make/close/trim** (§5, §6) via the shared
  `exfat.c` / `card.c`, over the C5's SD-over-SPI.
- The **server side** (`lflog.py`): unchanged, because the on-card bytes are
  unchanged.

The ESP32-C5 is single-core, so there is no V3F/V5F split and no shared-block
`k_auth` service: the key-derivation steps the CH32H417 splits between its two
cores all run in the one application, with the board's long-term key reached
through the C5's hardware (section 3), not a second core.

---

## 2. The C5 cryptographic hardware

From [esp32c5-notes.md](esp32c5-notes.md): AES-128/256 with DMA modes, SHA-1..512,
RSA (≤3072) and ECC (P-192/256/384), HMAC-SHA-256, a TRNG, and the RSA/ECDSA
Digital Signature (DS) peripheral, plus XTS-AES flash/PSRAM encryption. What the
chip does **not** have in hardware: BLAKE2s, Poly1305, X25519/Ed25519 — those
stay the software Monocypher/blake2s path the shared core already uses.

So "compatible with the C5 cryptographic hardware" means: the **bulk symmetric
work runs on the AES accelerator** (as the CH32H417's bulk ran on its ECDC
engine), **randomness comes from the TRNG**, hashing uses the **SHA
accelerator** where it helps, and the board's **long-term key is rooted in
eFuse/HMAC/DS** so it is never in readable memory. The per-file public-key step
(X25519) stays software — it is one operation per file, and keeping it means the
server's X25519 handshake is unchanged.

---

## 3. The mapping (the C5 delta)

### 3.1 Bulk stream cipher — on the AES accelerator

The 32 KB records are encrypted with AES on the C5's AES accelerator via
ESP-IDF's `esp_aes` (the analogue of the CH32H417's ECDC engine doing AES-CTR).
This fills the role `src/ctr.c`'s `CTR_HW` hook already defines: the C5 provides
a `ctr_hw_crypt` that hands whole aligned blocks to `esp_aes`, with the software
`ctr_crypt_block_sw` as the checked fallback — exactly the seam the CH32H417
uses, so `src/datalog.c` is unchanged above it.

### 3.2 Record authentication — the decision (section 7)

Two options, both keeping the cipher-agile header:

- **Keep cipher 1 (AES-128-CTR + Poly1305).** The AES-CTR runs on the
  accelerator (3.1); the Poly1305 one-time-key MAC stays software (Monocypher),
  as on the CH32H417. Fully portable, server unchanged, already tested. The only
  non-hardware part is the MAC.
- **Add a C5 cipher 2 (AES-GCM / AES-GMAC) on the accelerator.** Confidentiality
  *and* authentication from the one AES engine, no per-record software MAC. This
  is the "most compatible with the hardware" choice, **if** the C5 AES
  peripheral's GHASH path is exposed by IDF (`esp_aes_gcm`); to confirm against
  the TRM/IDF (section 7). The server's `lflog.py` gains a cipher-2 reader; the
  on-card framing is otherwise the same.

**Recommendation:** ship cipher 1 first (it already works and is portable, with
the AES bulk on hardware), and add cipher 2 (AES-GCM) as the C5-native profile
once the GHASH hardware path is confirmed — the header's `cipher` field makes
this additive, not a format break.

### 3.3 Key rooting — eFuse / HMAC / DS

On the CH32H417 the board's long-term key `b` lives in stage 1. The C5 has no
such core; instead the board's long-term secret is **rooted in hardware**:

- An **HMAC key burned into eFuse**, used through the **HMAC peripheral** so the
  key is never read by software, derives the board's long-term keying material
  (HMAC-SHA256, on the SHA/HMAC hardware). This is the C5-native source for what
  `b`/`k_auth` provide on the CH32H417.
- The **DS peripheral** holds the board's private asymmetric key (for any
  signing/identity use) so it is used without being readable.
- The one-off **`e`** and the file-id come from the **TRNG** (`esp_random`),
  matching `flash_rndkey`'s source ([esp32c5-notes.md](esp32c5-notes.md)).

The per-file derivation (`k_enc`, `nonce`, `k_hdr`) keeps the shared BLAKE2s KDF
so the derived file keys are identical to the portable definition and the server
is unchanged; only the *source of the board's long-term key* moves into
hardware. (A fully HMAC-SHA256 KDF is a possible later profile, section 7.)

### 3.4 The other accelerators, through hooks

The AES seam (3.1) is one of a set. The C5 has accelerators the CH32H417 does
not — **SHA-1..512, ECC (P-192/256/384), HMAC-SHA-256 and the RSA/ECDSA DS
peripheral** — and the design reaches each through a thin hook over the IDF/ROM
driver, verified once against a software reference with a software fallback,
exactly as `ctr.c`'s `CTR_HW` does for AES. The full hook table is in
[esp32c5-notes.md](esp32c5-notes.md#hardware-crypto-hooks-accelerators-the-ch32h417-lacks);
for logging, the ones that matter beyond AES are **SHA-256** (the HMAC KDF, 3.3)
and the **HMAC peripheral** (rooting the board key in eFuse).

### 3.5 Signatures where logging touches them: prefer LMS

Where the log/identity layer needs a signature *we* verify (e.g. signing a
server-key rotation or a board-identity certificate in the sealed layer), the
preference is **LMS** (hash-based, quantum-resistant; RFC 8554 / NIST SP 800-208)
over ECDSA — see
[esp32c5-notes.md](esp32c5-notes.md#signatures-prefer-lms-quantum-resistant-over-ecdsa).
LMS verification is almost all SHA-256, so it rides the SHA hook (3.4); the
device only verifies, so it carries none of LMS's signer-side statefulness. The
chip's own Secure Boot v2 stays ROM-fixed RSA/ECDSA; LMS is for the signature
layer above it.

---

## 4. The security-event log (penetration attempts)

The cybersecurity system writes security-relevant events to the card **in the
same encrypted, authenticated log format**, so:

- an attacker **cannot read** the record of their own attempts (it is encrypted
  to the server), and **cannot forge or selectively delete** entries without
  detection (it is authenticated and framed like a data log);
- the **server reads and verifies it with the same tool** (`lflog.py`), proving
  which board wrote it and that nothing was changed or reordered;
- it survives a power cut and is recoverable the same way.

**Shape.** A separate log *stream* — its own file (e.g. `lfsec.log`) with its own
`file_id` — distinguished by an **application-header type** in block 0, so the
server knows it is an event log, not audio. Its "sample blocks" are
fixed-size **event records** rather than audio frames:

| Field | Meaning |
|---|---|
| event type | boot-integrity result, secure-boot/signature refusal, OTA rollback / anti-rollback refusal, download-mode or JTAG access attempt, console anomaly / over-long input, tamper indication, flash-encryption check |
| timestamp | `lfGetTimeMicroSec`, anchored to the RTC by the header start time |
| severity | info / warning / critical |
| detail | a small fixed payload (e.g. the offending version, the refused signature's id, a counter) |

Because it is the same machinery, it costs little beyond defining the event
types and the write points, and it directly satisfies **event detection and
logging** in the FDA mapping ([fda.md §5.6](fda.md)), which is otherwise this
target's weakest control category. Events that must survive even a wiped card
(a boot-integrity counter) also go to a small eFuse/NVS counter; the card log is
the forensic detail.

---

## 5. Where it runs

- The **bulk AES** on the accelerator (3.1), fed by DMA as on the CH32H417, from
  buffers the IDF allocator provides (no PMP/Guarded-DMA layer on this
  single-core part — the isolation story is secure boot + flash encryption +
  closed debug, [fda.md §5.2](fda.md)).
- **SD-over-SPI** writes through the shared `blocks.c` / `card.c` once SD works
  on the C5 ([esp32c5-notes.md](esp32c5-notes.md)).
- **CODEC capture** (I2S/SAI into the pool) and the Forth words (`log-open`,
  `log-close`, `log-recover`, and the event-log writer) are the C5 target's to
  add, mirroring the CH32H417's list (CH32 data-logs.md §8).

---

## 6. What to build

| Part | Where | State |
|---|---|---|
| Log format, key derivation, framing, recovery | shared `src/datalog.c` / `src/logfile.c` | **done** (desktop-tested) |
| Server tool | `scripts/CH32H417/admin/lflog.py` | done (gains a cipher-2 reader if 3.2 adopts GCM) |
| C5 AES-accelerator CTR backend | a C5 `lf_ctr.c` providing `CTR_HW`/`ctr_hw_crypt` over `esp_aes` | planned |
| Board long-term key in hardware | C5 `board.c`: eFuse HMAC key via the HMAC peripheral; DS for the private key | planned |
| TRNG + SHA wiring | `esp_random`; `esp_sha` where it helps | planned |
| Forth words + CODEC (I2S) capture | C5 target | planned |
| Security-event log: event types + write points | a C5 module on `datalog.c`, written at each security decision point | planned |
| Tests | extend `unit_tests/datalog` with the C5 cipher profile and the event-log record type (desktop, keyless framing) | planned |

---

## 7. To decide

- **Record authentication (3.2):** keep cipher 1 (AES-CTR + software Poly1305,
  AES on hardware) or add cipher 2 (AES-GCM on the accelerator). Confirm whether
  IDF exposes the C5 AES peripheral's GHASH path (`esp_aes_gcm` hardware) before
  committing to cipher 2.
- **Key rooting (3.3):** HMAC-via-eFuse for the board's long-term key — confirm
  the eFuse HMAC-key scheme and whether to keep the shared BLAKE2s KDF for the
  per-file keys or move to a fully HMAC-SHA256 KDF (hardware) with a matching
  server change.
- **AES key length:** AES-128 (as cipher 1) or AES-256 on the C5.
- **Security-event log retention:** one file per boot like the data log, or a
  rolling/appended event log; and which events also need an eFuse/NVS counter
  that survives a wiped card.
- **Relationship to IDF:** whether any of this uses IDF's own NVS/secure-storage
  for the small counters, or stays entirely on the shared card format.
- **Signature scheme for our own layer:** adopt **LMS/HSS** (quantum-resistant,
  SHA-accelerated, SP 800-208) in place of Ed25519/ECDSA for the sealed-package
  and identity signatures we verify — which LMS parameter set (tree height,
  Winternitz `w`), and which vetted LMS implementation to add as a SOUP/SBOM
  component. (The chip's Secure Boot v2 remains RSA/ECDSA regardless.)

---

## 8. Traceability

The data-log need and the security-event-log need, and their system
requirements, are in the ESP32-C5 V-model: [UN-09](../quality/needs.md),
[UN-10](../quality/needs.md), [SYS-LOG-01..04](../quality/system.md), and
[ARC-LOG](../quality/architecture.md). The format items are *partial* (the
shared format is proven on the desktop, `sys:datalog`); the hardware mapping and
the security-event log are *planned* until the C5 backend and a board exist.
