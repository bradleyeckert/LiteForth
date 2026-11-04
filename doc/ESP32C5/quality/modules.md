# Modules (MOD) — ESP32-C5

The bottom of the ESP32-C5 V: the code, grouped into modules, each with its
design and its tests. A module is part of one architecture element
([architecture.md](architecture.md)). [README.md](README.md) explains what
differs from the CH32H417 V.

The shared Forth core (`src/vm.c`, `forth.c`, `comp.c`, `blocks.c`,
`sealed.c`, the crypto) is the CH32H417 V-model's modules, unchanged and
unit-tested there; this file covers only what is specific to the ESP32-C5:
the target's drivers (built against ESP-IDF, not yet run on a board) and the
build-time security tooling (tested in CI today).

## The target drivers

### MOD-SERIAL: The console UART driver
`serial_io.c` on the ESP32-C5: the CH340 USB-UART (UART0) as the Forth
console, through the IDF `uart` driver — non-blocking `serial_ready` from the
RX buffer level, `serial_getc`/`serial_putc`, binary mode for YMODEM.

- **Parent:** ARC-DRV
- **Code:** src/target/ESP32C5/main/serial_io.c::serial_getc
- **Design:** doc/ESP32C5/esp32c5-notes.md
- **Status:** partial
- **Gap:** built against IDF; no on-target unit test

### MOD-FLASH: The flash driver
`flash.c` on the ESP32-C5: the Forth dictionary pages in the `lfflash`
partition, read through `esp_partition_mmap` and written with erase-range +
write, skipping unchanged pages.

- **Parent:** ARC-DRV
- **Code:** src/target/ESP32C5/main/flash.c::flash_program
- **Design:** doc/ESP32C5/esp32c5-notes.md
- **Status:** partial
- **Gap:** built against IDF; no on-target unit test

### MOD-TIME: Time and reset
`lftime.c` on the ESP32-C5: microseconds from `esp_timer` and reboot through
the IDF restart; the RTC/standby words are stubs until the low-power path is
brought up.

- **Parent:** ARC-DRV
- **Code:** src/target/ESP32C5/main/lftime.c::lfGetTimeMicroSec
- **Design:** doc/ESP32C5/esp32c5-notes.md
- **Status:** partial
- **Gap:** built against IDF; RTC/standby not implemented; no board

### MOD-BOARD: Board identity
`board.c` on the ESP32-C5: `board_load` and the board's identity from the
eFuse MAC, standing in for the CH32H417's UID-derived identity until the DS
peripheral and keys are brought up.

- **Parent:** ARC-DRV
- **Code:** src/target/ESP32C5/main/board.c::board_load
- **Design:** doc/ESP32C5/esp32c5-notes.md
- **Status:** partial
- **Gap:** built against IDF; keys not yet in hardware; no board

### MOD-MAIN: Application entry
`main.c` on the ESP32-C5: `app_main` sets up the VM memory map
(`lfInitPointers`) and runs `lfQuit`, as one FreeRTOS task.

- **Parent:** ARC-APP
- **Code:** src/target/ESP32C5/main/main.c::app_main
- **Design:** doc/ESP32C5/esp32c5-notes.md
- **Status:** partial
- **Gap:** built against IDF; no board

## The security tooling

### MOD-SECCHECK: The security-configuration gate
`seccheck.py` parses an ESP-IDF `sdkconfig` and checks it against the
production security baseline (section 7.1 of the plan): every required
control present, no insecure escape in any profile, a warning for the cheap
software hardening. Two profiles (production, dev). This is the one
ESP32-C5-specific module with a real test today.

- **Parent:** ARC-SECCFG
- **Code:** scripts/ESP32C5/seccheck.py::RULES
- **Design:** doc/ESP32C5/admin/security-plan.md
- **Verified by:** unit:seccheck::compliant config: production passes, unit:seccheck::dev profile still fails
- **Status:** verified

### MOD-SECOVL: The production security overlay
`sdkconfig.secure.defaults` is the reviewed set of security settings applied
on top of `sdkconfig.defaults` for a release build; the gate checks that the
committed overlay itself satisfies the production baseline, so the overlay
and the gate can't drift apart.

- **Parent:** ARC-SECCFG
- **Code:** src/target/ESP32C5/sdkconfig.secure.defaults::CONFIG_SECURE_BOOT
- **Design:** doc/ESP32C5/admin/security-plan.md
- **Verified by:** unit:seccheck::satisfies the production baseline
- **Status:** verified
