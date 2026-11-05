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
- **2.8" 320×240 TFT, ST7789** (18-pin FPC), with **resistive touch via an
  XPT2046** on the shared SPI bus — *not* the capacitive controller the product
  listing implied (the schematic settles it).
- **microSD** slot (SPI, since the C5 has no SD host), on the **same** SPI bus
  as the panel and touch, with its own CS and a card-detect.
- A **WS2812** addressable LED (through a 3.3→5 V level-shifter), an **SC8002B
  mono audio power amplifier + speaker header**, an I²C "TEMP" header, and an
  LP-UART header.
- **Two USB-C:** one to the C5's USB Serial/JTAG, one to a **CH340C** UART
  bridge with auto-download (DTR/RTS → EN/BOOT0). Either can be the console;
  LiteForth uses the CH340 UART.
- Claims dimensions/interfaces "fully compatible with the ESP32-CYD."

**The GPIO map is now confirmed** from the NM-CYD-C5 v1.0 schematic
([NM-CYD-C5-schematic.pdf](NM-CYD-C5-schematic.pdf), RockBase) — see the pin
table below. (The witnessmenow CYD project remains a fine reference for the
board's *peripheral set*, but its GPIO numbers are the original ESP32-WROOM
CYD's and do not apply to the C5; the schematic supersedes them. Note the
earlier guess of *capacitive* touch was wrong: it is a *resistive* XPT2046.)

## ESP-IDF version

