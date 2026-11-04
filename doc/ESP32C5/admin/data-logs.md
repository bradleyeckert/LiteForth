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

### 3.2 Record authentication — a C5-native AES-GCM profile (decided)

**Decision:** the C5 uses a native **AES-256-GCM** profile (a new `cipher` id in
the header), not the portable AES-CTR + Poly1305 of cipher 1. GCM gives
confidentiality *and* authentication from the one AES engine — no per-record
software MAC — and it is **NIST-approved** (SP 800-38D), where Poly1305 is not;
that matches the standing preference for NIST-approved functions. The portable
cipher 1 (AES-CTR + Poly1305, software) stays defined for the desktop/CH32H417
software path, selected by the same cipher-agile header, so nothing is lost.

**AES-256, not 128.** Against Grover's algorithm a symmetric key loses half its
effective strength, so AES-128 drops to a ~64-bit quantum margin while AES-256
keeps ~128 bits. Since symmetric and hash-based primitives are the ones expected
to survive a quantum break (the whole reason for the LMS choice, 3.5), the log
profile spends the cheap headroom and uses AES-256.

**GCM's one hard rule — never reuse an (key, IV) pair — is met by construction.**
GCM is catastrophic under IV reuse, so the design makes reuse impossible rather
than unlikely: `k_enc` is unique per file (derived from a fresh ephemeral, 3.3),
and the 96-bit IV is `salt32 || record_sequence` — the per-file salt from the
header times the record's monotonic sequence number — so no IV repeats within a
file, and no key repeats across files. The record count per file (≤ 131071) is
far inside SP 800-38D's limits for a deterministic IV. The header block and the
application header get their own reserved sequence values, disjoint from the
records'.

**Pending:** confirm the C5 AES peripheral's GHASH path is exposed by IDF
(`esp_aes_gcm` hardware) before committing the hardware hook; the profile and
format are fixed regardless, and the server's `lflog.py` gains the matching
GCM reader.

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

**Decision — the C5 profile derives keys with HMAC-SHA256, not BLAKE2s.** Both
KDF steps use HMAC-SHA256 (NIST-approved; FIPS 198-1 over SHA-256, FIPS 180-4),
on the SHA/HMAC hardware:

- **Board long-term key, from the eFuse key** — NIST **SP 800-108** KBKDF in
  counter mode, with HMAC-SHA256 as the PRF and the eFuse HMAC key as the
  key-derivation key, computed by the HMAC peripheral so the root key is never
  read. This is the C5-native source for what `b`/`k_auth` give on the CH32H417.
- **Per-file keys, from the X25519 shared secret** — NIST **SP 800-56C Rev. 2**
  two-step KDF (**HKDF-SHA256**: extract-then-expand). X25519 is itself
  NIST-approved (SP 800-186), so the shared secret comes from an approved scheme;
  HKDF-Extract concentrates it into a uniform pseudorandom key, and HKDF-Expand
  produces `k_enc` (32 bytes, AES-256), the GCM `salt`, and `k_hdr`, each bound
  to a distinct `info` label.

This replaces the shared BLAKE2s KDF for the C5 profile, so the server's
`lflog.py` gains a matching C5 reader; the portable BLAKE2s profile stays for
the software path. Why these KDFs are secure is spelled out in section 3.6.

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

### 3.6 Why the KDFs are secure

A KDF turns one secret into several keys that are each as strong as, and
computationally independent of, the source and of one another. Both C5 KDFs rest
on **HMAC-SHA256**, which is a pseudorandom function (PRF): without the key its
output is indistinguishable from random, and SHA-256 has no known
collision/preimage weakness that would undermine it. That PRF property is what
makes each derivation below secure, and each is a named NIST construction so the
reasoning is the standard's, not ours.

