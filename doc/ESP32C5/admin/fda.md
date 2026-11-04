# Cybersecurity for a medical device: LiteForth on ESP32-C5

This document maps the ESP32-C5 target's security design and evidence to the
U.S. FDA guidance **"Cybersecurity in Medical Devices: Quality Management
System Considerations and Content of Premarket Submissions"** (docket
FDA-2021-D-1158, the February 2026 edition; "Contains Nonbinding
Recommendations"). It is the ESP32-C5 counterpart of the CH32H417's
[fda.md](../../CH32H417/admin/fda.md), and it organizes what this target has
so far — the product security plan ([security-plan.md](security-plan.md)), the
SOUP controls ([soup.md](soup.md)), the V-model
([doc/ESP32C5/quality/](../quality/)) and the build-time security gate
([seccheck.py](../../../scripts/ESP32C5/seccheck.py)) — against that guidance's
structure, and calls out where the medical-device framing should **tighten or
loosen** the target's requirements. It is not a filed submission and not
regulatory advice; a regulatory professional owns any actual submission.

The FDA guidance is nonbinding and scales its expectations with a device's
cybersecurity risk. Two things make this target's mapping read differently
from the CH32H417's:

1. **The board isn't here yet.** The ESP32-C5 port builds under ESP-IDF but
   has not run on hardware ([board-tests.md](board-tests.md) is a stub), so
   most controls are **configured and inspectable but not yet
   board-verified**: marked **Gap** or *planned/partial* throughout, matching
   the V-model's honest state.
2. **The root of trust is the chip + ESP-IDF**, not LiteForth's own dual-core
   sealed-package scheme. Secure Boot v2, flash encryption, anti-rollback and
   OTA are Espressif's mechanisms; our job (and the evidence here) is
   *configuring them correctly, controlling ESP-IDF as SOUP, and proving both
   automatically* ([security-plan.md](security-plan.md)).

