# Product security plan: LiteForth on CH32H417

Draft outline for planning, 2026-10-15. It covers flash image signing, the
root of trust, unique keys and secure firmware update, and what they need
from policies, provisioning computers and key management.

Items marked **Decide** are open; **Verify** marks things to check on real
parts before relying on them. Section 10 collects them.

---

## 1. Scope

In scope:

- The CH32H417 board running LiteForth (V5F core) with the V3F USB CDC
  bridge, its internal flash, and the SD card block partition.
- The firmware image: the root of trust (RoT), LiteForth, and the Forth
  application in flash pages 0-3.
- Content loaded at run time: SD card blocks (source, fonts, data).
- The update path: USB CDC (kwai, XMODEM) and the SD card.
- The factory: provisioning computers, signing, the device database.

Out of scope for now (say so in the threat model): invasive attacks (decap,
probing), power/clock glitching, side channels on the AES engine. The
CH32H417 isn't a secure element and doesn't claim resistance to these.

## 2. What's driving the requirements

Confirm which apply; each one adds documents and process, not only code.

| Driver | Applies if | Dates / notes |
|---|---|---|
| EU Cyber Resilience Act (CRA) | Product sold in the EU with a digital element | Vulnerability and incident reporting applies from 11 Sep 2026; full application (essential requirements, technical file, CE marking) from 11 Dec 2027 |
| EU RED delegated act, EN 18031-1/-2/-3 | Product has a radio (Wi-Fi, BLE, cellular...) | In force for radio equipment since 1 Aug 2025 |
| ETSI EN 303 645 | Consumer IoT | Baseline many schemes reference: no default passwords, update, vulnerability disclosure |
| IEC 62443-4-1 / 4-2 | Industrial customers ask for it | Secure development process and component requirements |
| US Cyber Trust Mark (FCC) | Voluntary label for US consumer IoT | Based on NIST IR 8425 |
| Customer contracts | Always check | Often ask for signed updates, SBOM, a support period |

Common thread in all of them: unique credentials (no shared default
secrets), authenticated updates, a declared support period, a
vulnerability handling process, and documentation of all of it.

## 3. What the CH32H417 gives us

From the reference manual (CH32H417RM V1.8) and datasheet.

| Feature | Detail | Use |
|---|---|---|
| Read protection (RDPR option byte) | 0xA5 = off; any other value = on. On: the boot ROM, the debugger (SDI/SWD) and code running from RAM can't read, erase or program main flash | Keeps keys and code from being read out |
| Removing read protection | Writing RDPR = 0xA5 erases all of user flash first | A physical attacker can wipe and reflash, but can't extract secrets |
| Auto write protect under RDP | The first 8 KB (DBMODE=1) is write-protected while RDP is on, whatever WRPR says | Hold the first stage of the RoT here |
| Write protection (WRPR0-3) | 8 KB units (4 KB with DBMODE=0); clearing it requires removing RDP, so a wipe | Protect the whole RoT region |
| Boot ROM downloads | USER bits USARTDLEN and USBFSDLEN enable the built-in bootloader's USART and USB download | Turn both off in production |
| Unique ID (ESIG) | 96 bits at 0x1FFFF7E8, read-only, readable by any code and by the debugger | An identifier and serial number, **not a secret** |
| ECDC crypto engine | AES-128/192/256 and SM4, ECB and CTR only; DMA memory-to-memory | Image encryption (CTR), AES-CMAC built from ECB |
| RNG | 32-bit, from analog noise through an LFSR; reports clock and seed errors | Key generation, with conditioning (see 5.4) |
| PMP | 4 regions per core | Can fence off key storage from U-mode code |

What it doesn't have, and what that means:

- **No secure boot ROM.** Nothing in silicon checks our first instruction.
  The RoT is our own code, made unchangeable only by RDP plus write
  protection.
- **No fuse to disable debug.** The debug port stays usable, but under RDP
  it can only wipe the chip, not read or change flash.
- **No OTP key slot and no hardware-only key.** Device secrets live in
  read-protected flash, readable by any code running on the chip. So code
  running on the chip is the main threat, not the debugger.