- **Per-file keys — HKDF-SHA256 (SP 800-56C Rev. 2), from the X25519 secret.**
  *Extract* (`PRK = HMAC-SHA256(salt, Z)` over the X25519 shared secret `Z`)
  distils a non-uniform Diffie-Hellman secret — which has high entropy but is not
  uniformly random — into a uniformly pseudorandom key `PRK`, using HMAC as a
  randomness extractor. *Expand* then runs HMAC-SHA256 in a counter chain to
  produce `k_enc`, the GCM `salt` and `k_hdr`. **Why secure:** each output block
  is a PRF evaluation under `PRK` on a distinct input that includes a distinct
  `info` label, so the three keys are pseudorandom and independent (domain
  separation), and recovering any of them, or `Z`, would require breaking the
  PRF. It is the NIST-approved way to derive keys from a DH shared secret, and
  the shared secret itself is from an approved scheme (X25519, SP 800-186).
- **Board long-term key — SP 800-108 counter-mode KBKDF, from the eFuse key.**
  The key-derivation key is a uniformly random key burned into eFuse (from a good
  RNG, below) and used only inside the HMAC peripheral, so it never appears in
  readable memory. Each derived value is `HMAC-SHA256(KDK, counter || label ||
  context)`. **Why secure:** with a uniform KDK the extract step is unnecessary,
  so SP 800-108 applies the PRF directly; distinct labels give independent
  outputs, and an attacker who never sees the KDK (hardware-held) cannot compute
  or distinguish them. Compromising a derived key does not reveal the KDK or the
  other derivations.
- **Randomness — a SP 800-90A DRBG.** The ephemeral `e`, the per-file material
  and the eFuse key's entropy come from the ESP RNG, a hardware-seeded DRBG
  (`esp_random`, the TRNG hook, 3.4). A KDF's guarantees are only as good as the
  entropy under them, so the inputs come from an approved DRBG, not a bare
  counter or timer.

Taken together the C5 profile is NIST-approved end to end — AES-256-GCM
(SP 800-38D), HKDF-SHA256 (SP 800-56C), SP 800-108 KBKDF, HMAC-SHA256
(FIPS 198-1) over SHA-256 (FIPS 180-4), X25519 (SP 800-186), the DRBG
(SP 800-90A), and LMS (SP 800-208) for signatures — and it leans on exactly the
symmetric/hash-based primitives that a quantum break would leave standing.

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

## 7. Decided and still open

**Decided (this iteration):** the C5 cryptographic profile is NIST-approved end
to end — **AES-256-GCM** AEAD (3.2), **HKDF-SHA256** per-file and **SP 800-108
HMAC-SHA256** key rooting (3.3, 3.6), **X25519** key agreement, a SP 800-90A
DRBG, and **LMS/HSS** for the signatures we verify (3.5). The portable
AES-CTR + Poly1305 + BLAKE2s profile stays for the software/CH32H417 path.

**Still open:**

- **Confirm the GHASH hardware path:** that IDF exposes the C5 AES peripheral's
  AES-GCM (`esp_aes_gcm` on hardware) before wiring the hook; the AES-256-GCM
  *profile and on-card format* are fixed regardless (a software GCM would still
  produce identical bytes).
- **The eFuse HMAC-key scheme:** which eFuse block/key purpose holds the
  key-derivation key, and the provisioning burn (confirm against the TRM).
- **LMS parameters:** the LMS/HSS parameter set (tree height, Winternitz `w`)
  and which vetted LMS implementation to add as a SOUP/SBOM component.
- **Security-event log retention:** one file per boot like the data log, or a
  rolling/appended event log; and which events also need an eFuse/NVS counter
  that survives a wiped card.
- **Relationship to IDF:** whether the small counters use IDF's NVS/secure
  storage or stay entirely on the shared card format.

---

## 8. Traceability

The data-log need and the security-event-log need, and their system
requirements, are in the ESP32-C5 V-model: [UN-09](../quality/needs.md),
[UN-10](../quality/needs.md), [SYS-LOG-01..04](../quality/system.md), and
[ARC-LOG](../quality/architecture.md). The format items are *partial* (the
shared format is proven on the desktop, `sys:datalog`); the hardware mapping and
the security-event log are *planned* until the C5 backend and a board exist.
