# System requirements (SYS) — ESP32-C5

What LiteForth on the ESP32-C5, as a whole product, must do: each
requirement refines a stakeholder need ([needs.md](needs.md)), is satisfied
by architecture elements ([architecture.md](architecture.md)), and is
verified by a test or, where no test can yet run, by inspection or the CI
security gate. [README.md](README.md) explains what differs from the
CH32H417 V; its [README](../../CH32H417/quality/README.md) explains the
evidence kinds.

"The board" is the NM-CYD-C5 ESP32-C5 target, built as an ESP-IDF
application; "lf" is the desktop build that exercises the shared core. The
security baseline is `doc/ESP32C5/admin/security-plan.md` section 7.1,
turned into a build gate by `scripts/ESP32C5/seccheck.py`. "Production" and
"dev" are that gate's two profiles.

## CORE: the Forth system

### SYS-CORE-01: The shared Forth core, unchanged
The interpreter, compiler, VM, blocks, sealed format and crypto are the
shared `src/*.c`, compiled for the ESP32-C5 behind the target's drivers
with no source change — the same behavior the desktop and CH32H417 builds
have.

- **Parent:** UN-02
- **Verified by:** demo:scripts/regression.f::T{, demo:src/target/desktop/unit_tests/sealed::stage_tests
- **Status:** partial
- **Gap:** demonstrated on the desktop; not yet run on an ESP32-C5

### SYS-REPL-01: A Forth REPL over the board's console
The system interprets and compiles Forth a line at a time over the board's
CH340 USB-UART console (UART0), reporting an undefined word, underflow,
overflow or over-long line with its ior and carrying on.

- **Parent:** UN-01
- **Verified by:** insp:src/target/ESP32C5/main/serial_io.c::uart_read_bytes
- **Status:** partial
- **Gap:** driver built against IDF; no on-board test

### SYS-FLASH-01: Persistent dictionary in flash
Colon definitions compile into flash pages that survive a reset, kept in a
dedicated flash partition (`lfflash`) separate from the application image
and OTA slots.

- **Parent:** UN-01
- **Verified by:** insp:src/target/ESP32C5/main/flash.c::esp_partition_mmap
- **Status:** partial
- **Gap:** driver built against IDF; no on-board test

## SEC: security

### SYS-SEC-01: Only vendor-signed firmware boots
Secure Boot v2 is enabled: the ROM verifies the bootloader and the
bootloader verifies the application against the vendor's public key, so a
modified or substituted image does not run.

- **Parent:** UN-04
- **Verified by:** insp:src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_BOOT_V2_ENABLED
- **Status:** partial
- **Gap:** configured in the overlay and checked by the gate; eFuses not burned (no board)

### SYS-SEC-02: Firmware and secrets encrypted at rest
Flash encryption is enabled in Release mode, so the application, its
blocks and any stored secrets are XTS-AES encrypted in flash with a key
that stays in eFuse.

- **Parent:** UN-05
- **Verified by:** insp:src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE
- **Status:** partial
- **Gap:** configured in the overlay and checked by the gate; eFuses not burned (no board)

### SYS-SEC-03: The serial download back door is closed
In production the ROM serial-download mode is disabled (or restricted to a
secure, encrypted path), so the chip can't be reflashed or dumped over
UART/USB; field updates go through OTA instead.

- **Parent:** UN-05
- **Verified by:** insp:src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_DISABLE_ROM_DL_MODE
- **Status:** partial
- **Gap:** configured in the overlay and checked by the gate; not burned (no board)

### SYS-SEC-04: No downgrade to a withdrawn image
OTA anti-rollback is enabled: a new image carries a secure version and the
bootloader refuses an image older than the one burned into eFuse, blocking
a downgrade to a withdrawn, vulnerable build.

- **Parent:** UN-03
- **Verified by:** insp:src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
- **Status:** partial
- **Gap:** configured in the overlay and checked by the gate; no OTA path or board yet

### SYS-SEC-05: The production security baseline is enforced automatically
The production security configuration (secure boot, flash encryption in
Release, download closed, anti-rollback, runtime encryption check) is
enforced by an automated gate, so a control can't quietly regress between
builds. The gate runs in CI.

- **Parent:** UN-06
- **Verified by:** ci:seccheck::compliant config: production passes, insp:scripts/ESP32C5/seccheck.py::RULES
- **Status:** verified

### SYS-SEC-06: ESP-IDF is treated as SOUP, with controls
ESP-IDF (and its bundled mbedTLS, lwIP, wpa_supplicant, FreeRTOS) is
software of unknown provenance: the version is pinned, its linked attack
surface tracked, an SBOM kept, and its CVEs monitored, so a platform
vulnerability is found and patched rather than shipped and forgotten.

- **Parent:** UN-06
- **Verified by:** insp:doc/ESP32C5/admin/security-plan.md::SOUP
- **Status:** partial
- **Gap:** controls defined in the plan; SBOM and CVE monitoring not yet built

### SYS-SEC-07: No insecure build escapes, in any profile
The build never enables an insecure escape — JTAG-allowed secure boot, the
flash-encryption Development mode, or a UART-bootloader encrypt/decrypt/
cache allowance — in either the production or the development profile. The
gate fails on any of them regardless of profile.

- **Parent:** UN-06
- **Verified by:** ci:seccheck::flags the allow-JTAG escape, ci:seccheck::dev profile still fails
- **Status:** verified

### SYS-SEC-08: A restricted console at production level
At the production level the console offers only a limited, reviewed
wordlist, so the serial path isn't an open interpreter into the device.
(The equivalent limited startup wordlist is also still to come on the
CH32H417.)

- **Parent:** UN-05
- **Status:** planned
- **Gap:** not yet designed

### SYS-SEC-09: Keys kept in hardware
The device's keys live in eFuse and the Digital Signature / HMAC
peripheral, not in readable flash or in the application, so firmware can
use a key without being able to read it out.

- **Parent:** UN-05
- **Status:** planned
- **Gap:** not yet brought up; needs a board

## UPD: updates

### SYS-UPD-01: Signed over-the-air updates
Firmware updates are delivered as signed images into an OTA slot, verified
by the secure-boot key, installed on trial and confirmed or rolled back —
the update path for a device whose download mode is closed.

- **Parent:** UN-03
- **Status:** planned
- **Gap:** partition layout in place; OTA logic not yet built

## QA: process and evidence

### SYS-QA-01: The target builds clean under ESP-IDF
The ESP32-C5 target builds without errors or new warnings under the pinned
ESP-IDF (`idf.py set-target esp32c5 && idf.py build`), so a build break is
caught early. (ESP-IDF isn't in CI — no toolchain — so this is checked by
the developer against a real IDF install.)

- **Parent:** UN-07
- **Verified by:** insp:src/target/ESP32C5/README.md::idf.py
- **Status:** partial
- **Gap:** builds locally against IDF v5.5.6; not built in CI
