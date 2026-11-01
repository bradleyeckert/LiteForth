# Cybersecurity for a medical device: LiteForth on CH32H417

This document maps LiteForth's security design and evidence to the U.S. FDA
guidance **"Cybersecurity in Medical Devices: Quality Management System
Considerations and Content of Premarket Submissions"** (docket
FDA-2021-D-1158, the February 2026 edition; "Contains Nonbinding
Recommendations"). It is engineering documentation that organizes what the
project already has -- the product security plan
([security-plan.md](security-plan.md)), the V-model requirements
([doc/quality/](../quality/)), the board tests
([board-tests.md](board-tests.md)) and the SBOM
([sbom.md](sbom.md)) -- against that guidance's structure, and it calls out
where the medical-device framing should **tighten or loosen** LiteForth's
own requirements. It is not a filed submission and not regulatory advice;
a regulatory professional owns the actual submission.

The FDA guidance is nonbinding and scales its expectations with a device's
cybersecurity risk rather than with fixed tiers: a simple, unconnected
device needs limited documentation; a connected or safety-critical one needs
more. LiteForth, as built, sits at the low-connectivity end (see
[§1](#1-the-device-and-its-ports) and [§10](#10-section-524b-cyber-device-status)),
so this document is scaled accordingly.

Status: draft, 2026-10-31. Items that LiteForth does not yet satisfy are
marked **Gap**; recommended requirement changes are collected in
[§11](#11-requirement-impact-what-the-fda-framing-changes).

---

## 1. The device and its ports

The product is a **secure audio recorder**. Two CODECs are streamed to
encrypted log files on a microSD card ([data-logs.md](data-logs.md),
[SYS-LOG-01..09](../quality/system.md)); only the data owner's server can
read a recording, and the server can prove which board made it and that
nothing changed ([UN-10](../quality/needs.md)). The recorder runs LiteForth
on a dual-core CH32H417: a V5F core runs the application in user mode, and a
V3F core is the root of trust and the USB bridge
([firmware-updates.md](firmware-updates.md)).

The interfaces the device has today -- the "ports" an FDA labeling section
asks a manufacturer to enumerate -- are the full attack surface, and there
is deliberately no network among them:

| Interface | Direction | Function | Approved endpoint | Trust and control |
|---|---|---|---|---|
| USB CDC terminal (through the V3F) | in/out | Service terminal; firmware/app update by YMODEM (`~x FILE -2`) | A service PC running the vendor's terminal (kwai) | Untrusted input. Updates must be vendor-signed and are checked by stage 1 ([SYS-UPD-01](../quality/system.md), [SYS-UPD-02](../quality/system.md)); a production board boots to a restricted wordlist ([SYS-SEC-10](../quality/system.md), planned) |
| microSD card | in/out | Recordings out; `update.lfu`, the block file and backups in | The card itself | Fully untrusted. Nothing on the card is ever executed without stage 1's checks ([SYS-STO-04](../quality/system.md), [SYS-UPD-02](../quality/system.md)); recordings are encrypted ([SYS-LOG-06](../quality/system.md)) |
| Debug port (SWD / WCH-Link UART) | in/out | Development debug; the V5F's debug UART prints boot and fault lines | A development probe | Under read protection the probe can only erase the chip, never read or change flash ([SYS-SEC-09](../quality/system.md), board:B8/B18/B22/B23). Off-limits for readout on a production board |
| Boot ROM download (USART/USB) | in | The silicon bootloader | -- | **Turned off** in production lock-down ([SYS-SEC-05](../quality/system.md)) |
| Network (Wi-Fi/BLE/Ethernet/cellular/internet) | -- | **None** | -- | The device has no network interface of any kind |

Because the only data paths are a local service USB and a removable card,
the remote attack surface is empty and the "multi-patient harm" surface is
limited to what one physically-held board can reach (nothing, beyond
itself). This is the single biggest factor scaling the guidance down for
this product, and it is stated honestly in the threat model
([security-plan.md §4](security-plan.md)).

---

## 2. Secure Product Development Framework (SPDF)

The guidance (§IV.A.1, §V) asks that cybersecurity be managed through a
secure development framework across the total product life cycle, not bolted
on. LiteForth's development process already supplies the SPDF artifacts:

- **Requirements and traceability.** A V-model of stakeholder needs,
  system requirements, architecture and modules, each naming its parent and
  its evidence, with a checked Requirements Traceability Matrix
  ([SYS-QA-04](../quality/system.md), [rtm.md](../quality/rtm.md)). This is
  the "requirements → evidence" trace the guidance expects for each security
  control ([§5](#5-security-architecture-the-eight-control-categories)).
- **Every change built and tested.** CI builds the desktop host and both
  cores and runs the full test suite on every pull request; the core builds
  fail on a warning ([SYS-QA-01](../quality/system.md)).
- **A current SBOM** regenerated from the source and checked in CI
  ([SYS-QA-02](../quality/system.md), [§8](#8-software-bill-of-materials-sbom)).
- **Board tests on record** -- every security test on real hardware is
  logged with the firmware hashes, steps, expected result and the board's
  verbatim output ([SYS-QA-03](../quality/system.md),
  [board-tests.md](board-tests.md)).
- **A product security plan** covering the root of trust, unique keys,
  secure update, key management and provisioning
  ([security-plan.md](security-plan.md)).

Mapped to the QMS expectation (§IV.A): the safety risk process lives in the
V-model and the security-specific process in `security-plan.md`; a filing
would carry both as separate but linked risk files
([§3](#3-security-risk-management)).

---

## 3. Security risk management

The guidance (§V.A) expects threat modeling, a cybersecurity risk assessment
separate from the safety risk assessment, treatment of third-party
components and unresolved anomalies, and management across the life cycle
(referencing AAMI TIR57 / ANSI/AAMI SW96 and ISO 14971 / ISO 13485).

- **Threat model.** [security-plan.md §4](security-plan.md) enumerates the
  assets (vendor signing keys, per-device keys, firmware/application IP,
  customer recordings, and the device's behavior) and the adversaries by
  reach (remote, device holder, non-invasive lab, factory insider, supply
  chain), each with its defenses. §3 states the honest silicon-level
  property: a physical holder can always wipe and reflash a board, but
  cannot extract its keys, run modified firmware while keeping the board's
  identity, or pass a wiped board off as genuine to the server.
- **Out of scope, stated.** Invasive attacks (decapsulation, probing),
  power/clock glitching and side channels on the AES engine are out of
  scope; the CH32H417 is not a secure element and makes no such claim
  ([security-plan.md §1](security-plan.md), and the hardness runner's report
  repeats this, [SYS-TOOL-07](../quality/system.md)).
- **Third-party components.** Tracked in the SBOM with their licenses and
  checksums ([§8](#8-software-bill-of-materials-sbom)).
- **Unresolved anomalies / known vulnerabilities.** The crypto primitives
  are vendored at pinned versions (Monocypher 4.0.2, a tracked BLAKE2s);
  monitoring these and the WCH SDK for advisories is part of the
  cybersecurity management plan ([§9](#9-transparency-labeling-and-the-management-plan)).
  **Gap:** no formal link yet to CISA's Known Exploited Vulnerabilities
  catalog or the NIST NVD.

---

## 4. Security objectives

The guidance (§IV.B) lists the objectives a design should meet:
authenticity (including integrity), authorization, availability,
confidentiality, and secure and timely updatability/patchability. LiteForth
meets them as follows, with the detail in
[§5](#5-security-architecture-the-eight-control-categories):

| Objective | How LiteForth meets it | Lead evidence |
|---|---|---|
| Authenticity / integrity | Ed25519-signed packages, a per-board image MAC, a root-signed machine-mode region, boot-time signature and hash checks | [SYS-UPD-01](../quality/system.md), [SYS-SEC-06](../quality/system.md), [SYS-SEC-14](../quality/system.md); board:B32/B35/B37 |
| Authorization | Security levels, PMP fencing, a restricted start-up wordlist, Guarded DMA | [SYS-SEC-01..04](../quality/system.md), [SYS-SEC-10..15](../quality/system.md); board:B29..B33/B39 |
| Availability | Never-brick update with trial, rollback and USB recovery | [UN-07](../quality/needs.md), [SYS-UPD-03](../quality/system.md), [SYS-UPD-07](../quality/system.md); board:B35/B37 |
| Confidentiality | Read protection, per-board key wrapping, encrypted recordings, keys not extractable | [UN-09](../quality/needs.md), [SYS-LOG-01/03/06](../quality/system.md); board:B8/B18/B22/B23 |
| Secure updatability | Signed/encrypted packages, anti-rollback, two update paths | [SYS-UPD-01/06/13](../quality/system.md); board:B36/B38 |

---

## 5. Security architecture: the eight control categories

The guidance (§V.B.1, Appendix 1) groups security controls into eight
categories. This is the core mapping; each row cites the LiteForth
requirement(s) and the strongest piece of evidence.

### 5.1 Authentication

The guidance asks for cryptographically strong authentication of personnel,
messages, commands and updates; authentication before any firmware/software
update; no hardcoded or default passwords; anti-replay measures such as
nonces; and -- explicitly -- **"Do not rely on cyclic redundancy checks
(CRCs) as security controls."**

- Firmware and application updates are authenticated by an **Ed25519
  release signature** over the package, with the release key certified by an
  offline root key; the board checks it before installing
  ([SYS-UPD-01](../quality/system.md), [encrypted-blocks.md](encrypted-blocks.md)).
  Board evidence: a tampered signature, image hash, machine-mode root
  signature or content block is refused (board:B37-battery via
  [hardness.py](../../scripts/admin/hardness.py), [SYS-TOOL-07](../quality/system.md)).
- **No passwords exist on the device**; there is nothing hardcoded or
  default to guess. Per-board keys are made on the board from its RNG at
  provisioning ([SYS-UPD-09](../quality/system.md),
  [security-plan.md §5.4](security-plan.md)).
- **Anti-replay:** each package carries a fresh content-key nonce, and the
  anti-rollback minimum stops an old signed package from being re-installed
  ([SYS-UPD-06](../quality/system.md); board:B38).
- **CRCs are never a security control.** CRC-16 is used only by the YMODEM
  transport to catch line errors; integrity and authenticity rest on
  BLAKE2s hashes and the Ed25519 signature, never on a CRC.

### 5.2 Authorization

The guidance asks for least privilege. LiteForth enforces it in hardware and
in the interpreter:

- **Security levels** 0/1/2 are one build setting; a production board is
  level 2 ([SYS-SEC-07](../quality/system.md)). At level 2 the application
  runs in **user mode** behind the PMP, which denies it stage 1's secrets
  and the V3F's RAM, and gives it read-only or no access to the sensitive
  peripherals ([SYS-SEC-01](../quality/system.md),
  [SYS-SEC-02](../quality/system.md); board:B22/B23).
- **The interpreter reaches only allowed peripherals** through the guarded
  I/O page ([SYS-VM-03](../quality/system.md)).
- **DMA only through Guarded DMA**, which checks every buffer and burns the
  channel after each transfer ([SYS-SEC-03](../quality/system.md); board:B33).
- **User mode cannot steer machine mode**, reach the interrupt controller,
  or run a bus master ([SYS-SEC-11/13/15](../quality/system.md);
  board:B29/B31/B33).
- **A restricted start-up wordlist** limits a locked board's terminal to
  safe words until an authenticated unlock ([SYS-SEC-10](../quality/system.md)).
  **Gap:** this requirement is still *planned* -- see
  [§11](#11-requirement-impact-what-the-fda-framing-changes).

### 5.3 Cryptography

The guidance asks for strong, standard cryptography of adequate security
strength. LiteForth uses:

- **Ed25519** (signatures) and **X25519** (per-board key wrapping) from
  Monocypher 4.0.2, a widely reviewed constant-time library
  ([sbom.md](sbom.md)).
- **BLAKE2s** (RFC 7693) for all integrity hashes and MACs.
- **AES-128** and **SM4** in CTR mode for package and recording encryption,
  on the chip's ECDC engine with a software fallback, the engine checked once
  against software ([SYS-SEC-13](../quality/system.md); board:B27/B33).
- **Keys only from a healthy RNG:** the RNG's error flags are checked and a
  health test runs before a key is made ([SYS-SEC-12](../quality/system.md)).
  **Gap:** this requirement is *planned*; RNG quality on real parts is still
  to be sampled ([security-plan.md §5.4](security-plan.md)).

### 5.4 Code, data and execution integrity

The guidance asks that only authentic code run and that data integrity be
verifiable.

- **Only signed images start at level 2** (`SEC_SIGNED_BOOT`): stage 1
  checks the installed package's block-0 signature and the image hash at
  every boot, so the boot does not rest on a device MAC alone
  ([SYS-SEC-06](../quality/system.md); board:B25).
- **The machine-mode region carries a root signature**, checked at boot, so
  the most privileged code is the root's ([SYS-SEC-14](../quality/system.md);
  board:B32).
- **Protected stores fault into Forth** rather than silently succeeding
  ([SYS-SEC-08](../quality/system.md); board:B23).
- **Flash pages are written only through a checked machine-mode service**;
  at level 2 `open-flash`/`close-flash`/`save-pages` are refused
  ([SYS-STO-07](../quality/system.md); board:B39).
- **An interrupted install can't pass for a good one**
  ([SYS-STO-06](../quality/system.md)); the installed version is recorded
  and reported ([SYS-UPD-10](../quality/system.md)).
- **Recordings and blocks are checked when read**, so NOR/card content is
  never trusted for being present ([encrypted-blocks.md](encrypted-blocks.md),
  [SYS-LOG-02](../quality/system.md)).

### 5.5 Confidentiality

- **Read protection (RDP)** keeps the boot ROM, the debugger and
  RAM-resident code from reading or reprogramming flash; removing it erases
  user flash first, so a physical attacker can wipe but not extract
  ([security-plan.md §3](security-plan.md); board:B8/B18/B22/B23,
  [UN-09](../quality/needs.md)).
- **No product-wide secret:** each package's content key is wrapped for each
  board's own X25519 key, so one extracted board's keys unlock nothing else
  ([encrypted-blocks.md](encrypted-blocks.md), [SYS-UPD-01](../quality/system.md)).
- **Recordings are confidential and unlinkable:** encrypted so only the
  data owner's server can read them, with the board's UID kept off the card
  ([SYS-LOG-01/03/06](../quality/system.md)).

### 5.6 Event detection and logging

The guidance asks that a device detect and log security-relevant events and
make them available for forensics.

- The V3F prints a **boot report** on the debug port: the security level,
  the installed version, whether the board is locked down, and the PMP
  configuration ([board-tests.md](board-tests.md), seen in B35-B39). Update
  outcomes (installed, confirmed, rolled back, refused and why) and a
  power-loss "crumbs" count are reported too.
- **Gap (the weakest category).** There is no persistent, queryable
  security-event log: the boot report is transient on a debug UART, and
  there is no record of failed unlock attempts, refused updates or tamper
  indications that a technician or server could later read. See
  [§11](#11-requirement-impact-what-the-fda-framing-changes) for the
  recommended new requirement.

### 5.7 Resiliency and recovery

The guidance asks that a device maintain or restore a safe state.

- **Never-brick update:** every install runs on trial with an encrypted
  backup on the card; an image that doesn't confirm within its trial boots
  is rolled back ([UN-07](../quality/needs.md),
  [SYS-UPD-03](../quality/system.md); board:B4/B37).
- **Power-cut safe:** an install is resumable and an interrupted one never
  passes for good ([SYS-UPD-04](../quality/system.md),
  [SYS-STO-06](../quality/system.md)).
- **Recovery over USB:** a board with no working image runs a recovery
  terminal that accepts a signed package ([SYS-UPD-07](../quality/system.md);
  board:B35).
- **The backup can't be read or changed** by an attacker
  ([SYS-UPD-12](../quality/system.md)).
- **Recordings survive a power cut:** at most the interval being written is
  lost ([SYS-LOG-04](../quality/system.md)).

### 5.8 Firmware and software updates (updatability / patchability)

The guidance asks for authenticated, timely updates and a patching
capability.

- **Signed and encrypted packages** install over two paths -- a service
  USB (YMODEM) and an `update.lfu` on the card that stage 1 installs at boot
  if it is newer ([SYS-UPD-01](../quality/system.md),
  [SYS-UPD-13](../quality/system.md); board:B36).
- **Anti-rollback:** a package can raise a sticky version floor; an older
  signed package is then refused ([SYS-UPD-06](../quality/system.md);
  board:B38).
- **Provisioning** turns blank boards into products and installs the current
  release without any private key on the station ([UN-14](../quality/needs.md),
  [SYS-TOOL-05](../quality/system.md), [provisioning.md](provisioning.md)).
- The field patch cadence and support period are policy, collected in the
  cybersecurity management plan ([§9](#9-transparency-labeling-and-the-management-plan)).

---

## 6. Security architecture views

The guidance (§V.B.2) asks for four views.

- **Global system view.** The board (V3F root of trust + USB bridge; V5F
  application in user mode; internal flash; microSD) connects only to a
  **local service PC** over USB and to the **data owner's server** indirectly
  -- recordings are carried on the removable card, never streamed over a
  network. The **provisioning station** and the **offline signing root**
  touch the board only in the factory. Diagram source: the tables in
  [security-plan.md §5](security-plan.md) and
  [firmware-updates.md](firmware-updates.md).
- **Multi-patient harm view.** Limited by construction: the device has no
  network, so a compromised unit cannot reach any other unit, and per-board
  key wrapping means one board's extracted secrets unlock nothing else. The
  only shared asset is the vendor signing key, which lives offline in an HSM
  ([security-plan.md §6](security-plan.md)); its compromise is the one path
  to fleet-wide harm and is addressed by key custody and the rotation plan.
- **Updateability / patchability view.** The full path: vendor build →
  offline root certifies a release key → `sealblocks.py` packages and signs →
  the package reaches a board as `update.lfu` on a card or by YMODEM over USB
  → stage 1 checks signature, recipient, version and hashes → install on
  trial → confirm or roll back ([firmware-updates.md](firmware-updates.md),
  [SYS-UPD-01..13](../quality/system.md)). No part of the path is outside the
  manufacturer's control except the physical card and service PC, both
  treated as untrusted.
- **Security use-case views.** Update (USB and card), provisioning,
  recording-to-card, and service/recovery, each worked through in the
  referenced docs.

---

## 7. Cybersecurity testing

The guidance (§V.C) asks for requirement and threat-mitigation testing,
vulnerability testing, penetration testing (with tester independence and
scope stated), fuzz testing, and static/dynamic analysis, continued after
release.

- **Requirement and threat-mitigation testing.** The V-model's unit,
  integration and system tests, plus the board tests
  ([board-tests.md](board-tests.md)); every requirement traces to its tests
  ([rtm.md](../quality/rtm.md)).
- **Penetration / hardness testing.** [hardness.py](../../scripts/admin/hardness.py)
  is a turnkey root-of-trust hardness test a lab runs against a locked board:
  it reads the locked state and throws a battery of corrupted packages,
  checking each is refused and the good image still boots, without any
  private key ([SYS-TOOL-07](../quality/system.md); board:B34 on the
  simulator, and a board run with GOOD/HANG packages). Its report states
  tester scope and what is out of scope (physical and key-holding attacks).
  This session's manual pentest also exercised rollback (B37) and
  anti-rollback (B38) on hardware.
- **Fuzz testing.** The exFAT/card reader parses untrusted data and is
  fuzzed on the desktop build with malformed card images
  ([security-plan.md §5.5/§9](security-plan.md), `unit_tests/exfat`).
- **Static/dynamic analysis.** The core builds are warning-clean and gated
  in CI ([SYS-QA-01](../quality/system.md)). **Gap:** the desktop `lf` build
  does not yet fail on a warning (no `-Werror`), and no dedicated SAST/DAST
  tool is wired in yet.
- **Post-release.** The hardness test and the board-test log are the basis
  for re-testing at a risk-based cadence named in the management plan.

---

## 8. Software bill of materials (SBOM)

The guidance (§V.A.4) and Section 524B(b)(3) call for a machine-readable
SBOM of commercial, open-source and off-the-shelf components, with each
component's support level and end-of-support date, and known vulnerabilities
identified.

LiteForth ships an SPDX 2.3 SBOM, generated from the source and checked in CI
so a drifted file fails the tests ([sbom.spdx.json](../../sbom.spdx.json),
[sbom.md](sbom.md), [SYS-QA-02](../quality/system.md)). It covers Monocypher,
the tracked BLAKE2s, WCH's EVT/SDK, Unity, the Noto fonts and the Python
packages, each with its license and file checksums.

**Gap:** the SBOM does not yet carry the **support level and
end-of-support/EOL date** per component, nor a mapping of known
vulnerabilities (NVD / CISA KEV) to components. See
[§11](#11-requirement-impact-what-the-fda-framing-changes).

---

## 9. Transparency: labeling and the management plan

**Labeling (§VI.A).** For this device the relevant labeling items are: the
ports-and-interfaces table ([§1](#1-the-device-and-its-ports)); the SBOM,
kept current for users ([§8](#8-software-bill-of-materials-sbom)); how to
obtain and install vendor-authorized, version-identifiable updates
([firmware-updates.md](firmware-updates.md)); the secure shipped
configuration (level 2, locked down) and which changes raise risk
(unlocking to level 0); how the device reports security-relevant events
([§5.6](#56-event-detection-and-logging)); and **secure decommissioning** --
removing read protection erases all keys and user flash, so a retired board
reveals nothing ([security-plan.md §3](security-plan.md)). Backup/restore of
recordings is the data owner's server-side concern.

**Cybersecurity management plan (§VI.B).** The guidance asks for a plan
naming the responsible personnel, the sources and cadence for monitoring
vulnerabilities (NVD, WCH advisories, upstream crypto libraries), the
handling of CISA KEV entries, the patch-development and field-deployment
timelines, the update/patching capability, a **coordinated vulnerability
disclosure (CVD)** process, and how remediations reach customers.
[security-plan.md §6/§8/§10](security-plan.md) lists these as policies to
write (key management, release/signing, vulnerability disclosure with a
`security.txt` contact, a declared support period, incident response).

**Gap:** the CVD process, the declared support period, and the monitoring
cadence are planned but not yet formalized. These are process documents, not
code; they are the main non-code work a medical-device filing would add.

---

## 10. Section 524B ("cyber device") status

Section 524B applies to a "cyber device" -- in the statute, a device that
(among other criteria) includes software and **has the ability to connect to
the internet**. LiteForth as built has **no network interface of any kind**
([§1](#1-the-device-and-its-ports)); its only external paths are a local
service USB and a removable microSD card. On that basis the product as
currently ported likely falls **outside** the strict statutory "cyber
device" definition, and the mandatory 524B package (a postmarket
vulnerability-management plan, a coordinated-disclosure process, and an SBOM
as a condition of clearance) would not be triggered by the statute alone.

This does not remove the cybersecurity expectations: the guidance still asks
for cybersecurity documentation **scaled to risk** for any device with
software, and a recorder of patient audio has real confidentiality and
integrity stakes. The design meets most of that already
([§5](#5-security-architecture-the-eight-control-categories)); the SBOM and a
management plan ([§8](#8-software-bill-of-materials-sbom),
[§9](#9-transparency-labeling-and-the-management-plan)) are the pieces to
finish regardless of the 524B trigger.

**If a networked variant is ever built** (adding Wi-Fi, BLE, cellular or a
wired network path, or internet-reachable update/telemetry), the device
becomes a cyber device: 524B then applies in full, the remote attack surface
and multi-patient-harm view reopen, and the additional EU/RED drivers in
[security-plan.md §2](security-plan.md) come into force. That change should
be treated as a new threat model, not an increment.

---

## 11. Requirement impact: what the FDA framing changes

The user asked whether the medical-device focus should loosen or tighten the
existing requirements. It does both. These are recommendations, to adopt
into [doc/quality/](../quality/) deliberately, not yet applied.

### Tighten / add

- **Event detection and logging (new requirement).** The weakest control
  category ([§5.6](#56-event-detection-and-logging)). Recommend a new
  `SYS-LOG` or `SYS-SEC` requirement for a persistent, tamper-evident
  security-event record (failed unlocks, refused updates, rollbacks, boot
  integrity results) that a technician or the server can read, and a parent
  need for it. This is the clearest FDA-driven addition.
- **SBOM support status and EOL (tighten SYS-QA-02).** Add a support level
  and end-of-support date per component, and a periodic check of components
  against the NVD / CISA KEV.
- **Cybersecurity management plan and CVD (new need + policy).** A declared
  support period, a coordinated vulnerability disclosure contact and
  process, and a patch cadence ([§9](#9-transparency-labeling-and-the-management-plan)).
  Promote [security-plan.md §6/§8](security-plan.md) from "to write" to owned
  documents.
- **Finish the planned controls that the framing makes load-bearing:** the
  restricted start-up wordlist ([SYS-SEC-10](../quality/system.md)) and the
  RNG health gate ([SYS-SEC-12](../quality/system.md)) are both still
  `planned`; under an authenticity/authorization claim they should be built
  and board-verified.
- **Authentication of the service session.** Updates are signed (strong),
  but terminal access control currently rests on the security level alone;
  the challenge/response unlock ([security-plan.md §5.4/§5.7](security-plan.md))
  should be specified as a requirement if the service USB is a claimed
  control.

### Loosen / scope out (document the rationale)

- **Network and remote-attack controls** (firewalls, TLS, network IDS,
  remote-session management) are **not applicable** while the device has no
  network interface. The guidance scales documentation to risk; record the
  no-network decision so the absence is a justified scope limit, not an
  omission.
- **Multi-patient harm** reduces to single-device harm
  ([§6](#6-security-architecture-views)); the view can be brief with that
  rationale.
- **Availability** as a safety objective is limited: a recorder losing a
  session is a data-loss event, not a life-safety one, which lowers the bar
  on availability controls relative to a therapy-delivering device.

### Housekeeping (the standing invitation to prune)

- Several `SYS-SEC` items (01, 02, 11, 13, 14, 15) together make one claim --
  "user-mode code cannot reach the root of trust's secrets or subvert
  machine mode." They are correctly separate requirements with distinct
  evidence, so no merge is recommended; but the FDA mapping in
  [§5.2](#52-authorization)/[§5.4](#54-code-data-and-execution-integrity)
  should be the single place that presents them as one control story.

---

## 12. Traceability

The guidance expects each security control to trace to a requirement and to
its verification evidence. LiteForth's Requirements Traceability Matrix
already provides this end to end ([rtm.md](../quality/rtm.md),
[SYS-QA-04](../quality/system.md)), and the board-test log supplies the
hardware evidence with verbatim output ([board-tests.md](board-tests.md),
[SYS-QA-03](../quality/system.md)). The per-category tables in
[§5](#5-security-architecture-the-eight-control-categories) are the index
from the FDA control categories into that matrix.

---

## References

- FDA, *Cybersecurity in Medical Devices: Quality Management System
  Considerations and Content of Premarket Submissions*, docket
  FDA-2021-D-1158 (February 2026; nonbinding recommendations):
  <https://www.fda.gov/media/119933/download>
- AAMI TIR57, *Principles for medical device security -- risk management*;
  ANSI/AAMI SW96, *Medical device security -- security risk management*
- ISO 14971 (safety risk management); ISO 13485 / 21 CFR 820 QMSR
- NTIA, *The Minimum Elements for a Software Bill of Materials*; CISA, *Known
  Exploited Vulnerabilities Catalog*
- LiteForth: [security-plan.md](security-plan.md),
  [firmware-updates.md](firmware-updates.md),
  [encrypted-blocks.md](encrypted-blocks.md), [data-logs.md](data-logs.md),
  [provisioning.md](provisioning.md), [sbom.md](sbom.md),
  [board-tests.md](board-tests.md), and the V-model
  ([doc/quality/](../quality/))
