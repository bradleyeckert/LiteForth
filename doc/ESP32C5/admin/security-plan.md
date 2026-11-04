# Product security plan: LiteForth on ESP32-C5

Status: **draft, foundational.** This is the anchor document for building
cybersecurity into the ESP32-C5 target from the start, not bolting it on. It
sets the standard, the architecture, the controls for ESP-IDF as SOUP, the
threat model, and -- first-class here -- the test automation we will build.
The control-by-control FDA mapping (`fda.md`) and the V-model items
(`doc/ESP32C5/quality/`, mirroring the CH32H417) follow from this.

See also `doc/ESP32C5/esp32c5-notes.md` (chip/board, IDF app model) and the
CH32H417's `security-plan.md` for the house approach. The ESP32-C5 model is
**different in kind**: single core, and the root of trust is the chip +
ESP-IDF (Secure Boot v2, flash encryption, OTA), not LiteForth's dual-core
sealed-package scheme. We lean on Espressif's security engineering and spend
our effort on *configuring it correctly, controlling the SOUP, and proving
both automatically.*

## 1. Scope

LiteForth running as an ESP-IDF application on the ESP32-C5 (NM-CYD-C5 board).
In scope: the boot chain, firmware/OTA updates, key storage, the console and
other input surfaces, data at rest, and the provenance and configuration of
ESP-IDF itself. A main application is recording CODEC/data streams to the card
as encrypted, authenticated logs, with security events (penetration attempts)
logged in the same format — designed in [data-logs.md](data-logs.md), using the
C5's crypto accelerators through checked hooks
([esp32c5-notes.md](esp32c5-notes.md)). Out of scope (stated, as the FDA asks):
invasive physical attacks (decapsulation, FIB), and attacks that assume
possession of a valid signing private key.

## 2. What drives the requirements

- **FDA premarket cybersecurity guidance** (FDA-2021-D-1158, the same
  document used for the CH32H417) is taken as the **best-practice standard**,
  not because this is a cleared device but because it is a rigorous, current
  bar: a Secure Product Development Framework (SPDF), the eight security
  control categories, security risk management, an SBOM, and a plan to keep
  the device secure in the field.
- **Automated and AI-driven attack tooling.** The threat model must assume
  the attacker is not a lone expert spending weeks, but tooling that tries
  the entire known-weakness surface cheaply and at scale: default
  credentials, debug/download back doors, known-CVE exploits against the
  SOUP, fuzzed inputs, downgrade attacks. The defensive posture is therefore
  "no soft targets": every documented hardening control on, every debug/
  download path closed in production, and the SOUP kept patched -- because an
  unpatched known CVE in a bundled library is exactly what such tooling finds
  first.
- **Build it in from the beginning.** Controls, their configuration baseline,
  and their automated verification land alongside the code, so security is a
  gating property of every build, not an audit at the end.

## 3. What the ESP32-C5 + ESP-IDF give us

The chip and framework provide the root of trust; our job is to turn it on
and prove it. Inventory:

- **Secure Boot v2** -- the ROM verifies the bootloader and the bootloader
  verifies the app, by RSA-3072 or ECDSA (P-256/384) signature; the public-
  key *digest* is burned in eFuse. Up to three keys with revocation.
- **Flash encryption** -- XTS-AES-128/256 of flash contents; key in eFuse,
  not readable by software. *Release* mode (vs *development*) makes it
  permanent and closes the re-encryption back doors.
- **eFuse** -- one-time-programmable: secure-boot key digests, the flash
  encryption key, `DIS_DOWNLOAD_MODE`, `DIS_USB_SERIAL_JTAG`/JTAG controls, a
  monotonic `SECURE_VERSION` for anti-rollback, and read/write protection on
  key blocks.
- **Secure storage for our own keys** -- the Digital Signature (DS)
  peripheral + an eFuse-resident HMAC key let the app sign/derive without the
  private key ever existing in readable flash or RAM; NVS encryption protects
  other stored secrets.