- **No SHA or public-key engine.** SHA-256 and signature checks run in
  software. That's fine at 400 MHz, and only matters for boot time.
- **No monotonic counter.** Anti-rollback counters live in flash that the
  RoT controls.

The resulting security property, stated honestly: someone with physical
access can always wipe a device and load their own firmware. They can't
extract its keys, run modified firmware while keeping its identity, or
pass a wiped device off as genuine to our servers. That last point is
why per-device identity keys matter (section 6).

## 4. Threat model

Assets:

- Vendor signing keys (the most valuable: they decide what runs everywhere)
- Per-device keys (identity, the image HMAC key, the admin key)
- Firmware and Forth application code (IP, and integrity)
- Customer data on the device and its SD card
- The device's behavior: it must not run unauthorized code

Adversaries, by reach:

| Adversary | Can | Main defenses |
|---|---|---|
| Remote, over the product's interfaces | Send input, maybe updates | Signed updates, input limits, no debug features in production |
| Holder of the device | USB terminal, SD card, buttons | Security levels and challenge-response, signed/HMAC'd blocks, RDP |
| Lab attacker (non-invasive) | Debug probe, swapping the SD card, reflashing | RDP, WRPR, a wipe loses all keys, server-side identity checks |
| Insider at the factory or contract manufacturer | Provisioning station access | HSM, separated duties, audit logs, no raw keys on stations |
| Supply chain | Swapped parts or images | Signed images, attestation at first contact |

## 5. Architecture

### 5.1 Flash layout

LiteForth's current plan ([flashmem.md](flashmem.md)): RoT in sectors 0..N-1, a
security sector N, then four flash pages for the dictionary. A proposal to
pin down (**Decide** sizes):

| Region | Size | Protection | Contents |
|---|---|---|---|
| Stage 0 RoT | 8 KB | Auto WP under RDP | Self-test, signature check, update installer core, recovery |
| RoT / LiteForth core | N x 8 KB | WRPR | LiteForth C, RoT API, vendor public keys |
| Security sector | 8 KB | Not WP; reachable only by RoT code | Device keys, version counters, image HMAC, provisioning record |
| Flash pages 0-3 | 4 x 64 KB | Not WP | Forth application dictionary |
| Update staging | 256 KB, or on the SD card | Not WP | A received image waiting to be checked and installed |

**Decide:** is the RoT ever field-updatable? Recommendation: stage 0
never is. The LiteForth core above it can be updated only through stage 0
and a vendor signature, which leaves room for fixing bugs in C.

### 5.2 Root of trust and the boot sequence

At every reset, stage 0:

1. **Self-test.** Check that RDP is on, WRPR covers the RoT, the boot ROM
   download bits are off, and the security sector holds a provisioned
   record. Anything wrong: stay in a locked-down state (the "development"
   state) and say so on the terminal.
2. **Check the RoT core**, its hash against a value stage 0 holds, if the
   core is updatable.
3. **Check the application.** Fast path: the per-device image MAC (5.3).
   No match: don't launch; go to QUIT at security level 2, as
   [popthehood.md](popthehood.md) already describes.
4. **Recovery override.** A strap pin held at reset skips launching the
   app, so a bad app can't brick the unit. It only skips the launch; it
   never lowers the security level or turns off checks.
5. Launch.

LiteForth already plans most of this (RoT, self-test, HMAC check, recovery
pin). The changes below are the vendor signature in 5.3 and the IO window
in 5.6.

### 5.3 Image signing: two layers

The current design generates the image HMAC on the device with a
device-unique key, at install (`finalize`). That proves "this image was
installed through this device's RoT and hasn't changed since". It doesn't
prove the vendor made the image. A level 0 user can install anything and
it gets a valid HMAC. So production needs two layers:

| Layer | Who makes it | Checked | Algorithm (**Decide**) | Proves |
|---|---|---|---|---|
| Vendor signature | Signing service (HSM), at release | Once, at install, by the RoT | Ed25519 (small, fast, constant-time code available), or ECDSA P-256 if a customer requires NIST curves | The vendor released this exact image, for this product, at this version |
| Device image MAC | The RoT, after it checks the signature | At every boot | HMAC-SHA256, or AES-CMAC using the ECDC engine | The installed flash is still what was checked |