**ESP32-C5 needs ESP-IDF v5.4 or later (v5.5 recommended for stable support);
v5.3.x does not support the C5** -- `idf.py set-target esp32c5` fails on it.
Check with `idf.py --list-targets` (esp32c5 must appear). Pin the exact
version here once chosen. (If a build hits the `setuptools<71.0.1` constraint
the installer warns about, run ESP-IDF's `install.bat`.)

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

## Flashing and the download-mode eFuse

Development flashing is `idf.py flash` over the CH340 (UART0), whose auto-reset
puts the chip into the ROM download mode. For a production board the plan is
Secure Boot v2 + flash encryption + **`DIS_DOWNLOAD_MODE`**, with updates over
**OTA** thereafter. `DIS_DOWNLOAD_MODE` disables only the ROM's *download mode*
(the serial/USB reflash path at reset); it does **not** disable the UART
peripheral, so LiteForth keeps using UART0 as its data console after it is
blown -- you just can't reflash over serial any more (OTA only). An optional
*secure download mode* (`ENABLE_SECURITY_DOWNLOAD`) leaves a restricted,
encrypted recovery path. Confirm the exact C5 fuse interactions (UART vs
USB-Serial-JTAG download, secure download) against the TRM before automating.

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

## Pin map (NM-CYD-C5 schematic v1.0)

Module: **ESP32-C5-WROOM-1-N16R8** (16 MB flash, 8 MB PSRAM). From
[NM-CYD-C5-schematic.pdf](NM-CYD-C5-schematic.pdf). The panel, touch and SD
share one SPI bus (SCK/MOSI/MISO) with separate chip-selects.

| Function | GPIO(s) | Notes |
|---|---|---|
| **Shared SPI** (TFT + touch + SD) | SCK `IO6`, MOSI `IO7`, MISO `IO2` | serialize access across the three devices |
| TFT ST7789 (18-pin FPC) | CS `IO23`, DC/RS `IO24`, BL `IO25`, RST = chip `RST` | |
| Touch **XPT2046** (resistive) | CS `IO1`, IRQ `IO3` | on the shared SPI |
| microSD (SPI / 1-bit) | CS `IO10`, CLK `IO6`, CMD/MOSI `IO7`, DAT0/MISO `IO2` | card-detect present |
| **Speaker amp SC8002B** | `IO26` (net `SPEAK_IN`) | **LEDC PWM** → RC low-pass → amp (below) |
| **WS2812** LED | `IO27` | via an AO3400A 3.3→5 V level-shifter |
| Console UART (CH340C) | `U0TXD`/`U0RXD` | auto-download DTR/RTS → EN/BOOT0 |
| Native USB (2nd USB-C) | D+ `IO14`, D- `IO13` | USB-Serial-JTAG |
| I²C "TEMP" | SCL `IO8`, SDA `IO9` | header/sensor |
| LP-UART | RXD `IO4`, TXD `IO5` | header |
| Strap | BOOT0 `IO28` | |

Cautions: `IO6`/`IO7`/`IO2` are one physical SPI bus for three peripherals, so
the drivers must share it (distinct CS lines, and the XPT2046 wants a slower
clock than the panel); `IO2` is also a C5 strapping pin, fine as wired but keep
it released at boot.

**Future: a capacitive-touch variant.** The panel may later be swapped for a
capacitive one. In that case the touch controller moves to **I²C** (not the
shared SPI), and **`IO3` becomes the capacitive panel's IRQ** — the same pin the
XPT2046 uses for `TP_IRQ` today, so the interrupt line is common to both. The
I²C is the board's existing `TEMP` bus (SCL `IO8`, SDA `IO9`) unless a dedicated
one is added; a controller reset line may also be needed (TBD when the part is
known, e.g. GT911/FT6x36/CST816). So the touch driver should sit behind the
target contract as a swappable back-end: an SPI/XPT2046 (resistive) back-end and
an I²C/capacitive one, both presenting the same read-point/IRQ interface, with
`IO3` as the shared IRQ. Only the resistive variant uses the SPI CS (`IO1`).

### Audio: how the speaker is driven

There is **no DAC or codec** in the audio path (the ESP32-C5 has no built-in
DAC). The SC8002B (`U5`, mono bridge-tied-load class-AB, VDD 3.3 V, differential
`VO1`/`VO2` to the speaker header) takes a **single analog net `SPEAK_IN` on
`IO26`**, fed through a two-pole RC reconstruction low-pass (`R4 4.7k`·`C11 1nF`
and `R7 47k`·`C12 100pF`, each ~34 kHz corner) and a 330 nF AC-couple into the
inverting input (`R8 22k`, `R9 68k` feedback, gain ≈ 6× in BTL).

So the C5 **synthesizes the audio digitally on `IO26` and the RC filter averages
it to analog.** The chosen source is **LEDC PWM** (duty = sample, high carrier
well above the ~34 kHz corner); the sigma-delta modulator (`driver/sdm`) is the
higher-fidelity alternative on the same pin if PWM proves too coarse. A
`SHUTDOWN` pin on the amp allows muting when idle.

## Open questions to resolve before coding past milestone 1

- The **chip revision** (SDIO host and secure-boot key manager depend on it).
- Whether `vm_rv32.S` assembles cleanly under `riscv32-esp-elf`.
- ESP-IDF version to pin (must be v5.4+; v5.5 recommended -- see above).
- LEDC audio details: carrier frequency, PWM resolution (bit depth), and the
  sample-feed path (timer ISR vs DMA) for `IO26`.
- Capacitive-touch variant (if adopted): the controller part (GT911/FT6x36/
  CST816…), whether it shares the `TEMP` I²C (`IO8`/`IO9`) or a dedicated bus,
  and whether it needs a reset GPIO. IRQ is `IO3` either way.

## Hardware crypto hooks (accelerators the CH32H417 lacks)

The CH32H417 accelerates only AES (its ECDC engine, reached through
`src/ctr.c`'s `CTR_HW` seam). The ESP32-C5 has more hardware — SHA-1..512, ECC
(P-192/256/384), HMAC-SHA-256, the RSA/ECDSA DS peripheral, AES-128/256 with
DMA, and a TRNG — and the design should reach each through a thin **hook** over
the IDF/ROM driver, so no accelerator sits idle. Each hook follows the pattern
`CTR_HW`/ECDC already set: a C5 wrapper (`lf_*.c`) over the driver, **checked
once against the software reference** (as `ctr_crypt_block_sw` and the CH32's
`ecdc-test` do), with the **software fallback kept** for portability and for the
self-check.

| Hook | C5 hardware | IDF/ROM driver | Software reference | Used for |
|---|---|---|---|---|
| AES (bulk) | AES accelerator (+DMA) | `esp_aes` | `ctr.c` (`CTR_HW` seam, exists) | log stream cipher, any bulk AES-CTR/GCM |
| SHA-256 | SHA accelerator | `esp_sha` | a software SHA-256 | HMAC KDF, **LMS** verification (below) |
| HMAC | HMAC peripheral + eFuse key | `esp_hmac` | software HMAC-SHA256 | key rooting (board long-term key, never read) |
| ECC | ECC accelerator (P-256/384) | mbedTLS ECC / `esp_ecc` | Monocypher / mbedTLS sw | ECDH key agreement; IDF's own secure-boot ECDSA |
| DS | Digital Signature peripheral | `esp_ds` | — (hardware-only key use) | sign/identity with a private key that is never readable |
| TRNG | hardware RNG | `esp_random` | — | one-off keys, file-ids, nonces (as `flash_rndkey`) |

BLAKE2s and Poly1305 have no C5 hardware; they stay in the portable software
profile (desktop/CH32H417). X25519 stays software too but is NIST-approved
(SP 800-186), so it is kept. The TRNG hook is a **SP 800-90A DRBG**
(`esp_random`), which matters because a KDF is only as strong as the entropy
under it. The C5's own log/crypto profile is therefore **NIST-approved end to
end** — AES-256-GCM (SP 800-38D), HKDF-SHA256 (SP 800-56C) and SP 800-108
HMAC-SHA256 key rooting, HMAC-SHA256 (FIPS 198-1) / SHA-256 (FIPS 180-4), the
DRBG, and LMS (SP 800-208) for signatures — and it leans on the symmetric and
hash-based primitives that survive a quantum break. Why each KDF is secure is in
[data-logs.md §3.6](admin/data-logs.md).

### Signatures: prefer LMS (quantum-resistant) over ECDSA

For LiteForth's **own** signature layer — the sealed-package / application-content
signatures and any board identity *we* verify in software — prefer **LMS**
(Leighton-Micali hash-based signatures, RFC 8554; HSS for more signatures;
NIST SP 800-208, which approves LMS/HSS specifically for firmware/software
signing) over ECDSA, for quantum resistance. LMS's security rests on the hash
function, not on discrete-log/factoring, so it is not broken by Shor's
algorithm.

This fits the C5 especially well: **LMS signing and verification are almost
entirely SHA-256**, so the SHA hook above makes verification cheap on-device —
the device only *verifies*, so it carries none of LMS's statefulness. The
**statefulness burden is the signer's** (the offline vendor root must never
reuse a one-time key index); that is a key-management requirement, manageable
for an infrequent offline firmware/content signer, and is exactly SP 800-208's
intended use.

Boundaries and caveats, stated honestly:

- **The chip's Secure Boot v2 is ROM-fixed RSA-3072 / ECDSA** and cannot be made
  to verify LMS; the boot chain stays classical. LMS applies to the signature
  layer *above* the chip's RoT (what the CH32H417 does with Ed25519 today), so
  the quantum-resistant claim is for LiteForth's own packages/content, not for
  the Espressif boot ROM.
- **No LMS in Monocypher:** adopting it adds a vetted LMS/HSS implementation as a
  new SOUP/SBOM component (or a small audited one), tracked like the rest.
- The ECC accelerator still earns its place for ECDH key agreement and for IDF's
  own secure-boot ECDSA; LMS is the preference specifically for *signatures we
  control*.