- **OTA with rollback and anti-rollback** -- `esp_ota_*` + the `otadata`
  partition give trial/confirm/rollback; `SECURE_VERSION` + anti-rollback
  config refuse downgrades. This is the IDF-native analogue of the CH32's
  trial/confirm/anti-rollback updater.
- **Memory protection / isolation** -- PMP/PMA, the TEE/World-Controller
  (`esp_tee`) and APM for privilege separation; a watchdog set and brownout
  detector for resiliency.
- **Crypto accelerators + TRNG** -- AES, SHA, RSA, ECC, HMAC, and a hardware
  RNG for keys and nonces.

## 4. ESP-IDF as SOUP (software of unknown provenance)

ESP-IDF is a large third-party dependency that ships *more* third-party code
inside it (mbedTLS, wpa_supplicant, lwIP, FreeRTOS, newlib, zlib, etc.). It is
SOUP, and SOUP that reaches the network and parses untrusted input. It needs
its own controls, independent of LiteForth's code:

1. **Pin and record the version.** Build against one exact IDF version
   (currently **v5.5.6**) and record its git commit and the toolchain
   version. Capture `dependencies.lock` and the `managed_components` set.
   Reproducible builds so an image can be tied to its exact inputs.
2. **Know the real attack surface.** The linked component set -- not the
   whole framework -- is what matters. The milestone-1 image links ~164 KB
   of IDF runtime with **no Wi-Fi/BLE** (garbage-collected, unused); that is
   the current surface. When networking is added, the surface grows to
   mbedTLS, lwIP and the wireless stacks, and the SOUP controls tighten
   accordingly. Keep unused components out (don't link them).
3. **SBOM of the SOUP.** Generate a bill of materials for the IDF components
   actually in the image and their upstream third-party versions (mbedTLS
   x.y, wpa_supplicant, lwIP, FreeRTOS kernel, newlib), so a CVE against any
   of them can be matched to this device. Extend or complement the existing
   `scripts/mksbom.py`.
4. **Monitor advisories.** Track Espressif security advisories and IDF
   release notes, and the upstreams' CVE feeds, against the pinned version.
   This is a standing, automatable control (see §7).
5. **Update policy.** Define when and how IDF is bumped for a security fix:
   the trigger (an advisory affecting a linked component), the regression
   gate (the full build + security-config + on-board checks must pass), and
   the field path (an OTA release). An unpatched, networked SOUP CVE is the
   highest-probability finding for automated attackers, so the update path
   must be real and exercised.
6. **Configuration is part of the dependency.** `sdkconfig` chooses which
   SOUP behaviors are on. A reviewed security baseline (secure boot, flash
   encryption release, download disabled, anti-rollback, no debug back doors,
   assertions, stack protection) is version-controlled and checked (§7).

## 5. Threat model

Adversaries and the controls that answer them:

| Threat | Control |
|---|---|
| Run unauthorized/modified firmware | Secure Boot v2 (signed bootloader + app) |
| Read firmware/IP or secrets from flash | Flash encryption (XTS-AES), key in eFuse |
| Extract keys with a debugger | JTAG disabled; keys read-protected / only via DS peripheral |
| Reflash over the serial/USB back door | `DIS_DOWNLOAD_MODE` (+ USB-serial-JTAG download fuse); OTA only |
| Downgrade to an old, vulnerable version | Anti-rollback (`SECURE_VERSION` eFuse) |
| Exploit a known CVE in the SOUP | Pinned + monitored + patched IDF; minimal linked surface |
| Fuzz the console / OTA / (later) network inputs | Input validation; the signed-update checks; fuzz tests (§7) |
| Brick the device with a bad update | OTA trial/confirm/rollback |
| Automated tooling sweeping all of the above | Baseline with *no* soft targets, verified in CI and on the board |

Explicitly out of scope: invasive physical attacks and private-key
compromise (same as the CH32 plan).

## 6. Security architecture (summary; detail to follow)

- **Boot chain:** ROM -> (verifies) second-stage bootloader -> (verifies)
  app, each by Secure Boot v2 signature; flash encrypted throughout.
- **Partitions:** signed app slots `ota_0`/`ota_1` + `otadata`, encrypted
  NVS for secrets, the `lfflash` data partition for LiteForth's own flash
  pages. (See `esp32c5-notes.md` for the current table; revisit sizes and
  add an `nvs_keys` partition for NVS encryption.)
- **Keys:** secure-boot signing key (offline, like the CH32 release key);
  flash-encryption key (per-device, eFuse, never leaves the chip); the
  device's own application keys via the DS peripheral + eFuse HMAC key.
- **Updates:** OTA, signed, with rollback and anti-rollback.
- **Console:** the CH340 UART terminal is an input surface; at the production
  security level it must present a restricted wordlist (the ESP32 analogue of
  the CH32's safe startup wordlist, SYS-SEC-10) so the Forth interpreter is
  not an arbitrary-memory back door. **Open design item.**
- **Data at rest:** recordings/logs encrypted (carry over the CH32 data-log
  design where it applies).

## 7. Testing and the automation scaffolding (the big build)

The point the FDA guidance and the threat model share: a control you cannot
*demonstrate* is not a control. Most of this must be automated, both in CI
(config/SOUP) and against a board (eFuse/boot/OTA). Planned scaffolding, under
`scripts/ESP32C5/` mirroring `scripts/CH32H417/`:

1. **Security-config linter (CI).** Parse the built `sdkconfig` and assert the
   security baseline: Secure Boot v2 on, flash encryption = Release, download
   mode disabled, anti-rollback on, JTAG disabled, assertions on, no
   `*_INSECURE`/development options, stack protection. Fails the build if any
   control regresses. This is the ESP32 analogue of `hardness.py`'s config
   phase and the most important early piece -- it makes the baseline a gate.
2. **SOUP/CVE monitor (CI).** From the pinned IDF version + component SBOM,
   check Espressif advisories and upstream CVE feeds; flag an affected,
   linked component. Runs on a schedule, not just on push.
3. **eFuse-state verification (board, `espefuse.py`).** Read the shipped
   fuses and assert they match the secure baseline (secure boot enabled,
   flash encryption enabled + release, download disabled, keys
   read-protected, `SECURE_VERSION` as expected). The analogue of
   `lockdown.py`/`hardness.py`'s locked-state readback.
4. **Secure-boot & flash-encryption enforcement (board).** Attempt to boot a
   tampered or unsigned image and confirm it is refused; confirm flash reads
   back encrypted. The analogue of `hardness.py`'s bad-input battery.
5. **OTA rollback / anti-rollback (board).** A bad image rolls back; an image
   with a lower `SECURE_VERSION` is refused. Mirrors the CH32 B37/B38 tests.
6. **Input fuzzing (host + board).** Throw malformed console and OTA inputs
   (the surface reachable without a key) and confirm safe rejection -- the
   automated-attacker surface. Later, the network inputs once Wi-Fi/BLE land.
7. **Board-test log.** Record every on-board security run in
   `doc/ESP32C5/admin/board-tests.md`, as the CH32 target does, as evidence.

Where a check can run without the chip (config, SBOM, CVE, host fuzz), it goes
in CI; where it needs the silicon's locked state, it is a board test with a
recorded result.

## 8. Key management

- **Signing key** (Secure Boot v2): generated and held offline, like the
  CH32 release key; only its public digest is on the device (eFuse). Rotation
  and revocation via the three-key slots.
- **Flash-encryption key:** per device, generated on-chip or burned at
  provisioning, read-protected in eFuse, never exported.
- **Application keys:** via the DS peripheral + eFuse HMAC key, so the private
  key never exists in readable memory. Document the derivation.

## 9. Provisioning / manufacturing

A provisioning flow (the ESP32 analogue of the CH32's `provision.py`): flash
signed images, burn the eFuses (secure boot digest, flash-enc key, download
disable, security version), verify the locked state (§7.3), and record each
unit. Define what is burned where (factory vs field) and the irreversibility
checklist, since eFuse burns are one-way.

## 10. Open decisions

**Decided (the C5 log/crypto profile, NIST-approved end to end,
[data-logs.md](data-logs.md)):** AES-256-GCM AEAD (SP 800-38D); HKDF-SHA256
per-file keys (SP 800-56C) and SP 800-108 HMAC-SHA256 key rooting from the eFuse
key; X25519 key agreement (SP 800-186); a SP 800-90A DRBG; **LMS/HSS**
(SP 800-208) for the signatures we verify. AES-256 and LMS are the deliberate
quantum hedge. The portable BLAKE2s/Poly1305/Ed25519 profile stays for the
software path. Still open below.

- Secure Boot v2 signature scheme on the C5: RSA-3072 vs ECDSA-P256/384.
- Restricted startup wordlist for the console at the production level (the
  interpreter must not be an arbitrary read/write back door).
- Whether any of LiteForth's sealed-package format is kept for *application/
  content* updates on top of IDF's firmware OTA, or dropped entirely.
- **Signature scheme for LiteForth's own signature layer: prefer LMS**
  (hash-based, quantum-resistant; RFC 8554 / NIST SP 800-208) over ECDSA, for
  the packages/content/identity we verify — SHA-256-heavy, so it rides the C5
  SHA accelerator; the chip's Secure Boot v2 stays RSA/ECDSA regardless. Choose
  the LMS/HSS parameters and a vetted implementation (a new SOUP component).
- **The hardware-crypto hooks:** reach the C5's SHA, ECC, HMAC and DS
  accelerators (which the CH32H417 lacks) through checked seams like the AES
  one, each verified once against software with a fallback.
- NVS encryption key scheme (`nvs_keys` partition vs HMAC/DS-derived).
- The exact C5 download-mode fuses (UART vs USB-Serial-JTAG, secure download)
  -- confirm against the TRM before provisioning burns them.
- Field update transport once networking lands (TLS trust anchors = more SOUP).
- **Post-quantum key agreement (hedge):** whether to hybridize the X25519 key
  agreement with **ML-KEM** (FIPS 203) so confidentiality survives a future
  break of X25519's discrete-log hardness, not just the signature layer (LMS).
  X25519 is the one Shor-vulnerable primitive left in the profile; a belt-and-
  suspenders X25519+ML-KEM KEM would close it. Weigh the added SOUP and the
  server-side change against the low present risk.

## 11. Suggested order of work

1. This plan (done, iterate).
2. The security-config linter + a reviewed `sdkconfig` security baseline (CI
   gate) -- highest value, needs no board. **Done:**
   `scripts/ESP32C5/seccheck.py`, `sdkconfig.secure.defaults`,
   `unit_tests/seccheck`.
3. The `doc/ESP32C5/quality/` V-model items (needs/system/architecture/
   modules) with security requirements, traced like the CH32 target.
   **Done:** `doc/ESP32C5/quality/`, checked by `unit_tests/rtm`.
4. The SOUP SBOM + CVE-monitor scaffolding. **Done:** `soup.lock.json`,
   `scripts/ESP32C5/soup_sbom.py` (→ `soup.spdx.json`), `cvecheck.py`
   (vs `advisories.json` + OSV), `unit_tests/soup`, the scheduled
   `soup-cve.yml`; see [soup.md](soup.md).
5. `fda.md`: the control-by-control FDA mapping for the ESP32-C5. **Done:**
   [fda.md](fda.md).
6. The provisioning flow and the on-board eFuse/boot/OTA tests + board-test
   log, as the board and the secure-boot keys become available. *(Next;
   board-dependent.)*

## References

- FDA, *Cybersecurity in Medical Devices: Quality System Considerations and
  Content of Premarket Submissions* (FDA-2021-D-1158): https://www.fda.gov/media/119933/download
- ESP-IDF Security guides: Secure Boot v2, Flash Encryption, ESP-TEE, the
  Security Advisories page (pin the version's docs).
- `doc/ESP32C5/esp32c5-notes.md`; the CH32H417 `security-plan.md` / `fda.md`.