Update package (manifest + payload), a first cut:

```
magic, format version
product ID, hardware revision range
image type: app | rot-core | blocks
image version (monotonic), minimum version allowed to install it
payload length, SHA-256 of the payload
target: any device | one UID (for debug unlocks or per-device images)
encryption: none | AES-CTR (key ID, nonce)
signing key ID
signature over everything above
```

The same scheme covers **SD card blocks**. `load` runs source from blocks,
and the SD card is removable, so it's attacker-controlled. In production,
blocks that will be loaded need a signature (vendor-shipped content) or a
device MAC (content made on the device). The 32 bytes already reserved in
binary blocks (`putbin.py`, [blocks.md](blocks.md)) are a natural home for a MAC.
**Decide:** sign each block, or a manifest that lists block hashes.

### 5.4 Unique keys

Keys on each device:

| Key | Type | Made | Stored | Used for |
|---|---|---|---|---|
| K_img | 256-bit symmetric | On the device at first provisioned boot, from the RNG | Security sector | The image MAC (5.3) |
| Device identity | Ed25519 key pair | On the device during provisioning | Private key in the security sector; public key exported | Attestation, signing challenge responses, TLS/server auth if networked |
| Device certificate | Vendor-CA-signed record: UID + identity public key + model + date | Provisioning station via HSM | Security sector, and the device database | Lets servers and tools check a device is genuine |
| K_admin | Symmetric, or verify with the device identity key | See below | Security sector | `challenge` / `response:` to change security level |
| Vendor public keys | Ed25519 public | Build time | RoT image (WP) | Checking update signatures. Hold 2-3 so a key can be retired |

Rules:

- **Nothing secret is shared between devices.** No fleet-wide passwords
  or keys; the RoT holds only public keys, which aren't secret.
- **Keys made on the device never leave it**, except public halves.
- **The UID is an identifier.** Don't derive secrets from it alone
  (`K = HMAC(master, UID)` is acceptable only if `master` lives in an HSM
  and is used only there).
- **RNG conditioning:** feed several RNG words through SHA-256 for each key
  and check the RNG's error flags. Run a basic health test (stuck output,
  repeated words) at power-up. **Verify** RNG quality on real parts at
  provisioning (collect a sample per lot).

Security-level changes ([popthehood.md](popthehood.md), challenge-response): the
challenge should include the UID, the requested level and a fresh random
nonce. The management server answers with a signature. That's checked
with a vendor *admin* public key in the RoT, so a leaked server database
doesn't give away unlock codes. Every unlock is logged server-side with
who asked and why.

### 5.5 Secure firmware update

Flow (USB CDC with XMODEM, or from the SD card):

1. Receive the package into staging (internal flash or SD). Nothing is
   trusted yet.
2. The RoT checks: format, product and hardware match, signature with a
   known, non-retired key, payload hash, version >= the stored minimum.
3. Decrypt if needed (AES-CTR via ECDC).
4. Write it to its target pages. Mark "installing" in the security
   sector first, so a power failure resumes or rolls back on the next boot.
5. Read back, compute the device MAC with K_img, store MAC + version.
6. Raise the anti-rollback minimum only after the new image boots and
   confirms itself (**Decide** how: a first-boot health check, or
   immediately).

Points to settle:

- **Atomicity.** 960 KB of flash doesn't fit a full A/B copy of the RoT
  plus 4 x 64 KB pages comfortably. The simplest option: stage on the SD
  card, keep the old image until the new one checks out, and allow
  recovery from the SD card staging copy. **Decide.**
- **Anti-rollback** stores a version counter in the security sector, so it
  depends on the RoT being the only writer (5.6).
- **Field compilation vs signed images.** LiteForth can compile to flash
  at level 0, which makes code the vendor never signed. Policy: production
  units boot at level 2, flash compilation needs level 0, and level 0
  needs an authenticated unlock (5.4). A unit that's been unlocked and
  compiled on should say so (a flag in the security sector, shown by
  `attest`) so support can tell.
- **kwai.** `~x` sends files by XMODEM; the RoT does the checking, so kwai
  needs no secrets. kwai could check the signature itself before sending,
  just to catch mistakes early.