One main application is now fixed: like the CH32H417, the device records CODEC
audio and other data streams to the microSD card as **encrypted, authenticated
logs only the data owner's server can read**, using the C5's cryptographic
hardware, and the cybersecurity system records **penetration attempts and
other security events into a log of the same form**
([data-logs.md](data-logs.md), [UN-09](../quality/needs.md),
[UN-10](../quality/needs.md)). That makes this a data-recording device with
real confidentiality/integrity stakes, and it gives the weakest control
category — event detection and logging ([§5.6](#56-event-detection-and-logging))
— a concrete, designed home. The NM-CYD-C5 is still a development board, so the
hazard analysis of a finished product remains to be pinned down. Status: draft,
2026-11-04.

---

## 1. The device and its ports

The product is **LiteForth running as an ESP-IDF application on the ESP32-C5**
(NM-CYD-C5 board: 16 MB flash, 8 MB PSRAM, ST7789 TFT + touch, microSD, RGB
LED). A single RISC-V core runs the application; the boot chain and security
services are the chip's and ESP-IDF's ([esp32c5-notes.md](../esp32c5-notes.md)).

The interfaces the port has today — the "ports" an FDA labeling section asks a
manufacturer to enumerate — and their intended production posture:

| Interface | Direction | Function | Trust and control |
|---|---|---|---|
| CH340 USB-UART console (UART0) | in/out | Service terminal; the Forth REPL; `idf.py flash` auto-reset | Untrusted input. Updates must be vendor-signed (OTA); a production build should offer only a restricted wordlist ([SYS-SEC-08](../quality/system.md), planned) |
| microSD card (SPI) | in/out | Source, data, block file | Fully untrusted; checked before use, as on the CH32H417 (shared core) |
| USB-Serial-JTAG / ROM download | in | Silicon bootloader / debug | **Closed in production** by `DIS_DOWNLOAD_MODE` + the secure-boot/JTAG fuses ([SYS-SEC-03](../quality/system.md), [SYS-SEC-01](../quality/system.md)); updates then via OTA only |
| OTA (over the console now; Wi-Fi later) | in | Signed firmware updates | Vendor-signed, verified by the secure-boot key, installed on trial ([SYS-UPD-01](../quality/system.md), planned) |
| **Wi-Fi 6 / Bluetooth LE** | — | **Not enabled yet** (a planned feature, [UN-08](../quality/needs.md)) | The chip *is* radio-capable; the milestone-1 image links no Wi-Fi/BLE ([soup.md](soup.md)), so the radios are not attack surface today — but see [§10](#10-section-524b-cyber-device-status) |

The crucial difference from the CH32H417, where the silicon has **no** network
of any kind: the ESP32-C5 **is a connectable part**. Wi-Fi/BLE are off in the
current image, so the *present* remote attack surface is empty — but
connectivity is a design goal, not a physical impossibility. The 524B analysis
([§10](#10-section-524b-cyber-device-status)) turns on exactly this.

---

## 2. Secure Product Development Framework (SPDF)

The guidance (§IV.A.1, §V) asks that cybersecurity be managed through a secure
development framework across the total product life cycle, not bolted on — the
project's stated intent for this target from the first commit
([security-plan.md §2](security-plan.md)). The SPDF artifacts already in place:

- **Requirements and traceability.** A V-model of stakeholder needs, system
  requirements, architecture and modules, each naming its parent and its
  evidence, with a checked Requirements Traceability Matrix
  ([doc/ESP32C5/quality/](../quality/), [rtm.md](../quality/rtm.md),
  [SYS-QA-01](../quality/system.md)). The security spine is first-class in it.
- **A security configuration gate.** The production security baseline is a
  build check, not an intention: [seccheck.py](../../../scripts/ESP32C5/seccheck.py)
  parses `sdkconfig` and fails the build on any regressed control
  ([SYS-SEC-05](../quality/system.md), [SYS-SEC-07](../quality/system.md);
  `unit_tests/seccheck`, run in CI).
- **SOUP controls, automated.** A pinned manifest, an SPDX SBOM and a CVE
  monitor for ESP-IDF and its bundled libraries, in CI and on a schedule
  ([soup.md](soup.md), [SYS-SEC-06](../quality/system.md); `unit_tests/soup`,
  `.github/workflows/soup-cve.yml`).
- **A product security plan** covering the standard, the architecture, the
  SOUP controls, the threat model, key management and provisioning
  ([security-plan.md](security-plan.md)).
- **Board tests on record (when the board arrives).** The log format and the
  on-board security battery are defined ([board-tests.md](board-tests.md),
  [security-plan.md §7](security-plan.md)); no entries yet.

Mapped to the QMS expectation (§IV.A): the functional V-model and the
security-specific `security-plan.md` are separate but linked, as a filing
would carry them.

---

## 3. Security risk management

The guidance (§V.A) expects threat modeling, a cybersecurity risk assessment
separate from the safety one, treatment of third-party components and
unresolved anomalies, and life-cycle management (AAMI TIR57 / ANSI/AAMI SW96,
ISO 14971 / ISO 13485).

- **Threat model.** [security-plan.md §5](security-plan.md) tabulates the
  adversaries and the control that answers each: unauthorized firmware,
  flash/secret readout, debugger key extraction, the serial download back
  door, downgrade, a known SOUP CVE, console/OTA fuzzing, a bad-update brick,
  and — called out explicitly — **automated tooling sweeping all of the
  above**, answered by a baseline with no soft targets.
- **Out of scope, stated.** Invasive physical attacks and private-key
  compromise are out of scope, the same honest limit as the CH32H417
  ([security-plan.md §1/§5](security-plan.md)).
- **Third-party components.** ESP-IDF and its bundled code are managed as SOUP
  with their own risk controls ([§8](#8-software-bill-of-materials-sbom),
  [soup.md](soup.md)) — the dominant third-party risk on this target.
- **Unresolved anomalies / known vulnerabilities.** The CVE monitor matches
  the pinned SOUP against advisories and fails on a linked, unpatched hit
  ([cvecheck.py](../../../scripts/ESP32C5/cvecheck.py)). **Gap:** no formal
  link yet to CISA's Known Exploited Vulnerabilities catalogue, and the
  security *safety*-risk file (ISO 14971 linkage) is not written — a finished
  product's hazard analysis needs the full application pinned down (the
  recording application, [§1](#1-the-device-and-its-ports), is now fixed; the
  clinical context is not).

---

## 4. Security objectives

For this target, in priority order (from [security-plan.md §2/§3](security-plan.md)):

1. **Authenticity** — only vendor-signed firmware runs (Secure Boot v2).
2. **Confidentiality** — firmware/IP and secrets are unreadable at rest
   (flash encryption) and over the debug/download paths (fuses).
3. **Integrity** — updates are signed, versioned and roll back cleanly.
4. **Patchability** — the SOUP is known, monitored and can be bumped.
5. **No soft targets** — every documented hardening on, every debug/download
   path closed in production, verified automatically.

Availability is a lower objective until an application fixes it; a bad update
must not brick the device ([§5.7](#57-resiliency-and-recovery)), but there is
no life-safety availability claim for a platform port.

---

## 5. Security architecture: the eight control categories

The guidance (§V.B.1, Appendix 1) groups controls into eight categories. Each
row cites the ESP32-C5 requirement(s) and the strongest evidence that exists
**today**; because the board is not here, most "evidence" is the configuration
and its gate, not a board run. This is the core gap list.

### 5.1 Authentication

The guidance asks for cryptographically strong authentication of updates
before install, no hardcoded/default passwords, anti-replay, and — explicitly
— **"Do not rely on CRCs as security controls."**

- **Updates are authenticated by Secure Boot v2** (RSA-3072 or ECDSA): the
  bootloader verifies the application's signature against the vendor key whose
  digest is in eFuse ([SYS-SEC-01](../quality/system.md),
  [ARC-SECBOOT](../quality/architecture.md)). *Configured in the overlay and
  gated by seccheck; **Gap:** not burned/board-verified.*
- **No passwords on the device**; nothing hardcoded to guess. Device keys come
  from eFuse / the DS peripheral ([SYS-SEC-09](../quality/system.md),
  planned).
- **Anti-replay / downgrade:** anti-rollback via the `SECURE_VERSION` eFuse
  ([SYS-SEC-04](../quality/system.md); overlay-gated, not yet burned).
- **CRCs are not a security control** here either: integrity rests on the
  secure-boot signature and (for the shared sealed/YMODEM path carried over
  from the CH32H417) BLAKE2s, never a CRC.

### 5.2 Authorization

Least privilege.

- **A restricted production console wordlist** so the serial path is not an
  open read/write interpreter into the device ([SYS-SEC-08](../quality/system.md)).
  **Gap:** planned, not designed — the single most important authorization
  control for this target and called out in [§11](#11-requirement-impact-what-the-fda-framing-changes).
- **ESP-IDF privilege model.** The application is one task on the IDF runtime;
  the ESP32-C5 has no second supervisory core like the CH32H417's V3F, so the
  isolation story is the chip's (secure boot + flash encryption + closed
  debug), not a PMP split. This is a deliberate model difference, not a
  missing control ([security-plan.md](security-plan.md)).
- **World Controller / memory protection** on the C5 (if used for a privilege
  boundary) is an **open item** — see [§11](#11-requirement-impact-what-the-fda-framing-changes).

### 5.3 Cryptography

Strong, standard cryptography.

- **Secure Boot v2**: RSA-3072 or ECDSA P-256/384 (scheme is an open decision,
  [security-plan.md §10](security-plan.md)), in the chip's ROM/bootloader.
- **Flash encryption**: XTS-AES with the key in eFuse
  ([SYS-SEC-02](../quality/system.md)).
- **The shared LiteForth crypto** (Monocypher X25519/Ed25519, BLAKE2s, AES/SM4
  CTR) is carried over unchanged for any sealed-package/application-content use
  ([SYS-CORE-01](../quality/system.md); tested on the desktop, covered by the
  CH32H417 SBOM and `unit_tests`). Whether LiteForth's sealed format is kept
  on top of IDF's OTA is an open decision ([security-plan.md §10](security-plan.md)).
- **The C5's crypto accelerators are used through checked hooks**
  ([SYS-SEC-10](../quality/system.md), [ARC-CRYPTO](../quality/architecture.md)).
  The C5 has accelerators the CH32H417 lacks — **SHA-1..512, ECC (P-192/256/384),
  HMAC-SHA-256 and the RSA/ECDSA DS peripheral**, on top of the AES accelerator
  and TRNG ([esp32c5-notes.md](../esp32c5-notes.md)). Each is reached through a
  thin target hook over the IDF/ROM driver, verified once against the software
  reference with a software fallback — the pattern the shared `ctr.c`'s
  `CTR_HW`/ECDC seam already establishes. So the bulk cipher, hashing, HMAC KDF,
  ECDSA/ECDH identity and randomness all run on hardware where the chip offers
  it, rather than in software. This keeps the crypto standard *and* leaves no
  idle accelerator a design review would question.
- **Keys in hardware** via eFuse + the **HMAC peripheral** (a burned HMAC key
  used without being read) and the **DS peripheral** (the private asymmetric key
  used without being read), so a private key never sits in readable memory
  ([SYS-SEC-09](../quality/system.md), [SYS-SEC-10](../quality/system.md),
  planned).
- **A NIST-approved profile, quantum-hedged.** The C5's own log/crypto profile is
  NIST-approved end to end — **AES-256-GCM** (SP 800-38D), **HKDF-SHA256**
  (SP 800-56C) and **SP 800-108** HMAC-SHA256 key rooting, HMAC-SHA256
  (FIPS 198-1) / SHA-256 (FIPS 180-4), X25519 (SP 800-186), a SP 800-90A DRBG,
  and LMS (SP 800-208) for signatures — and every KDF's security is stated
  against its standard ([data-logs.md §3.6](data-logs.md)). AES-256 (not 128) and
  the hash-based LMS are the deliberate quantum hedge: a break of the hardness
  assumptions under ECDSA/RSA would not degrade gracefully, so the design rests
  on symmetric/hash-based primitives whose only quantum threat (Grover) is a
  bounded, key-length problem AES-256 already answers.

### 5.4 Code, data and execution integrity

- **Only signed code runs**: Secure Boot v2 verifies the bootloader and app;
  a runtime check refuses to run if flash encryption was somehow disabled
  (`CONFIG_SECURE_FLASH_CHECK_ENC_EN_IN_APP`, [ARC-SECBOOT](../quality/architecture.md),
  seccheck-gated). **Gap:** board-verified refusal of a tampered image is a
  planned board test ([security-plan.md §7.4](security-plan.md)).
- **The dictionary flash is a separate partition** (`lfflash`), so a Forth
  flash write never touches the signed application image
  ([ARC-PART](../quality/architecture.md), [SYS-FLASH-01](../quality/system.md)).
- **Shared-core integrity checks** (sealed blocks/records checked when read)
  carry over from the CH32H417 ([SYS-CORE-01](../quality/system.md)).

### 5.5 Confidentiality

- **Flash encryption (Release mode)** keeps firmware/IP and secrets encrypted
  at rest ([SYS-SEC-02](../quality/system.md)); Release mode closes the
  re-encryption/readback back doors Development mode leaves open
  ([seccheck.py](../../../scripts/ESP32C5/seccheck.py) forbids Development mode
  in every profile).
- **Debug/download readout closed**: `DIS_DOWNLOAD_MODE` and the JTAG fuses so
  the part can't be dumped over UART/USB/JTAG ([SYS-SEC-03](../quality/system.md),
  [SYS-SEC-05](../quality/system.md)).
- **Secure decommissioning**: erasing the flash-encryption key renders the
  flash unreadable — the ESP32 analogue of the CH32's RDP-wipe
  ([§9](#9-transparency-labeling-and-the-management-plan)). **Gap:** document
  and board-verify the exact sequence.
- **No per-device secret in plaintext**: keys read-protected in eFuse
  ([SYS-SEC-09](../quality/system.md), planned).
- **Recordings are confidential**: each CODEC/data log is encrypted so only the
  data owner's server can read it, with the board's UID kept off the card
  ([SYS-LOG-01](../quality/system.md), [SYS-LOG-02](../quality/system.md),
  [data-logs.md](data-logs.md)). Same format as the CH32H417, bulk cipher on the
  C5 AES accelerator.

### 5.6 Event detection and logging

The guidance asks that a device detect and log security-relevant events.

- ESP-IDF's bootloader and app log boot, secure-boot and update outcomes over
  the console.
- **A security-event log is designed** ([SYS-LOG-04](../quality/system.md),
  [data-logs.md §4](data-logs.md)): penetration attempts and security decisions
  — refused updates, rollback/anti-rollback, boot-integrity failures,
  download-mode/JTAG access attempts, tamper, console anomalies — are recorded to
  the card in the **same encrypted, authenticated log format** as the data logs,
  so an attacker can neither read nor forge the record of their own attempts, and
  the server verifies it with the same tool. This gives the category — the
  weakest on the CH32H417 — a concrete home on this target, reusing the logging
  subsystem rather than a bespoke mechanism. **Gap:** designed, not built (the
  event types and the write points at each security decision); events that must
  survive a wiped card also need a small eFuse/NVS counter
  ([data-logs.md §4](data-logs.md)).

### 5.7 Resiliency and recovery

- **Never-brick update**: OTA installs on trial and rolls back a bad image
  ([SYS-UPD-01](../quality/system.md), [ARC-OTA](../quality/architecture.md);
  IDF's `app_rollback`, overlay-gated). **Gap:** not yet built or board-tested.
- **Anti-rollback** refuses a downgrade to a withdrawn image
  ([SYS-SEC-04](../quality/system.md)).
- The shared core's power-cut-safe storage carries over
  ([SYS-CORE-01](../quality/system.md)).

### 5.8 Firmware and software updates (updatability / patchability)

- **Signed OTA** into an A/B slot layout (`ota_0`/`ota_1`/`otadata`),
  verified by the secure-boot key, installed on trial, confirmed or rolled
  back ([ARC-PART](../quality/architecture.md),
  [ARC-OTA](../quality/architecture.md), [SYS-UPD-01](../quality/system.md)).
  **Gap:** the partition layout exists; the OTA logic does not.
- **SOUP patchability** is the part that *is* built: the device's ESP-IDF and
  its bundled libraries are inventoried and monitored so a fix can be
  identified and scheduled ([§8](#8-software-bill-of-materials-sbom)). The
  field delivery of that fix depends on the OTA mechanism above — so the
  monitor tells us *when* to patch, and OTA (planned) is *how*.

---

## 6. Security architecture views

The guidance (§V.B.2) asks for four views.

- **Global system view.** One ESP32-C5: the ROM → signed bootloader → signed
  app chain, external encrypted QSPI flash, a microSD card, and a CH340 USB
  console to a local service PC. No network in the current image. The offline
  signing root and a (planned) provisioning station touch the device only in
  the factory. Diagram source: [architecture.md](../quality/architecture.md),
  [security-plan.md §5](security-plan.md).
- **Multi-patient harm view.** **Today:** limited, because the image has no
  network — a compromised unit cannot reach another. **But conditional on the
  radios staying off:** unlike the CH32H417, this is a *configuration*
  property, not a silicon one, so the view must be revisited the moment Wi-Fi/
  BLE is enabled ([§10](#10-section-524b-cyber-device-status)). The one
  fleet-wide asset is the vendor signing key (offline).
- **Updateability / patchability view.** vendor build → offline root certifies
  → signed image → OTA slot → secure-boot verify → trial → confirm/rollback,
  with anti-rollback as a floor ([§5.8](#58-firmware-and-software-updates-updatability--patchability)).
  **Gap:** the OTA leg is planned.
- **Security use-case views.** Update (OTA), provisioning (eFuse burns),
  console service, and recovery — defined in [security-plan.md §7/§9](security-plan.md),
  to be worked through as the board arrives.

---

## 7. Cybersecurity testing

The guidance (§V.C) asks for requirement and threat-mitigation testing,
vulnerability testing, penetration testing (with independence and scope),
fuzzing, and static/dynamic analysis, continued after release.

- **Requirement / threat-mitigation testing.** The V-model's evidence. Today
  that is: the **security-config gate** (`unit_tests/seccheck`) and the
  **SOUP SBOM + CVE monitor** (`unit_tests/soup`, the scheduled OSV run), both
  in CI. The on-board battery (secure-boot enforcement, flash-encryption
  readback, anti-rollback, eFuse state) is specified but **not yet run**
  ([security-plan.md §7.3–7.6](security-plan.md); [board-tests.md](board-tests.md)
  is a stub). This is the largest testing gap and it is board-dependent.
- **Vulnerability testing.** The CVE monitor is the standing automated check
  against the SOUP ([§8](#8-software-bill-of-materials-sbom)).
- **Penetration / hardness testing.** The ESP32 analogue of the CH32H417's
  `hardness.py` — reading the locked eFuse state and throwing tampered/unsigned
  images at a locked board — is planned ([security-plan.md §7.3/§7.4](security-plan.md)).
  **Gap:** needs a provisioned board.
- **Fuzz testing.** The shared exFAT/card reader is fuzzed on the desktop
  (carried over). Console/OTA input fuzzing, and later network inputs, are
  planned ([security-plan.md §7.6](security-plan.md)).
- **Static/dynamic analysis.** ESP-IDF builds are expected warning-clean
  ([SYS-QA-01](../quality/system.md)); **Gap:** ESP-IDF is not in CI (no
  toolchain), so the build-clean check is a developer step, and no SAST/DAST
  is wired in.

---

## 8. Software bill of materials (SBOM)

The guidance (§V.A.4) and Section 524B(b)(3) call for a machine-readable SBOM
of commercial, open-source and off-the-shelf components, each with its support
level and end-of-support date, and known vulnerabilities identified.

This is a **strong** category for the target, because the SOUP controls were
built first:

- **An SPDX 2.3 SBOM of the ESP-IDF SOUP** — the pinned framework plus its
  bundled mbedTLS, FreeRTOS, lwIP, newlib, wpa_supplicant, zlib and TinyUSB —
  generated from a pinned manifest and checked in CI
  ([soup.spdx.json](../../../src/target/ESP32C5/soup.spdx.json),
  [soup_sbom.py](../../../scripts/ESP32C5/soup_sbom.py),
  [soup.md](soup.md), [SYS-SEC-06](../quality/system.md)). It records each
  component's version, licence, CPE/purl and — the attack-surface point the
  guidance implies — whether it is actually **linked** into the image.
- **Known-vulnerability identification** is automated: the CVE monitor matches
  the SBOM versions against a curated backstop and the live OSV feed, failing
  on a linked, unpatched advisory ([cvecheck.py](../../../scripts/ESP32C5/cvecheck.py),
  `.github/workflows/soup-cve.yml`).
- The repository's own third-party code remains covered by the existing SBOM
  ([sbom.spdx.json](../../../sbom.spdx.json)); the two are complementary.

**Gap:** the SBOM does not yet carry a per-component **support level and
end-of-support/EOL date**, and the version fields marked `CONFIRM` in the
manifest must be confirmed against the pinned IDF install
([soup_sbom.py --verify-idf](../../../scripts/ESP32C5/soup_sbom.py)). No CISA
KEV cross-reference yet.

---

## 9. Transparency: labeling and the management plan

**Labeling (§VI.A).** The relevant items for this target: the
ports-and-interfaces table ([§1](#1-the-device-and-its-ports)); the SOUP SBOM,
kept current ([§8](#8-software-bill-of-materials-sbom)); how to obtain and
install vendor-authorized, version-identifiable OTA updates; the secure shipped
configuration (secure boot + flash encryption + download disabled + the seccheck
baseline) and which changes raise risk (a dev build, enabling the radios);
how the device reports security events ([§5.6](#56-event-detection-and-logging));
and **secure decommissioning** (erase the flash-encryption key)
([§5.5](#55-confidentiality)). **Gap:** no labeling drafted — the application
isn't fixed.

**Cybersecurity management plan (§VI.B).** The guidance asks for named
responsible personnel, monitoring sources and cadence, CISA KEV handling,
patch-development and field-deployment timelines, the update capability, a
**coordinated vulnerability disclosure (CVD)** process, and how remediations
reach customers. The automatable core — monitoring the SOUP — is **built and
scheduled** ([§8](#8-software-bill-of-materials-sbom)). **Gap:** the CVD
process, the declared support period, the patch-cadence policy, and the
field-delivery mechanism (OTA) are not yet formalized; these are the main
non-code work, shared with the CH32H417 target ([security-plan.md §10/§11](security-plan.md)).

---

## 10. Section 524B ("cyber device") status

This is where the ESP32-C5 **diverges sharply** from the CH32H417.

Section 524B applies to a "cyber device" — software that **has the ability to
connect to the internet**, among other criteria. The CH32H417 has no network
silicon, so it likely falls outside the definition. **The ESP32-C5 is a Wi-Fi 6
/ BLE part.** Even though the milestone-1 image links no radio code
([soup.md](soup.md)) and the present remote attack surface is empty, the chip's
*ability* to connect is a hardware fact, and Wi-Fi/BLE is a stated product goal
([UN-08](../quality/needs.md)).

Two readings, stated honestly:

- **If the shipped device enables networking** (the likely product), it is a
  cyber device and **524B applies in full**: a postmarket vulnerability-
  management plan, a coordinated-disclosure process, and an SBOM as a condition
  of clearance. The SBOM and the SOUP CVE monitor
  ([§8](#8-software-bill-of-materials-sbom)) are already pointed at exactly the
  postmarket obligation; the CVD process and support period
  ([§9](#9-transparency-labeling-and-the-management-plan)) are the pieces to
  finish.
- **If a variant ships with the radios provably disabled** (fused/configured
  off, no network stack linked), the strict statutory trigger is arguable — but
  given a radio-capable part, the conservative and recommended posture is to
  **treat the device as a cyber device regardless** and keep the full 524B
  package. The cost is low because the hardest part (the SBOM + monitor) is
  built.

So, unlike the CH32H417, the recommendation here is **not** to scope 524B out.
The remote attack surface and the multi-patient-harm view
([§6](#6-security-architecture-views)) must be assumed live for the shipped
product, and enabling Wi-Fi/BLE should be treated as a new threat model
(mbedTLS/lwIP/wpa_supplicant become *linked* SOUP, and TLS trust anchors are
added), not an increment.

---

## 11. Requirement impact: what the FDA framing changes

Recommendations, to adopt into [doc/ESP32C5/quality/](../quality/)
deliberately, not yet applied.

### Tighten / add

- **Build the planned load-bearing controls and board-verify them.** Secure
  Boot v2, flash encryption (Release), download-mode disable, anti-rollback
  ([SYS-SEC-01..04](../quality/system.md)) are *configured and gated* but not
  burned or tested; under an authenticity/confidentiality claim they must be
  provisioned on a board and checked by the planned hardness battery
  ([security-plan.md §7](security-plan.md)). This is the dominant gap.
- **Restricted production console wordlist ([SYS-SEC-08](../quality/system.md)).**
  Still only *planned*; the serial REPL must not be an arbitrary read/write
  back door on a production unit. Design and require it.
- **Event detection and logging — now designed, build it.** Previously the
  weakest category; it now has a requirement and a design: the security-event
  log in the shared encrypted/authenticated log format
  ([SYS-LOG-04](../quality/system.md), [data-logs.md §4](data-logs.md),
  [§5.6](#56-event-detection-and-logging)). The remaining work is building the
  event types, the write points, and the eFuse/NVS counter for events that must
  survive a wiped card.
- **Quantum-resistant signatures (LMS).** For the signature layer LiteForth
  verifies, adopt LMS over ECDSA ([SYS-SEC-11](../quality/system.md),
  [esp32c5-notes.md](../esp32c5-notes.md#signatures-prefer-lms-quantum-resistant-over-ecdsa)):
  select parameters and a vetted implementation (a new SOUP/SBOM component).
  The chip's Secure Boot v2 stays RSA/ECDSA, so state the quantum-resistance
  claim as scoped to our own packages/content.
- **Use the C5 accelerators through checked hooks ([SYS-SEC-10](../quality/system.md)).**
  Build the SHA/HMAC/ECC/DS hooks (the AES seam exists), each checked once
  against software — so a review finds no idle accelerator and the crypto runs
  on hardware where the chip offers it.
- **SBOM support status and EOL, and a CISA KEV cross-reference** (tighten
  [SYS-SEC-06](../quality/system.md) / [soup.md](soup.md)): add a support
  level and end-of-support date per SOUP component and a KEV check in the
  monitor.
- **Treat the device as a cyber device (524B).** Make the CVD process, the
  declared support period and the OTA field-delivery path owned deliverables,
  not "planned" ([§9](#9-transparency-labeling-and-the-management-plan),
  [§10](#10-section-524b-cyber-device-status)). This is the biggest
  framing-driven change versus the CH32H417.
- **Key storage in hardware ([SYS-SEC-09](../quality/system.md)).** Specify and
  verify the eFuse/DS-peripheral key scheme before any key-bearing claim.

### Loosen / scope out (document the rationale)

- **While the radios are off,** the network/remote controls (TLS, network IDS,
  remote-session management) are not *exercised* — but, per
  [§10](#10-section-524b-cyber-device-status), do **not** record this as a
  permanent scope-out the way the CH32H417 can; record it as "deferred until
  Wi-Fi/BLE lands," which is when they become required.
- **Availability** as a safety objective is low for a platform port with no
  therapy delivery; a bad update must still not brick the device
  ([§5.7](#57-resiliency-and-recovery)).
- **The dual-core PMP/Guarded-DMA control story** from the CH32H417 does not
  apply — the C5 is single-core and leans on the chip's secure boot + flash
  encryption instead. Scope it out with that rationale, don't leave it as a
  silent gap.

### Open decisions that the framing makes urgent

- Secure Boot v2 scheme (RSA-3072 vs ECDSA), the C5 download-mode fuses, NVS
  key scheme, and whether LiteForth's sealed format is kept on top of IDF OTA
  ([security-plan.md §10](security-plan.md)) — each is a security-relevant
  choice a submission would need settled.

---

## 12. Traceability

The guidance expects each security control to trace to a requirement and to
its verification evidence. The ESP32-C5 Requirements Traceability Matrix
provides this ([rtm.md](../quality/rtm.md), [SYS-QA-01](../quality/system.md)),
with the honest caveat that the evidence today is configuration, inspection and
the CI gates (seccheck, soup) rather than board runs; the board-test log
([board-tests.md](board-tests.md)) will supply the hardware evidence. The
per-category tables in [§5](#5-security-architecture-the-eight-control-categories)
are the index from the FDA control categories into that matrix.

---

## References

- FDA, *Cybersecurity in Medical Devices: Quality Management System
  Considerations and Content of Premarket Submissions*, docket
  FDA-2021-D-1158 (February 2026; nonbinding recommendations):
  <https://www.fda.gov/media/119933/download>
- AAMI TIR57; ANSI/AAMI SW96; ISO 14971; ISO 13485 / 21 CFR 820 QMSR
- NTIA, *The Minimum Elements for a Software Bill of Materials*; CISA, *Known
  Exploited Vulnerabilities Catalog*; SPDX 2.3; OSV (<https://osv.dev>)
- Espressif, *ESP-IDF Security* guides (Secure Boot v2, Flash Encryption,
  ESP32-C5 TRM) — the SOUP's own security documentation
- LiteForth: [security-plan.md](security-plan.md), [soup.md](soup.md),
  [esp32c5-notes.md](../esp32c5-notes.md), [board-tests.md](board-tests.md), the
  V-model ([doc/ESP32C5/quality/](../quality/)), and the CH32H417's
  [fda.md](../../CH32H417/admin/fda.md) for the house approach
