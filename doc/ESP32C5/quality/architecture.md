# Architecture (ARC) — ESP32-C5

The components the ESP32-C5 target is built from, what each is responsible
for, and how they meet. Each element satisfies system requirements
([system.md](system.md)), is made of modules ([modules.md](modules.md)), and
is verified by tests that run it with its neighbors, or — until a board
exists — by inspection and the CI security gate. [README.md](README.md)
explains what differs from the CH32H417 V;
[esp32c5-notes.md](../esp32c5-notes.md) and
[security-plan.md](../admin/security-plan.md) hold the design.

```
 ESP32-C5 (single RISC-V core, ESP-IDF application)
 ------------------------------------------------------------------
  ROM ──> 2nd-stage bootloader ──> app  (ARC-SECBOOT: each verifies the next)
                                     │
            ┌────────────────────────┴───────────────────────┐
            │  app_main → lfQuit   (ARC-APP: one FreeRTOS task) │
            └───┬───────────────┬───────────────┬─────────────┘
                │               │               │
          ARC-DRV          shared src/*.c     ARC-OTA
          serial/flash/    (VM, forth, blocks, (esp_ota + anti-
          time/board/lcd    sealed, crypto)     rollback)
                │
          flash / eFuse / DS peripheral        ARC-SECCFG + ARC-SOUP
          ARC-PART  ARC-KEYS                    (build-time: the gate,
                                                 the SOUP controls)
```

### ARC-APP: LiteForth as an ESP-IDF application
LiteForth runs as a single ESP-IDF application: `app_main` sets up the VM
memory map and calls `lfQuit`, as one FreeRTOS task, so the platform's
Wi-Fi/BLE, OTA and secure-boot services are available rather than a bespoke
root of trust. Interfaces: the IDF component build (`main/CMakeLists.txt`
pulling in the shared `src/*.c`), `app_main`, and the target-contract calls
into the driver layer.

- **Parent:** SYS-CORE-01, SYS-REPL-01
- **Verified by:** insp:src/target/ESP32C5/main/main.c::app_main
- **Status:** partial
- **Gap:** builds against IDF; not run on a board

### ARC-DRV: The target driver layer
The ESP32-C5's implementation of the target contract over ESP-IDF:
`serial_io.c` (the CH340 UART console), `flash.c` (the `lfflash` partition
via `esp_partition`), `lftime.c` (`esp_timer` and reboot), `board.c` (board
identity from the eFuse MAC), `blocks.c` and `lcd.c`. Each presents the same
interface the other targets' drivers do, so the shared core is unchanged.

- **Parent:** SYS-REPL-01, SYS-FLASH-01
- **Verified by:** insp:src/target/ESP32C5/main/flash.c::flash_program, insp:src/target/ESP32C5/main/serial_io.c::serial_getc
- **Status:** partial
- **Gap:** built against IDF; no on-board or integration test

### ARC-PART: The partition and storage layout
The flash is partitioned for a secure, updatable device: an encrypted NVS,
`otadata`, two application slots (`ota_0`, `ota_1`) for A/B OTA, and a
`lfflash` data partition for the Forth dictionary — separate from the signed
application image so a dictionary write never touches signed code.

- **Parent:** SYS-UPD-01, SYS-FLASH-01
- **Verified by:** insp:src/target/ESP32C5/partitions.csv::lfflash
- **Status:** partial
- **Gap:** layout defined; OTA and encrypted NVS not yet exercised

### ARC-SECBOOT: The secure-boot and flash-encryption chain
The chain of trust: the ROM verifies the bootloader's signature, the
bootloader verifies the application's, and flash encryption (Release mode)
keeps the images and secrets encrypted at rest with an eFuse key, with a
runtime check that encryption is still on. This is ESP-IDF's mechanism,
configured by the build overlay; the eFuse burns happen at provisioning.

- **Parent:** SYS-SEC-01, SYS-SEC-02, SYS-SEC-03
- **Verified by:** insp:src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_FLASH_CHECK_ENC_EN_IN_APP
- **Status:** planned
- **Gap:** configured but not burned or tested on a board

### ARC-SECCFG: The security-configuration baseline and its gate
The production security configuration lives in a reviewed overlay
(`sdkconfig.secure.defaults`), and a gate (`seccheck.py`) turns the baseline
into a build check: every required control present, no insecure escape in
any profile. The gate runs in CI, so a regressed control fails the build.

- **Parent:** SYS-SEC-05, SYS-SEC-07
- **Verified by:** ci:seccheck::compliant config: production passes, ci:seccheck::flags the allow-JTAG escape
- **Status:** verified

### ARC-SOUP: The SOUP controls for ESP-IDF
ESP-IDF and its bundled libraries are managed as SOUP: a pinned version and
lockfile, an inventory of the linked attack surface, an SBOM of the platform
and its upstreams, and CVE monitoring against them. Interfaces: the pinned
IDF version, the (future) SBOM, and the plan's control list.

- **Parent:** SYS-SEC-06
- **Verified by:** insp:doc/ESP32C5/admin/security-plan.md::SOUP
- **Status:** planned
- **Gap:** controls defined in the plan; SBOM and CVE monitoring not yet built

### ARC-OTA: Over-the-air update and anti-rollback
The update mechanism: a signed image written to the inactive OTA slot,
verified by the secure-boot key, booted on trial, confirmed or rolled back,
and refused if older than the eFuse secure version. The path for a device
whose serial download mode is closed.

- **Parent:** SYS-UPD-01, SYS-SEC-04
- **Status:** planned
- **Gap:** not yet built

### ARC-KEYS: Key storage in hardware
The device's keys in eFuse and the Digital Signature / HMAC peripheral, used
by firmware without being readable by it — the secure-boot public key digest,
the flash-encryption key, and application keys behind the DS peripheral.

- **Parent:** SYS-SEC-09
- **Status:** planned
- **Gap:** not yet brought up; needs a board