### 5.6 Locking down the chip

Production lock-down, done at the end of provisioning (section 7):

- USER: USARTDLEN = 0, USBFSDLEN = 0 (no boot ROM downloads)
- WRPR: every RoT sector
- RDPR != 0xA5 (read protection on), last, then a power-on reset
- Stage 0 checks all of this at every boot (5.2, step 1)

Inside the chip, the VM is the boundary. Any code that runs on the chip can
read the security sector, so the Forth VM must not be able to reach it, or
to reach anything that can:

- **VM memory map:** the security sector and RoT must not be mapped into
  any VM page.
- **VM IO page:** today `VM_IO_PAGE` maps the peripheral registers into
  Forth, all of 0x40000000-0x400383FF (`IO_CELLS` in
  `src/target/CH32H417/V5F/User/options.h`). That includes the FLASH controller (0x40022000),
  DMA1 (0x40020000), ECDC (0x40016C00) and RNG (0x40023C00). In
  production that window must be an allowlist. It must exclude:
  - the FLASH controller (0x40022000): Forth could unlock it, erase pages,
    write the security sector, or set RDPR = 0xA5 and wipe the chip;
  - DMA: a DMA copy could move the security sector into RAM that Forth can
    read;
  - ECDC, while it holds keys;
  - RNG, PWR/RCC and the option bytes.
  **Decide** the allowlist per product (GPIO, timers, LCD SPI, ...).
- **PMP:** if LiteForth's C runs in M mode, PMP can't fence it off from
  itself. It's still useful to lock the security sector against the V3F
  core and anything running in U mode. **Verify** what V3F can reach (shared
  flash?), since the V3F's USB CDC bridge is the first thing that sees
  host input.
- **API 0 audit:** C words reachable from Forth (`api0_list.h`) must not
  expose raw flash writes, memory reads outside the VM, or key material.
  `open-flash` / `close-flash` stay behind the security level.

### 5.7 Terminal access: a safe wordlist at startup

Goal: a production unit's terminal can do only safe things until someone
authenticates. It boots at security level 2 with a small "safe" wordlist,
and only an authenticated unlock gives it the `forth` wordlist.

Levels (refining [popthehood.md](popthehood.md)):

| Level | The interpreter finds | Can |
|---|---|---|
| 2, locked (the boot default) | The safe wordlist and the app's command wordlist; numbers | Run safe words and the app's public commands |
| 1 | Everything | Interpret and compile to RAM; `open-flash` refused |
| 0 | Everything | Everything, including `open-flash` and `finalize` |

Why not just `only`: LiteForth's root wordlist, which `only` leaves in the
search order, holds `forth`, so typing `forth` would restore everything.
The safe wordlist is a third built-in wordlist, next to `forth` and root,
and it has no way out.

What's in it (**Decide** the final list):

| Word | Does |
|---|---|
| `challenge` ( level -- ) | Prints a challenge for an unlock to that level (5.4) |
| `response:` ( "hex" -- ) | Checks the signed response; on success, sets the level |
| `lock` ( -- ) | Back to level 2. Never needs authentication |
| `attest` ( -- ) | Prints the UID, device certificate, image versions and unlock count; signs a nonce on request |
| `.version`, `words`, `help` | Information only |

The app's commands: a signed app can add words to its own `commands`
wordlist (built into the image and saved with `save-wids`), so a product
can offer, say, `5 set-speed` at level 2. These words follow the rules
below, and they're part of the signed image, so a user can't add any.

Enforcement in C, not only by the search order. `CONTEXT` lives in VM RAM
and the constant `context` gives its address, so the search order is a
convenience, not a boundary. At level 2, `interpretSource`:

1. Searches only the safe and `commands` wordlists, whatever `CONTEXT`
   holds.
2. Forces `STATE` to 0 for every line, so nothing compiles.
3. Skips the constants table (`context`, `state`, `dp[]`, ...). They're
   harmless without `!`, but they describe the memory layout.
4. Still accepts numbers, in decimal, so commands can take arguments.

Rules for words reachable at level 2, checked in review:

