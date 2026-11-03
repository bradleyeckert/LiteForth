# ESP32-C5 target (ESP-IDF application)

LiteForth on the ESP32-C5, built as an **ESP-IDF application** so it can use
Espressif's Wi-Fi 6 / BLE stacks, OTA, and secure boot + flash encryption
rather than a bespoke root of trust. Board: the **NM-CYD-C5** "Cheap Yellow
Display". See `doc/ESP32C5/esp32c5-notes.md` for the chip/board survey, the
target-contract → IDF-API mapping, the partition layout, and the milestones.

## Status

**Scaffolding only — milestone 1 (REPL over USB-Serial-JTAG) in progress.**
The build configuration and app entry are here; the target drivers
(`serial_io.c`, `flash.c`, `lftime.c`, `board.c`, and the `lf_*.c` wrappers
that pull in the shared `src/*.c`) are the next step and are listed, not yet
written, in `main/CMakeLists.txt`. It does not build end to end yet.

This mirrors how the CH32H417 target lives in the repo as a project you build
locally (there with MounRiver; here with `idf.py`), not in CI — CI has no
ESP-IDF toolchain.

## Build (once the drivers land)

```
idf.py set-target esp32c5
idf.py build
idf.py -p <port> flash monitor      # the C5's USB-Serial-JTAG USB-C port
```

Needs ESP-IDF v5.x with ESP32-C5 support (pin the exact version in
`esp32c5-notes.md`). The console is the chip's USB-Serial-JTAG; the board's
other USB-C (CH340 UART) is an alternative monitor.

## Layout

```
CMakeLists.txt        project (includes $IDF_PATH/tools/cmake/project.cmake)
sdkconfig.defaults    target esp32c5, 16 MB flash, PSRAM, console = USB-Serial-JTAG
partitions.csv        two OTA app slots + an lfflash data partition
main/
  CMakeLists.txt      the component: target drivers + lf_*.c wrappers + src/ include
  app_main.c          app_main() -> LiteForth init -> lfQuit()
  (to come) serial_io.c flash.c lftime.c board.c options.h lf_*.c
  (later)   blocks.c (SD over SPI) lcd.c (ST7789) -- need the board GPIO map
```

The shared interpreter/VM (`src/*.c`) compiles unchanged through `lf_*.c`
wrappers, the same convention as the CH32H417 target.
