# ESP32-C5 target notes

Porting LiteForth to the **ESP32-C5**, on the **NM-CYD-C5** dev board
(a "Cheap Yellow Display"-style board). Unlike the CH32H417 (bare-metal,
dual-core), this target is built as an **ESP-IDF application** so it can lean
on Espressif's Wi-Fi 6 / BLE stacks, OTA, and secure boot + flash
encryption rather than re-implementing a root of trust. Add new findings
here as the port proceeds, the way `doc/CH32H417/ch32h417-notes.md` does.

## The chip (ESP32-C5)

Single **HP core**: 32-bit ESP-RISC-V, RV32IMAC + Zc (Zcb/Zcmp/Zcmt) + a
custom hardware-loop extension (Xhwlp), 5-stage pipeline, up to **240 MHz**,
machine + user modes, **up to 16 PMP/PMA regions**, 3 HW breakpoints. A
separate small **LP core** (RV32IMAC, 48 MHz, stays up in deep sleep) is not
used initially.

- **Memory:** 384 KB HP SRAM, 320 KB ROM, 32 KB cache (4-way, 32-byte line),
  16 KB LP SRAM (deep-sleep retained), 4 Kbit eFuse (1792 bits user).
- **External memory:** instruction and data spaces each map up to 32 MB of
  flash/PSRAM in 64 KB blocks through the cache (execute-in-place). SPI /
  Dual / Quad / QPI; default flash clock 80 MHz (120 MHz gated by Espressif).
  **XTS-AES** flash and PSRAM encryption, with separate keys.
- **USB:** a **USB Serial/JTAG** controller on **GPIO13 (D-)/GPIO14 (D+)** —
  enumerates as a CDC-ACM + JTAG device. **No USB-OTG.**
- **Buses:** UART ×2 + LP UART; one general-purpose **SPI2**; one I2C + LP
  I2C; one I2S; two TWAI/CAN-FD. **SDIO *slave* only — there is no SD host**,
  so a microSD must be driven over SPI.
- **Timers:** 52-bit systimer, TIMG0/1 (54-bit, each a watchdog), 48-bit RTC
  timer, 3 digital WDTs + analog super-WDT.
- **Crypto / security:** AES-128/256 (+DMA modes), SHA-1..512, RSA (≤3072),
  ECC (P-192/256/384), HMAC-SHA-256, TRNG, RSA_DS/ECDSA_DS, secure boot,
  key manager (rev ≥ v1.2), APM + TEE, power-glitch detector.
- **Strapping/boot:** boot mode on GPIO26/27/28; ROM log on GPIO27; JTAG
  source GPIO7. (Table 3-3 in the datasheet was garbled in extraction —
  confirm the exact levels against the PDF before relying on them.)
- **Caution:** **SDIO host and secure-boot key manager depend on chip
  revision** (SDIO unsupported on v0.0/v0.1; key manager v1.2+). Read the
  revision off the board before committing to either.

## The board (NM-CYD-C5)

- ESP32-C5, **16 MB flash**, **8 MB PSRAM** (external; the datasheet lists an
  in-package 8 MB flash / 2 MB PSRAM part, `ESP32-C5NF8R2LA`, but this board
  advertises 16 MB + 8 MB, so the memory is off-package).
- **2.8" 320×240 TFT, ST7789**, with **capacitive touch** (controller and bus
  not documented — likely SPI for the panel, I2C for touch; confirm).
- **microSD** slot (SPI, since the C5 has no SD host).
- One **RGB LED**; a **12-pin FPC**; "additional GPIO pins."
- **Two USB-C:** one to the C5's USB Serial/JTAG, one to a **CH340** UART
  bridge. Either can be the LiteForth console.
- Claims dimensions/interfaces "fully compatible with the ESP32-CYD."

**Open — the GPIO map.** The AliExpress manual PDF is high-level only and the
product page is robots-blocked, so the exact GPIO assignments (panel
SPI/DC/CS/RST/backlight, touch I2C, SD SPI/CS, RGB LED, FPC) are **not yet
known**. The USB console (GPIO13/14) and the system flash do not need it, so
the REPL milestone is unblocked; the LCD/SD/RGB drivers do need it. Get the
board schematic, or reverse it from the CYD-compatibility claim and confirm
pin by pin.

## Why ESP-IDF, and how LiteForth fits into it

Bare-metal ESP32 bring-up (PLL, flash cache/MMU, the 2nd-stage bootloader
that maps and optionally decrypts the app) is deeply tied to ESP-IDF, and
the point of this target is to *use* IDF's Wi-Fi 6 / BLE, OTA and security.
So LiteForth is an **IDF application component**, not a from-scratch image.

- **RTOS.** IDF runs FreeRTOS. `app_main()` is already a FreeRTOS task;
  LiteForth's `lfQuit()` REPL loop runs there (or in a task of its own) and
  blocks on the console. Wi-Fi/BLE/OTA run in their own IDF tasks. LiteForth's
  "app runs while the terminal is idle" model (`loadTIB` stepping the VM)
  works unchanged inside its task; where it would busy-spin, yield with a
  short `vTaskDelay` so other tasks run. One LiteForth instance = one task.
- **Security / RoT.** Don't port the CH32 dual-core scheme (it assumes two
  cores and internal flash). Lean on the chip: **Secure Boot v2** (the ROM
  verifies the bootloader; the bootloader verifies the app, RSA/ECDSA) and
  **flash encryption** (XTS-AES, key in eFuse). OTA with rollback is IDF's
  `esp_ota_*` + the `otadata` partition — the same "install on trial, confirm
  or roll back" shape as LiteForth's updater, but done by IDF. LiteForth's
  sealed-package format could still wrap *application/block* content later; the
  firmware RoT itself is Espressif's. Keep `SECURITY_LEVEL` 0 for the initial
  dev port.