- No `execute`, `evaluate`, `load`, `'`, or anything else that runs code
  chosen at run time; no address or xt arguments; no raw memory access.
- Check every numeric argument's range, and fail with an error, not a
  wrap-around.
- A unit test lists the reachable set and fails when it changes, like
  `unit_tests/api0`'s `expected.txt`, so adding a word is a deliberate,
  reviewed change.

Unlocking:

- Only through `challenge` / `response:`, signed by the admin key, bound to
  the UID, the level and a fresh nonce (5.4).
- Failed responses: a growing delay and a limit per boot.
- The level is volatile: a reset (including the Ctrl+X restart through the
  V3F) returns to level 2. **Decide** an idle timeout that re-locks.
- Each unlock bumps a counter in the security sector, which `attest`
  reports, so support can see a unit has been opened.

Other points:

- The prompt stays `ok>` at every level (kwai paces and spots errors on
  it). `attest` or a `level` word says which level is in force.
- The running app (`cold`, `stop-tib`) is signed code with full VM rights.
  If it reads the keyboard itself, its own command parser follows the same
  rules.
- The desktop build keeps level 0 by default so development is unchanged;
  an `lf` option boots it locked for tests. A script-driven test suite
  (like `unit_tests/quit`) checks that at level 2 `forth`, `context`,
  `: x ;`, `1 2 +` and `0 load` all fail, that `lock` works, and that a
  wrong `response:` is refused.

## 6. Key management

Hierarchy:

```
Offline root CA (HSM, kept offline, used rarely)
 ├── Firmware signing key(s)   ─ sign releases (online HSM, 2-person rule)
 ├── Admin / unlock key        ─ signs security-level unlock responses
 └── Manufacturing CA          ─ signs device certificates at provisioning
        └── Device identity keys (one per unit, made on the device)
```

| Topic | Plan |
|---|---|
| Where keys live | HSMs (network HSM or YubiHSM 2 class for a small team; cloud KMS is an option for the signing key). Never in files, CI variables or laptops |
| Dev vs production | Separate key sets. Dev units trust only dev keys. A production unit never accepts a dev-signed image |
| Who may sign | Named people; releases need two (one builds, one approves). CI produces the build; signing is a separate, logged step |
| Reproducibility | Builds are reproducible (gcc output already is), so a reviewer can rebuild and compare before signing |
| Rotation | Each RoT ships 2-3 vendor public keys; retire one with a signed "revoke key ID" update. Plan the rotation schedule (**Decide**) |
| Compromise | Written response plan: revoke, re-sign with the next key, push update, notify per CRA timelines (24 h early warning, 72 h notification) |
| Backup | Root CA key backed up in split form (M-of-N), in separate places |
| Records | Every signature, unlock and certificate issued is logged in an append-only log |

## 7. Provisioning

Station: a dedicated computer per line, running only the provisioning
tool, with the HSM (or a link to it), the programmer, and a USB connection
to the board. Locked down: no general internet, signed tool releases,
operator login, all actions logged. **Decide:** in-house only, or at a
contract manufacturer (then the station and HSM are ours, shipped and
managed remotely).

Flow per unit:

1. Program the production RoT + app image (unlocked chip, debug probe or
   boot ROM). The image has no secrets in it.
2. Functional test.
3. The RoT, on first boot in its "unprovisioned" state:
   - checks the RNG, generates K_img and the device identity key pair;
   - reports the UID and identity public key to the station.
4. Station: checks the UID isn't already in the database, has the
   manufacturing CA (in the HSM) sign the device certificate, sends it to
   the device.
5. The device stores the certificate, computes the image MAC, marks itself
   provisioned.
6. Lock down (5.6): USER bits, WRPR, then RDP, then a power-on reset.
7. Verify: the unit boots, answers an `attest` challenge with a signature
   that checks against its certificate, and a debug probe can't read flash.
8. Record: UID, certificate, image versions, station, operator, time,
   test results.

Failure handling: a unit that fails after step 3 gets wiped (removing RDP
erases its keys) and starts again; its UID is marked in the database.
**Decide:** whether RMA units get re-provisioned with new keys (yes, by
default) and the old certificate revoked.

## 8. Policies and documents to write

