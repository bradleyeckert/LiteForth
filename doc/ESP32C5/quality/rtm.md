# Requirements Traceability Matrix

Written by `scripts/rtm.py` from [needs.md](needs.md), [system.md](system.md), [architecture.md](architecture.md) and [modules.md](modules.md); don't edit it by hand. [README.md](README.md) explains the V-model, the levels and the kinds of evidence. `make test` fails if this file is out of date or a link in it is broken.

## Summary

| Level | Items | Verified | Partial | Planned | With gaps |
|---|---|---|---|---|---|
| Stakeholder needs (UN) | 10 | 0 | 3 | 7 | 10 |
| System requirements (SYS) | 20 | 2 | 13 | 5 | 18 |
| Architecture elements (ARC) | 10 | 1 | 6 | 3 | 9 |
| Modules (MOD) | 8 | 3 | 5 | 0 | 5 |

## Needs to system requirements

Each stakeholder need, how it's validated, and the system requirements that refine it.

| Need | Validated by | System requirements | Status |
|---|---|---|---|
| [UN-01](needs.md#un-01-interactive-forth-on-the-esp32-c5-board) Interactive Forth on the ESP32-C5 board | - | [SYS-FLASH-01](system.md#sys-flash-01-persistent-dictionary-in-flash), [SYS-REPL-01](system.md#sys-repl-01-a-forth-repl-over-the-boards-console) | planned |
| [UN-02](needs.md#un-02-the-same-system-as-the-other-targets) The same system as the other targets | demo:`scripts/regression.f::T{`<br>demo:`src/target/desktop/unit_tests/exfat::run_lf: lf finds lfblocks.fb4` | [SYS-CORE-01](system.md#sys-core-01-the-shared-forth-core-unchanged) | partial |
| [UN-03](needs.md#un-03-updates-that-never-brick-a-device) Updates that never brick a device | - | [SYS-SEC-04](system.md#sys-sec-04-no-downgrade-to-a-withdrawn-image), [SYS-UPD-01](system.md#sys-upd-01-signed-over-the-air-updates) | planned |
| [UN-04](needs.md#un-04-only-the-vendors-firmware-runs) Only the vendor's firmware runs | - | [SYS-SEC-01](system.md#sys-sec-01-only-vendor-signed-firmware-boots), [SYS-SEC-11](system.md#sys-sec-11-quantum-resistant-signatures-for-our-own-signature-layer) | planned |
| [UN-05](needs.md#un-05-firmware-and-secrets-cant-be-extracted) Firmware and secrets can't be extracted | - | [SYS-SEC-02](system.md#sys-sec-02-firmware-and-secrets-encrypted-at-rest), [SYS-SEC-03](system.md#sys-sec-03-the-serial-download-back-door-is-closed), [SYS-SEC-08](system.md#sys-sec-08-a-restricted-console-at-production-level), [SYS-SEC-09](system.md#sys-sec-09-keys-kept-in-hardware) | planned |
| [UN-06](needs.md#un-06-stays-defensible-against-automated-attack-in-the-field) Stays defensible against automated attack in the field | - | [SYS-SEC-05](system.md#sys-sec-05-the-production-security-baseline-is-enforced-automatically), [SYS-SEC-06](system.md#sys-sec-06-esp-idf-is-treated-as-soup-with-controls), [SYS-SEC-07](system.md#sys-sec-07-no-insecure-build-escapes-in-any-profile), [SYS-SEC-10](system.md#sys-sec-10-the-c5-crypto-accelerators-are-used-through-checked-hooks) | planned |
| [UN-07](needs.md#un-07-evidence-for-a-security-assessment) Evidence for a security assessment | demo:`src/target/desktop/unit_tests/seccheck::compliant config: production passes` | [SYS-QA-01](system.md#sys-qa-01-the-target-builds-clean-under-esp-idf) | partial |
| [UN-08](needs.md#un-08-wireless-connectivity) Wireless connectivity | - | **none** | planned |
| [UN-09](needs.md#un-09-encrypted-authenticated-recordings-only-the-data-owner-can-read) Encrypted, authenticated recordings only the data owner can read | demo:`scripts/CH32H417/admin/lflog.py::server-key`<br>demo:`src/target/desktop/unit_tests/datalog::round trip` | [SYS-LOG-01](system.md#sys-log-01-encrypted-authenticated-recordings-using-the-c5-hardware), [SYS-LOG-02](system.md#sys-log-02-the-server-proves-origin-and-integrity-the-uid-stays-private), [SYS-LOG-03](system.md#sys-log-03-recoverable-after-a-power-cut-one-file-per-boot) | partial |
| [UN-10](needs.md#un-10-a-security-event-log-the-device-keeps-and-the-server-can-read) A security-event log the device keeps and the server can read | - | [SYS-LOG-04](system.md#sys-log-04-a-security-event-log-in-the-same-format) | planned |

## System requirements

Left side: the need above, the architecture below. Right side: the system and integration tests (and board tests) that verify it.

| Requirement | Need | Architecture | Verified by | Status |
|---|---|---|---|---|
| [SYS-CORE-01](system.md#sys-core-01-the-shared-forth-core-unchanged) The shared Forth core, unchanged | [UN-02](needs.md#un-02-the-same-system-as-the-other-targets) | [ARC-APP](architecture.md#arc-app-liteforth-as-an-esp-idf-application) | demo:`scripts/regression.f::T{`<br>demo:`src/target/desktop/unit_tests/sealed::stage_tests` | partial |
| [SYS-FLASH-01](system.md#sys-flash-01-persistent-dictionary-in-flash) Persistent dictionary in flash | [UN-01](needs.md#un-01-interactive-forth-on-the-esp32-c5-board) | [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer), [ARC-PART](architecture.md#arc-part-the-partition-and-storage-layout) | insp:`src/target/ESP32C5/main/flash.c::esp_partition_mmap` | partial |
| [SYS-LOG-01](system.md#sys-log-01-encrypted-authenticated-recordings-using-the-c5-hardware) Encrypted, authenticated recordings using the C5 hardware | [UN-09](needs.md#un-09-encrypted-authenticated-recordings-only-the-data-owner-can-read) | [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events) | sys:`datalog::test_round_trip`<br>insp:`doc/ESP32C5/admin/data-logs.md::AES accelerator` | partial |
| [SYS-LOG-02](system.md#sys-log-02-the-server-proves-origin-and-integrity-the-uid-stays-private) The server proves origin and integrity; the UID stays private | [UN-09](needs.md#un-09-encrypted-authenticated-recordings-only-the-data-owner-can-read) | [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events) | sys:`datalog::test_tampering`<br>sys:`datalog::test_round_trip` | partial |
| [SYS-LOG-03](system.md#sys-log-03-recoverable-after-a-power-cut-one-file-per-boot) Recoverable after a power cut; one file per boot | [UN-09](needs.md#un-09-encrypted-authenticated-recordings-only-the-data-owner-can-read) | [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events) | sys:`datalog::test_power_cuts`<br>sys:`datalog::test_stale` | partial |
| [SYS-LOG-04](system.md#sys-log-04-a-security-event-log-in-the-same-format) A security-event log in the same format | [UN-10](needs.md#un-10-a-security-event-log-the-device-keeps-and-the-server-can-read) | [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events) | insp:`doc/ESP32C5/admin/data-logs.md::security-event log` | planned |
| [SYS-QA-01](system.md#sys-qa-01-the-target-builds-clean-under-esp-idf) The target builds clean under ESP-IDF | [UN-07](needs.md#un-07-evidence-for-a-security-assessment) | **none** | insp:`src/target/ESP32C5/README.md::idf.py` | partial |
| [SYS-REPL-01](system.md#sys-repl-01-a-forth-repl-over-the-boards-console) A Forth REPL over the board's console | [UN-01](needs.md#un-01-interactive-forth-on-the-esp32-c5-board) | [ARC-APP](architecture.md#arc-app-liteforth-as-an-esp-idf-application), [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer) | insp:`src/target/ESP32C5/main/serial_io.c::uart_read_bytes` | partial |
| [SYS-SEC-01](system.md#sys-sec-01-only-vendor-signed-firmware-boots) Only vendor-signed firmware boots | [UN-04](needs.md#un-04-only-the-vendors-firmware-runs) | [ARC-SECBOOT](architecture.md#arc-secboot-the-secure-boot-and-flash-encryption-chain) | insp:`src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_BOOT_V2_ENABLED` | partial |
| [SYS-SEC-02](system.md#sys-sec-02-firmware-and-secrets-encrypted-at-rest) Firmware and secrets encrypted at rest | [UN-05](needs.md#un-05-firmware-and-secrets-cant-be-extracted) | [ARC-CRYPTO](architecture.md#arc-crypto-the-hardware-crypto-backend-accelerator-hooks), [ARC-SECBOOT](architecture.md#arc-secboot-the-secure-boot-and-flash-encryption-chain) | insp:`src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_FLASH_ENCRYPTION_MODE_RELEASE` | partial |
| [SYS-SEC-03](system.md#sys-sec-03-the-serial-download-back-door-is-closed) The serial download back door is closed | [UN-05](needs.md#un-05-firmware-and-secrets-cant-be-extracted) | [ARC-SECBOOT](architecture.md#arc-secboot-the-secure-boot-and-flash-encryption-chain) | insp:`src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_DISABLE_ROM_DL_MODE` | partial |
| [SYS-SEC-04](system.md#sys-sec-04-no-downgrade-to-a-withdrawn-image) No downgrade to a withdrawn image | [UN-03](needs.md#un-03-updates-that-never-brick-a-device) | [ARC-OTA](architecture.md#arc-ota-over-the-air-update-and-anti-rollback) | insp:`src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` | partial |
| [SYS-SEC-05](system.md#sys-sec-05-the-production-security-baseline-is-enforced-automatically) The production security baseline is enforced automatically | [UN-06](needs.md#un-06-stays-defensible-against-automated-attack-in-the-field) | [ARC-SECCFG](architecture.md#arc-seccfg-the-security-configuration-baseline-and-its-gate) | ci:`seccheck::compliant config: production passes`<br>insp:`scripts/ESP32C5/seccheck.py::RULES` | verified |
| [SYS-SEC-06](system.md#sys-sec-06-esp-idf-is-treated-as-soup-with-controls) ESP-IDF is treated as SOUP, with controls | [UN-06](needs.md#un-06-stays-defensible-against-automated-attack-in-the-field) | [ARC-SOUP](architecture.md#arc-soup-the-soup-controls-for-esp-idf) | ci:`soup::offline cvecheck of the pinned SOUP is clean`<br>insp:`src/target/ESP32C5/soup.lock.json::linked`<br>insp:`doc/ESP32C5/admin/soup.md::CVE monitor` | partial |
| [SYS-SEC-07](system.md#sys-sec-07-no-insecure-build-escapes-in-any-profile) No insecure build escapes, in any profile | [UN-06](needs.md#un-06-stays-defensible-against-automated-attack-in-the-field) | [ARC-SECCFG](architecture.md#arc-seccfg-the-security-configuration-baseline-and-its-gate) | ci:`seccheck::flags the allow-JTAG escape`<br>ci:`seccheck::dev profile still fails` | verified |
| [SYS-SEC-08](system.md#sys-sec-08-a-restricted-console-at-production-level) A restricted console at production level | [UN-05](needs.md#un-05-firmware-and-secrets-cant-be-extracted) | **none** | - | planned |
| [SYS-SEC-09](system.md#sys-sec-09-keys-kept-in-hardware) Keys kept in hardware | [UN-05](needs.md#un-05-firmware-and-secrets-cant-be-extracted) | [ARC-CRYPTO](architecture.md#arc-crypto-the-hardware-crypto-backend-accelerator-hooks), [ARC-KEYS](architecture.md#arc-keys-key-storage-in-hardware) | - | planned |
| [SYS-SEC-10](system.md#sys-sec-10-the-c5-crypto-accelerators-are-used-through-checked-hooks) The C5 crypto accelerators are used through checked hooks | [UN-06](needs.md#un-06-stays-defensible-against-automated-attack-in-the-field) | [ARC-CRYPTO](architecture.md#arc-crypto-the-hardware-crypto-backend-accelerator-hooks) | insp:`src/ctr.c::CTR_HW`<br>insp:`doc/ESP32C5/esp32c5-notes.md::Hardware crypto hooks` | partial |
| [SYS-SEC-11](system.md#sys-sec-11-quantum-resistant-signatures-for-our-own-signature-layer) Quantum-resistant signatures for our own signature layer | [UN-04](needs.md#un-04-only-the-vendors-firmware-runs) | [ARC-CRYPTO](architecture.md#arc-crypto-the-hardware-crypto-backend-accelerator-hooks) | insp:`doc/ESP32C5/esp32c5-notes.md::prefer LMS` | planned |
| [SYS-UPD-01](system.md#sys-upd-01-signed-over-the-air-updates) Signed over-the-air updates | [UN-03](needs.md#un-03-updates-that-never-brick-a-device) | [ARC-OTA](architecture.md#arc-ota-over-the-air-update-and-anti-rollback), [ARC-PART](architecture.md#arc-part-the-partition-and-storage-layout) | - | planned |

## Architecture

| Element | Satisfies | Modules | Integration evidence | Status |
|---|---|---|---|---|
| [ARC-APP](architecture.md#arc-app-liteforth-as-an-esp-idf-application) LiteForth as an ESP-IDF application | [SYS-CORE-01](system.md#sys-core-01-the-shared-forth-core-unchanged), [SYS-REPL-01](system.md#sys-repl-01-a-forth-repl-over-the-boards-console) | [MOD-MAIN](modules.md#mod-main-application-entry) | insp:`src/target/ESP32C5/main/main.c::app_main` | partial |
| [ARC-CRYPTO](architecture.md#arc-crypto-the-hardware-crypto-backend-accelerator-hooks) The hardware-crypto backend (accelerator hooks) | [SYS-SEC-10](system.md#sys-sec-10-the-c5-crypto-accelerators-are-used-through-checked-hooks), [SYS-SEC-11](system.md#sys-sec-11-quantum-resistant-signatures-for-our-own-signature-layer), [SYS-SEC-02](system.md#sys-sec-02-firmware-and-secrets-encrypted-at-rest), [SYS-SEC-09](system.md#sys-sec-09-keys-kept-in-hardware) | **none** | insp:`src/ctr.c::CTR_HW`<br>insp:`doc/ESP32C5/esp32c5-notes.md::Hardware crypto hooks` | partial |
| [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer) The target driver layer | [SYS-REPL-01](system.md#sys-repl-01-a-forth-repl-over-the-boards-console), [SYS-FLASH-01](system.md#sys-flash-01-persistent-dictionary-in-flash) | [MOD-BOARD](modules.md#mod-board-board-identity), [MOD-FLASH](modules.md#mod-flash-the-flash-driver), [MOD-SERIAL](modules.md#mod-serial-the-console-uart-driver), [MOD-TIME](modules.md#mod-time-time-and-reset) | insp:`src/target/ESP32C5/main/flash.c::flash_program`<br>insp:`src/target/ESP32C5/main/serial_io.c::serial_getc` | partial |
| [ARC-KEYS](architecture.md#arc-keys-key-storage-in-hardware) Key storage in hardware | [SYS-SEC-09](system.md#sys-sec-09-keys-kept-in-hardware) | **none** | - | planned |
| [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events) The logging subsystem (data logs and security events) | [SYS-LOG-01](system.md#sys-log-01-encrypted-authenticated-recordings-using-the-c5-hardware), [SYS-LOG-02](system.md#sys-log-02-the-server-proves-origin-and-integrity-the-uid-stays-private), [SYS-LOG-03](system.md#sys-log-03-recoverable-after-a-power-cut-one-file-per-boot), [SYS-LOG-04](system.md#sys-log-04-a-security-event-log-in-the-same-format) | **none** | sys:`datalog::test_round_trip`<br>insp:`doc/ESP32C5/admin/data-logs.md::security-event log` | partial |
| [ARC-OTA](architecture.md#arc-ota-over-the-air-update-and-anti-rollback) Over-the-air update and anti-rollback | [SYS-UPD-01](system.md#sys-upd-01-signed-over-the-air-updates), [SYS-SEC-04](system.md#sys-sec-04-no-downgrade-to-a-withdrawn-image) | **none** | - | planned |
| [ARC-PART](architecture.md#arc-part-the-partition-and-storage-layout) The partition and storage layout | [SYS-UPD-01](system.md#sys-upd-01-signed-over-the-air-updates), [SYS-FLASH-01](system.md#sys-flash-01-persistent-dictionary-in-flash) | **none** | insp:`src/target/ESP32C5/partitions.csv::lfflash` | partial |
| [ARC-SECBOOT](architecture.md#arc-secboot-the-secure-boot-and-flash-encryption-chain) The secure-boot and flash-encryption chain | [SYS-SEC-01](system.md#sys-sec-01-only-vendor-signed-firmware-boots), [SYS-SEC-02](system.md#sys-sec-02-firmware-and-secrets-encrypted-at-rest), [SYS-SEC-03](system.md#sys-sec-03-the-serial-download-back-door-is-closed) | **none** | insp:`src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_FLASH_CHECK_ENC_EN_IN_APP` | planned |
| [ARC-SECCFG](architecture.md#arc-seccfg-the-security-configuration-baseline-and-its-gate) The security-configuration baseline and its gate | [SYS-SEC-05](system.md#sys-sec-05-the-production-security-baseline-is-enforced-automatically), [SYS-SEC-07](system.md#sys-sec-07-no-insecure-build-escapes-in-any-profile) | [MOD-SECCHECK](modules.md#mod-seccheck-the-security-configuration-gate), [MOD-SECOVL](modules.md#mod-secovl-the-production-security-overlay) | ci:`seccheck::compliant config: production passes`<br>ci:`seccheck::flags the allow-JTAG escape` | verified |
| [ARC-SOUP](architecture.md#arc-soup-the-soup-controls-for-esp-idf) The SOUP controls for ESP-IDF | [SYS-SEC-06](system.md#sys-sec-06-esp-idf-is-treated-as-soup-with-controls) | [MOD-SOUP](modules.md#mod-soup-the-soup-sbom-and-cve-monitor) | ci:`soup::offline cvecheck of the pinned SOUP is clean`<br>insp:`src/target/ESP32C5/soup.spdx.json::SPDX-2.3` | partial |

## Modules

| Module | Element | Code | Design | Unit tests | Status |
|---|---|---|---|---|---|
| [MOD-BOARD](modules.md#mod-board-board-identity) Board identity | [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer) | `src/target/ESP32C5/main/board.c::board_load` | `doc/ESP32C5/esp32c5-notes.md` | - | partial |
| [MOD-FLASH](modules.md#mod-flash-the-flash-driver) The flash driver | [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer) | `src/target/ESP32C5/main/flash.c::flash_program` | `doc/ESP32C5/esp32c5-notes.md` | - | partial |
| [MOD-MAIN](modules.md#mod-main-application-entry) Application entry | [ARC-APP](architecture.md#arc-app-liteforth-as-an-esp-idf-application) | `src/target/ESP32C5/main/main.c::app_main` | `doc/ESP32C5/esp32c5-notes.md` | - | partial |
| [MOD-SECCHECK](modules.md#mod-seccheck-the-security-configuration-gate) The security-configuration gate | [ARC-SECCFG](architecture.md#arc-seccfg-the-security-configuration-baseline-and-its-gate) | `scripts/ESP32C5/seccheck.py::RULES` | `doc/ESP32C5/admin/security-plan.md` | unit:`seccheck::compliant config: production passes`<br>unit:`seccheck::dev profile still fails` | verified |
| [MOD-SECOVL](modules.md#mod-secovl-the-production-security-overlay) The production security overlay | [ARC-SECCFG](architecture.md#arc-seccfg-the-security-configuration-baseline-and-its-gate) | `src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_BOOT` | `doc/ESP32C5/admin/security-plan.md` | unit:`seccheck::satisfies the production baseline` | verified |
| [MOD-SERIAL](modules.md#mod-serial-the-console-uart-driver) The console UART driver | [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer) | `src/target/ESP32C5/main/serial_io.c::serial_getc` | `doc/ESP32C5/esp32c5-notes.md` | - | partial |
| [MOD-SOUP](modules.md#mod-soup-the-soup-sbom-and-cve-monitor) The SOUP SBOM and CVE monitor | [ARC-SOUP](architecture.md#arc-soup-the-soup-controls-for-esp-idf) | `scripts/ESP32C5/cvecheck.py::affected`<br>`scripts/ESP32C5/soup_sbom.py::build` | `doc/ESP32C5/admin/soup.md` | unit:`soup::a linked component in an affected range fails the gate`<br>unit:`soup::a version at/above the fix is clean` | verified |
| [MOD-TIME](modules.md#mod-time-time-and-reset) Time and reset | [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer) | `src/target/ESP32C5/main/lftime.c::lfGetTimeMicroSec` | `doc/ESP32C5/esp32c5-notes.md` | - | partial |

## Test suites to items

Every suite under `src/target/desktop/unit_tests/` (run by `make test`) and what it verifies. A suite nothing names is listed as a gap.

| Suite | Verifies |
|---|---|
| `api0` | traced in the CH32H417 V-model |
| `appstep` | traced in the CH32H417 V-model |
| `blake2s` | traced in the CH32H417 V-model |
| `blkrun` | traced in the CH32H417 V-model |
| `blocks` | traced in the CH32H417 V-model |
| `blocksim` | traced in the CH32H417 V-model |
| `boot` | traced in the CH32H417 V-model |
| `calendar` | traced in the CH32H417 V-model |
| `ctr` | traced in the CH32H417 V-model |
| `datalog` | [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events), [SYS-LOG-01](system.md#sys-log-01-encrypted-authenticated-recordings-using-the-c5-hardware), [SYS-LOG-02](system.md#sys-log-02-the-server-proves-origin-and-integrity-the-uid-stays-private), [SYS-LOG-03](system.md#sys-log-03-recoverable-after-a-power-cut-one-file-per-boot) |
| `deadtib` | traced in the CH32H417 V-model |
| `editor` | traced in the CH32H417 V-model |
| `exfat` | traced in the CH32H417 V-model |
| `flash` | traced in the CH32H417 V-model |
| `glyph` | traced in the CH32H417 V-model |
| `guard` | traced in the CH32H417 V-model |
| `heads` | traced in the CH32H417 V-model |
| `ioguard` | traced in the CH32H417 V-model |
| `iomap` | traced in the CH32H417 V-model |
| `label` | traced in the CH32H417 V-model |
| `lcd` | traced in the CH32H417 V-model |
| `lfmcp` | traced in the CH32H417 V-model |
| `lintf` | traced in the CH32H417 V-model |
| `load` | traced in the CH32H417 V-model |
| `locked` | traced in the CH32H417 V-model |
| `memalloc` | traced in the CH32H417 V-model |
| `mkfont` | traced in the CH32H417 V-model |
| `putbin` | traced in the CH32H417 V-model |
| `putblocks` | traced in the CH32H417 V-model |
| `quit` | traced in the CH32H417 V-model |
| `reboot` | traced in the CH32H417 V-model |
| `rtc` | traced in the CH32H417 V-model |
| `rtm` | traced in the CH32H417 V-model |
| `savepages` | traced in the CH32H417 V-model |
| `sbom` | traced in the CH32H417 V-model |
| `seal` | traced in the CH32H417 V-model |
| `sealed` | traced in the CH32H417 V-model |
| `seccheck` | [ARC-SECCFG](architecture.md#arc-seccfg-the-security-configuration-baseline-and-its-gate), [MOD-SECCHECK](modules.md#mod-seccheck-the-security-configuration-gate), [MOD-SECOVL](modules.md#mod-secovl-the-production-security-overlay), [SYS-SEC-05](system.md#sys-sec-05-the-production-security-baseline-is-enforced-automatically), [SYS-SEC-07](system.md#sys-sec-07-no-insecure-build-escapes-in-any-profile) |
| `serial_io` | traced in the CH32H417 V-model |
| `soup` | [ARC-SOUP](architecture.md#arc-soup-the-soup-controls-for-esp-idf), [MOD-SOUP](modules.md#mod-soup-the-soup-sbom-and-cve-monitor), [SYS-SEC-06](system.md#sys-sec-06-esp-idf-is-treated-as-soup-with-controls) |
| `stage1sim` | traced in the CH32H417 V-model |
| `traps` | traced in the CH32H417 V-model |
| `update` | traced in the CH32H417 V-model |
| `vmasm` | traced in the CH32H417 V-model |
| `ymodem` | traced in the CH32H417 V-model |
| `ymrecv` | traced in the CH32H417 V-model |

## Board tests to items

Every entry of [board-tests.md](../admin/board-tests.md), the hardware evidence, and what it verifies or validates.

| Entry | Verifies |
|---|---|

## Gaps

What isn't verified at its level yet, or isn't traced. Planned items are here until they're built.

- [ARC-APP](architecture.md#arc-app-liteforth-as-an-esp-idf-application): partial: builds against IDF; not run on a board
- [ARC-CRYPTO](architecture.md#arc-crypto-the-hardware-crypto-backend-accelerator-hooks): no module traces to it
- [ARC-CRYPTO](architecture.md#arc-crypto-the-hardware-crypto-backend-accelerator-hooks): partial: the AES seam exists; the SHA/HMAC/ECC/DS hooks and the LMS verifier are not built, no board
- [ARC-DRV](architecture.md#arc-drv-the-target-driver-layer): partial: built against IDF; no on-board or integration test
- [ARC-KEYS](architecture.md#arc-keys-key-storage-in-hardware): planned: not built yet
- [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events): no module traces to it
- [ARC-LOG](architecture.md#arc-log-the-logging-subsystem-data-logs-and-security-events): partial: the shared format is proven on the desktop; the C5 backend, SD-over-SPI, CODEC capture and the security write points are not built
- [ARC-OTA](architecture.md#arc-ota-over-the-air-update-and-anti-rollback): planned: not built yet
- [ARC-PART](architecture.md#arc-part-the-partition-and-storage-layout): no module traces to it
- [ARC-PART](architecture.md#arc-part-the-partition-and-storage-layout): partial: layout defined; OTA and encrypted NVS not yet exercised
- [ARC-SECBOOT](architecture.md#arc-secboot-the-secure-boot-and-flash-encryption-chain): planned: not built yet
- [ARC-SOUP](architecture.md#arc-soup-the-soup-controls-for-esp-idf): partial: SBOM + monitor built and automated; the field patch path (OTA) not yet, and some manifest versions need confirming against the install
- [MOD-BOARD](modules.md#mod-board-board-identity): no unit evidence for a module
- [MOD-FLASH](modules.md#mod-flash-the-flash-driver): no unit evidence for a module
- [MOD-MAIN](modules.md#mod-main-application-entry): no unit evidence for a module
- [MOD-SERIAL](modules.md#mod-serial-the-console-uart-driver): no unit evidence for a module
- [MOD-TIME](modules.md#mod-time-time-and-reset): no unit evidence for a module
- [SYS-CORE-01](system.md#sys-core-01-the-shared-forth-core-unchanged): no board/ci/insp/integ/sys evidence for a system requirement
- [SYS-FLASH-01](system.md#sys-flash-01-persistent-dictionary-in-flash): partial: driver built against IDF; no on-board test
- [SYS-LOG-01](system.md#sys-log-01-encrypted-authenticated-recordings-using-the-c5-hardware): partial: format proven on the desktop; the C5 AES/HMAC/DS backend is not built, no board
- [SYS-LOG-02](system.md#sys-log-02-the-server-proves-origin-and-integrity-the-uid-stays-private): partial: proven on the desktop; not yet on the C5 with hardware-rooted keys
- [SYS-LOG-03](system.md#sys-log-03-recoverable-after-a-power-cut-one-file-per-boot): partial: proven on the desktop; SD-over-SPI and a board run pending
- [SYS-LOG-04](system.md#sys-log-04-a-security-event-log-in-the-same-format): planned: not built yet
- [SYS-QA-01](system.md#sys-qa-01-the-target-builds-clean-under-esp-idf): no architecture element traces to it
- [SYS-QA-01](system.md#sys-qa-01-the-target-builds-clean-under-esp-idf): partial: builds locally against IDF v5.5.6; not built in CI
- [SYS-REPL-01](system.md#sys-repl-01-a-forth-repl-over-the-boards-console): partial: driver built against IDF; no on-board test
- [SYS-SEC-01](system.md#sys-sec-01-only-vendor-signed-firmware-boots): partial: configured in the overlay and checked by the gate; eFuses not burned (no board)
- [SYS-SEC-02](system.md#sys-sec-02-firmware-and-secrets-encrypted-at-rest): partial: configured in the overlay and checked by the gate; eFuses not burned (no board)
- [SYS-SEC-03](system.md#sys-sec-03-the-serial-download-back-door-is-closed): partial: configured in the overlay and checked by the gate; not burned (no board)
- [SYS-SEC-04](system.md#sys-sec-04-no-downgrade-to-a-withdrawn-image): partial: configured in the overlay and checked by the gate; no OTA path or board yet
- [SYS-SEC-06](system.md#sys-sec-06-esp-idf-is-treated-as-soup-with-controls): partial: controls defined in the plan; SBOM and CVE monitoring not yet built
- [SYS-SEC-08](system.md#sys-sec-08-a-restricted-console-at-production-level): planned: not built yet
- [SYS-SEC-09](system.md#sys-sec-09-keys-kept-in-hardware): planned: not built yet
- [SYS-SEC-10](system.md#sys-sec-10-the-c5-crypto-accelerators-are-used-through-checked-hooks): partial: the AES seam exists; the SHA/HMAC/ECC/DS hooks are not built, no board
- [SYS-SEC-11](system.md#sys-sec-11-quantum-resistant-signatures-for-our-own-signature-layer): planned: not built yet
- [SYS-UPD-01](system.md#sys-upd-01-signed-over-the-air-updates): planned: not built yet
- [UN-01](needs.md#un-01-interactive-forth-on-the-esp32-c5-board): planned: not built yet
- [UN-02](needs.md#un-02-the-same-system-as-the-other-targets): partial: the shared core is demonstrated on the desktop; no ESP32-C5 board run logged
- [UN-03](needs.md#un-03-updates-that-never-brick-a-device): planned: not built yet
- [UN-04](needs.md#un-04-only-the-vendors-firmware-runs): planned: not built yet
- [UN-05](needs.md#un-05-firmware-and-secrets-cant-be-extracted): planned: not built yet
- [UN-06](needs.md#un-06-stays-defensible-against-automated-attack-in-the-field): planned: not built yet
- [UN-07](needs.md#un-07-evidence-for-a-security-assessment): partial: the plan, this V-model and the automated config gate exist; the control-by-control FDA mapping and the SBOM are still to come
- [UN-08](needs.md#un-08-wireless-connectivity): planned: not built yet
- [UN-09](needs.md#un-09-encrypted-authenticated-recordings-only-the-data-owner-can-read): partial: the shared log format is proven on the desktop; the C5 hardware-crypto backend, CODEC capture and a board run are not built
- [UN-10](needs.md#un-10-a-security-event-log-the-device-keeps-and-the-server-can-read): planned: not built yet
