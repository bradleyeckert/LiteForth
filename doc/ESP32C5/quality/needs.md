# Stakeholder needs (UN) — ESP32-C5

The top of the ESP32-C5 V's left side: what the people who build, ship, use
and service an ESP32-C5 LiteForth product need from it, in their terms. Each
need is refined by system requirements ([system.md](system.md)) and, once a
board exists, validated on it ([board-tests.md](../admin/board-tests.md)) or
by a demonstration. [README.md](README.md) explains what differs from the
CH32H417 V, whose [README](../../CH32H417/quality/README.md) holds the
format.

The stakeholders: the **developer** (writes the Forth application), the
**vendor** (releases and supports the product, holds the signing keys), the
**operator** (uses a device in the field), the **service technician**
(updates and recovers devices), and the **auditor** (assesses the product's
security for a regulator or a customer). The security needs (UN-04..UN-07)
are the user's standing direction: cybersecurity built in from the start, to
FDA premarket best practice, now that automated agents lower the cost of an
attack.

### UN-01: Interactive Forth on the ESP32-C5 board
The developer writes and tests Forth on the ESP32-C5 itself, over the
board's USB-UART console: typing words, compiling into flash, loading
source, and seeing errors where they happen.

- **Status:** planned
- **Gap:** no board run logged

### UN-02: The same system as the other targets
The developer runs the same Forth — the same words, storage and update
semantics — as on the CH32H417 and the desktop, so the application and the
tests carry over. The shared `src/*.c` core is reused unchanged behind the
target's drivers.

- **Verified by:** demo:scripts/regression.f::T{, demo:src/target/desktop/unit_tests/exfat::run_lf: lf finds lfblocks.fb4
- **Status:** partial
- **Gap:** the shared core is demonstrated on the desktop; no ESP32-C5 board run logged

### UN-03: Updates that never brick a device
A field device takes a new firmware image, runs it on trial, and falls back
to the previous working image if it fails — never ending up unbootable — and
refuses an older, withdrawn image.

- **Status:** planned
- **Gap:** OTA + rollback not yet built; no board

### UN-04: Only the vendor's firmware runs
The device boots and runs only firmware the vendor signed; a modified or
substituted bootloader or application is rejected before it runs.

- **Status:** planned
- **Gap:** secure boot configured in the build overlay but not burned; no board

### UN-05: Firmware and secrets can't be extracted
An attacker with the board in hand can't read out the firmware/IP or the
device's secrets over the debug, download or serial paths, and the keys
stay in hardware. (Physical and private-key attacks are out of scope, as
for the CH32H417.)

- **Status:** planned
- **Gap:** flash encryption, download-mode close and key storage configured but not burned; no board

### UN-06: Stays defensible against automated attack in the field
The product is built to a hardened baseline that doesn't quietly regress,
with the third-party platform (ESP-IDF) kept patchable and monitored, so an
automated attacker doesn't find a cheap way in after release.

- **Status:** planned
- **Gap:** baseline enforced at build (UN-07 evidence) but SOUP monitoring and field update not yet built

### UN-07: Evidence for a security assessment
The auditor can see what the product must do, how it's built to do it, and
how each security control was checked — to FDA premarket best practice:
a security plan, a traceable V-model, automated tests, and an SBOM of the
SOUP it carries.

- **Verified by:** demo:src/target/desktop/unit_tests/seccheck::compliant config: production passes
- **Status:** partial
- **Gap:** the plan, this V-model and the automated config gate exist; the control-by-control FDA mapping and the SBOM are still to come

### UN-08: Wireless connectivity
The application can use the chip's Wi-Fi 6 and Bluetooth LE through the
platform, rather than a bespoke stack — the reason this target is an
ESP-IDF application at all.

- **Status:** planned
- **Gap:** not yet brought up

### UN-09: Encrypted, authenticated recordings only the data owner can read
A main application: the device records CODEC audio and other data streams to
the microSD card so that only the data owner's server can read them, the
server can prove which board wrote a recording and that nothing was changed
or reordered, and the board's identity never appears in the clear on the
card. The format uses the ESP32-C5's cryptographic hardware.

- **Verified by:** demo:scripts/CH32H417/admin/lflog.py::server-key, demo:src/target/desktop/unit_tests/datalog::round trip
- **Status:** partial
- **Gap:** the shared log format is proven on the desktop; the C5 hardware-crypto backend, CODEC capture and a board run are not built

### UN-10: A security-event log the device keeps and the server can read
The cybersecurity system records penetration attempts and other
security-relevant events (refused updates, rollback, boot-integrity failures,
download/JTAG access attempts, tamper, console anomalies) to the card in the
same encrypted, authenticated format, so an attacker can neither read nor
forge the record of their own attempts, and the server can read and verify it
with the same tool.

- **Status:** planned
- **Gap:** not built; depends on the log subsystem and the security write points