- **Flash model.** The app executes from flash via cache; LiteForth must not
  program the partition it runs from. Give LiteForth its **own data
  partition** (`lfflash`, below) and implement `open-flash`/`close-flash`/
  `save-pages` on it with `esp_partition_erase_range` / `write` (IDF disables
  the cache around these internally — the analogue of the CH32 ramfunc rule).
  `lfflash.bin`-style blocks go on the microSD's exFAT volume, same as CH32,
  through the shared `exfat.c` + `card.c` once SD-over-SPI works.

## The target contract → IDF mapping

Every target implements the same headers the desktop and CH32 ports do.
Shared `src/*.c` (VM, interpreter, sealed, exfat, blake2s, ctr, monocypher,
ymodem, update) compile unchanged via `lf_*.c` wrappers.

| Target file | Must provide (from the headers) | ESP-IDF mapping |
|---|---|---|
| `serial_io.c` | `serial_open/close`, `serial_ready`, `serial_busy`, `serial_getc`, `serial_putc`, `serial_binary`, `set_terminal_mode` | `usb_serial_jtag` driver (console), or `uart` for the CH340 path |
| `flash.c` | `flash_init` (hand the VM a flash region), `flash_program`, `flash_locked`, `flash_set_locked`, `flash_rndkey` | a dedicated `lfflash` partition via `esp_partition_*`; `flash_rndkey` from `esp_random`/TRNG |
| `lftime.c` | `lfGetTimeMicroSec`, `lfWatchdogPing`, `lfRtcInit/Get/Set`, `lfRtcSetAlarm`, `lfRtcAlarm`, `lfStandby`, `lfReboot`, `lfRevision` | `esp_timer_get_time`, `esp_task_wdt_reset`, the RTC timer + `esp_sleep_*`, `esp_restart`, chip rev from `esp_chip_info`/eFuse |
| `board.c` | `board_load`, `lfAPI_enroll`, `lfAPI_ecdcTest` | UID from the eFuse MAC (`esp_efuse_mac_get_default`); keys/enroll deferred (stub at level 0) |
| `blocks.c` | the `blk_*` family (read/write, sectors, staging, pages, dir) | SD over **SPI** (`sdspi_host` + `sdmmc`), then the shared `card.c` + `exfat.c`; **needs the GPIO map** |
| `lcd.c` | `lcd_init`, `lcd_tx`, `lcd_decode` (the ST7789 bus) | `esp_lcd` ST7789 panel driver over SPI2; **needs the GPIO map** |
| `main.c` | `lfInitPointers`, `lfReset`, and `app_main` → `lfQuit` | `app_main()` inits console + flash, then `lfQuit()` |
| `options.h` | tunables (`SYSTEM_BLOCKS`, pool size, `VM_ASM`, …) | sized for 384 KB SRAM; PSRAM for the pool if useful |

`vm_rv32.S` (the hand-written RV32IMC `vmExec`) should work on the C5's
RV32IMAC core — the same path the CH32H417 uses — so define `VM_ASM`. Verify
the assembler accepts it under `riscv32-esp-elf` and that the Zc/Xhwlp
extensions don't change anything it relies on; otherwise fall back to the C
`vmExec`.

## Partition layout (draft, 16 MB flash)

```
# name,     type, subtype,  offset,   size
nvs,        data, nvs,      0x9000,   0x6000
otadata,    data, ota,      0xf000,   0x2000
phy_init,   data, phy,      0x11000,  0x1000
ota_0,      app,  ota_0,    0x20000,  0x200000   # 2 MB
ota_1,      app,  ota_1,    0x220000, 0x200000   # 2 MB  (OTA rollback)
lfflash,    data, fat,      0x420000, 0x100000    # 1 MB  LiteForth flash pages
# remainder free for recordings / FAT / spiffs as needed
```

Two OTA app slots give IDF's trial/confirm/rollback. `lfflash` is
LiteForth's programmable flash (colon definitions, the boot image); it is
*not* the running app. Sizes are a starting point — tune once the image size
is known.

## Milestones

1. **REPL over USB-Serial-JTAG.** `app_main` → console → `lfQuit`; VM +
   `serial_io` + `flash` on the `lfflash` partition so `:` definitions
   persist. No SD, LCD, or security. *(No board GPIO map needed.)*
2. **ST7789 LCD** via `esp_lcd` (the demo clock/graphics). *Needs the pinout.*
3. **microSD over SPI** → `card.c` + `exfat.c` → the `blk_*` family and the
   block file. *Needs the pinout.*
4. **Wi-Fi / BLE / OTA** exposed to Forth as words, using IDF's stacks and
   `esp_ota_*`.
5. **Security**: enable Secure Boot v2 + flash encryption; decide how much of
   the sealed format wraps application/block content on top of IDF's RoT.

## Open questions to resolve before coding past milestone 1

- The board **schematic / GPIO map** (panel, touch, SD, RGB, FPC).
- The **chip revision** (SDIO host and secure-boot key manager depend on it).
- Touch controller part and bus; panel bus width and backlight pin.
- Whether `vm_rv32.S` assembles cleanly under `riscv32-esp-elf`.
- ESP-IDF version to pin (v5.x with ESP32-C5 support).