- Key management policy (section 6)
- Code signing and release policy: who, two-person rule, what gets signed
- Provisioning SOP and station configuration (section 7)
- Security levels and unlock policy: who may unlock, why, logged how
- Vulnerability disclosure policy and a contact (`security.txt` on the
  website), handling process with CRA reporting timelines
- Support period: how many years of security updates per product (CRA
  requires declaring it)
- SBOM for each release: LiteForth, the WCH SDK and drivers, any crypto
  library
- Secure development: code review, static analysis, test on the threat
  model, dependency updates
- Incident response, including a key-compromise runbook
- Customer-facing security documentation (how to update, how to verify a
  device is genuine)

## 9. Testing and verification

- On a locked unit: a debug probe can't read flash; boot ROM downloads are
  off; removing RDP wipes everything (and the wiped unit fails `attest`).
- Updates: wrong signature, wrong product, older version, truncated
  payload, corrupted byte, retired key: all refused, and the unit still
  runs.
- Power loss at each update step: the unit recovers.
- Forth can't reach the FLASH controller, DMA, ECDC or security sector
  through the IO page or any API 0 word (a test script that tries).
- SD card swap: unsigned or modified blocks don't `load` at level 2.
- RNG health test runs and catches a stuck source (simulate).
- Regression tests for all of this in LiteForth's `make test` on the
  desktop build where possible (simulated flash and security sector), and
  a hardware-in-the-loop script driven over kwai.

## 10. Open decisions and things to verify

1. Which regulations and standards apply (section 2), and the support period.
2. Signature algorithm: Ed25519 or ECDSA P-256; which library.
3. Image MAC: HMAC-SHA256 or AES-CMAC on the ECDC engine.
4. Is the RoT core field-updatable, and how is stage 0 sized.
5. Update atomicity: SD card staging, or internal staging space.
6. When to raise the anti-rollback minimum.
7. SD block authentication: per block, or a signed manifest.
8. The VM IO page allowlist for each product.
9. Provisioning in-house or at a contract manufacturer; HSM choice.
10. Key rotation schedule; how many vendor public keys the RoT holds.
11. **Verify:** how parts ship (the manual lists RDPRT's reset value as 1;
    check whether new parts arrive read-protected).
12. **Verify:** whether the debugger can still read the UID and option
    bytes on a protected part (the manual says the UID is readable over
    SDI).
13. **Verify:** whether the ECDC key registers can be read back; clear
    them after use either way.
14. **Verify:** what the V3F core can access, and whether it shares flash
    and the security sector with the V5F.
15. **Verify:** RNG output quality on real parts.
16. The safe wordlist's contents, the idle re-lock timeout, and the
    limit on failed unlocks (5.7).

## 11. Suggested order of work

1. Decide 1, 2, 4, 8 (they shape everything else).
2. Desktop build: security levels and the safe wordlist (5.7), then the
   security sector simulation, manifest format, signature check, image
   MAC, tests. Fast to iterate, no hardware needed.
3. CH32H417: stage 0 RoT, lock-down self-test, IO page allowlist.
4. Signing tool and key ceremony (dev keys first).
5. Provisioning tool and station, a pilot run on a few boards.
6. Update path end to end: kwai, SD card, recovery.
7. Policies, SBOM, vulnerability process, technical file.

## References

- WCH, *CH32H417 reference manual* V1.8 (Chinese): ch. 39 ESIG, ch. 41
  ECDC, ch. 46 FLASH and option bytes:
  <https://www.wch.cn/downloads/CH32H417RM_PDF.html>
- WCH, *CH32H417/H416/H415 datasheet* V1.5:
  <https://www.wch-ic.com/downloads/CH32H417DS0_PDF.html>
- LiteForth: [popthehood.md](popthehood.md) (RoT, security levels, boot sequence),
  [flashmem.md](flashmem.md) (flash layout), [blocks.md](blocks.md) (block HMAC space)
- CRA timeline: <https://finitestate.io/blog/cyber-resilience-act-timeline-2026-2027>
- RED cybersecurity (EN 18031): <https://www.tuvsud.com/en-us/services/product-certification/red-cybersecurity-requirements>
