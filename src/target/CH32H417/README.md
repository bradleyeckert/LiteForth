# CH32H417 target

Not built by the makefile. Open `LiteForth.wvsln` in MounRiver Studio;
it holds two projects, `V3F` and `V5F`, one per core.

To get started on the nanoCH32H417 board, follow [quickstart.md](quickstart.md).

Where the chip, the reference manual and WCH's SDK disagree is collected
in [doc/ch32h417-notes.md](../../../doc/ch32h417-notes.md).

## Layout

| Path | What it is |
|---|---|
| `Common/` | Compiled into both projects. WCH's SimulateCDC sources (USB driver, descriptors, `UART.c`, `hardware.c`), plus `cdc_shared.h` (the rings both cores share) and `cdc_bridge.c/.h` (the V3F's USB-to-ring pump) |
| `V3F/`, `V5F/` | Per-core MounRiver projects: `User/` (`main.c`, interrupts, clock setup) and project files |
| `SRC/` | WCH's SDK, copied from [openwch/ch32h417 `EVT/EXAM/SRC`](https://github.com/openwch/ch32h417/tree/main/EVT/EXAM/SRC) (commit 6d1e469): `Core/`, `Debug/`, `Ld/` (linker scripts), `Peripheral/` (standard peripheral library), `Startup/` |
| `V5F/User/serial_io.c/.h` | LiteForth's terminal I/O for the V5F, over the shared rings (below) |
| `V5F/User/flash.c/.h`, `blocks.c/.h`, `options.h` | LiteForth's flash pages, block storage on the microSD card (in `lfblocks.fb4`, found by `src/exfat.c` and `src/card.c`), and options for the V5F (below) |
| `V5F/User/sdio.c/.h` | WCH's SD card driver for the SDIO peripheral, copied unmodified from `EVT/EXAM/SDIO/SDIO_SD` (commit 3924a05) |
| `V5F/User/main.c/.h`, `lftime.c/.h`, `lf_*.c`, `lf_vm_rv32.S` | LiteForth on the V5F: startup, the microsecond time base, and wrappers that compile LiteForth's `src/*.c` (and the assembly VM, `src/vm_rv32.S`) into the project (below) |

The projects reach `Common/` and the `SRC/` folders as linked folders
(`PARENT-1-PROJECT_LOC/...` in each `.project`), so the folder builds on
its own wherever the repo is checked out. To update the SDK, replace `SRC/`
with a newer copy of WCH's `EVT/EXAM/SRC`, then redo the local changes,
each marked `LiteForth` in the file:

- `SRC/Ld/V3F/Link_v3f.ld` ends the V3F's `RAM` 32K early to leave room
  for `RAM_SHARED`.
- `SRC/Ld/V5F/Link_v5f.ld` reserves the LiteForth flash and raises the
  V5F's C stack from 2K to 16K.
- `SRC/Debug/debug.h` and `debug.c` send both cores' `printf` to USART1,
  with a hardware semaphore around each string (see *Debug output*).

MounRiver's build output (`V3F/obj/`, `V5F/obj/`) and per-machine
workspace state (`.mrs/`) are not tracked.

## Terminal over USB CDC, across the two cores

The V3F runs the USB device and nothing else; the V5F runs the app
(LiteForth) and talks to the host through
`serial_io`. The two cores share only a pair of byte rings:

```
host --EP2 OUT--> V3F  cdc_bridge_poll --rx ring--> V5F  serial_getc
host <--EP3 IN--- V3F  cdc_bridge_poll <--tx ring-- V5F  serial_putc
```

### Shared memory (`Common/cdc_shared.h`)

The rings sit at `0x20178000`, the top 32K of the V3F's 512K SRAM, which
both cores can address (the address WCH's `HSEM_DataSharing` example uses).
`SRC/Ld/V3F/Link_v3f.ld` shrinks the V3F's `RAM` by 32K so its data and
stack stay below it; the V5F's RAM is its own TCM and can't overlap.
Nothing is linked into the region. Both cores use the fixed address, and
the V3F empties the rings (`cdc_shared_init`) before it wakes the V5F.

Each ring (4K) has one producer and one consumer, and each index is
written by only one core, so no lock or HSEM is needed; `fence`
instructions order the data before the index that publishes it.

### V3F: `Common/cdc_bridge.c`

`Hardware()` starts the USB device as WCH's SimulateCDC example does, then
loops on `cdc_bridge_poll()` in place of the example's `UART_DataRx_Deal()`
and `UART_DataTx_Deal()` (which bridged USB to USART4). The USB driver from
[SimulateCDC](https://github.com/openwch/ch32h417/tree/main/EVT/EXAM/USBFS/DEVICE/SimulateCDC)
is used with two changes (edits marked `LiteForth`):
`ch32h417_usbfs_device.c` records DTR in the shared block, and the device
has a serial number: `USB_SerialInit` (`usb_desc.c`, called by
`USBFS_Device_Init`) writes the chip's unique ID (ESIG `UNIID1` and
`UNIID2` at 0x1FFFF7E8) as 16 hex digits into the serial number string,
so the host can tell boards apart (`kwai -l` shows it). The reference
manual calls the ID 96 bits, but `UNIID3` reads `E339E339`, erased flash.

| From `Common/` | Used for |
|---|---|
| `ch32h417_usbfs_device.c/.h` | Enumeration, CDC class requests, the endpoint interrupts |
| `usb_desc.c/.h` | Descriptors (VID 1A86, PID FE0C; the serial number is the chip's unique ID) |
| `UART.c/.h` | The `Uart` state and packet slots the interrupt fills, and TIM2 (100 us tick) |

`UART_Init` sets up the `Uart` state. It also configures USART4 on PF3/PF4,
and the driver does that again whenever the host sets the line coding, so
those pins stay USART4. To free them, remove the USART/GPIO setup from
`UART_CfgInit` in `UART.c`.

### V5F: `V5F/User/serial_io.c`

Implements `serial_io.h` (the same interface as
`src/target/desktop/serial_io.h`) on the rings. `serial_ready` and
`serial_busy` never block; `serial_getc` waits for a byte and
`serial_putc` waits for space. The V5F never touches the USB hardware.

### Behavior

- **Input.** Host packets go into the rx ring as space allows. If the V5F
  stops reading, the ring fills, then the driver's 16 packet slots, and
  then the endpoint NAKs, so a fast paste makes the host wait instead of
  losing data.
- **Output.** Whenever the IN endpoint is free, the V3F sends up to 64
  bytes from the tx ring. Whatever the V5F writes while a packet is in
  flight goes in the next one, so output batches itself and needs no
  flushing. A zero-length packet follows a full final packet.
  `serial_busy` is 1 only while the 4K tx ring is full.
- **No terminal.** A terminal program raises DTR when it opens the port
  and drops it when it closes (CDC `SET_CONTROL_LINE_STATE`, wValue bit 0).
  The driver records it in `host_open` in the shared block (cleared on bus
  reset). While it's 0, or the device isn't configured, the V3F sends
  nothing and the tx ring holds the V5F's output, such as LiteForth's
  banner, until a terminal opens the port. `serial_putc` never waits then:
  once the 4K ring is full, further bytes are dropped, so the oldest 4K is
  what the terminal sees. The app keeps running offline (LiteForth runs it
  with `vmRun` while waiting for input).
- **Open but not reading.** If the port is open but the host stops reading
  the IN endpoint, after `CDC_TX_TIMEOUT` (default 1000 ticks, 100 ms) the
  V3F discards the tx ring until the host reads again, so the V5F's output
  never blocks for long.
- **Terminals must assert DTR.** PuTTY, Tera Term, minicom and screen do.
  A program that opens the port with DTR off sees no output.
- **Line coding is ignored.** Any baud rate works.

### Flashing and startup

Build both projects, then download from the **V5F** project. Its download
is `obj/Merge.Bin`: the V3F's image (at 0) merged with the V5F's (at
`0x10000`), so it programs both cores. The V3F project's download erases
the whole chip and writes only the V3F image, which leaves the V5F with
nothing to run.

That matters because the V3F doesn't start USB on its own. With
`Run_Core == Run_Core_V3FandV5F` (the default in `SRC/Debug/debug.h`):

1. The V3F empties the rings, wakes the V5F, and sleeps in STOP mode.
2. The V5F boots and releases HSEM0, which wakes the V3F.
3. The V3F calls `Hardware()`, which starts USB and the bridge.

So if the V5F image is missing or stale, or the V5F stops before step 2,
the V3F never wakes and the device never enumerates. The debug UART
(USART1, 9600 baud, also on the WCH-LinkE) shows how far it got. Both cores
print there; the lines appear in about this order:

| Line | Means |
|---|---|
| `V3F security: not locked down: ...` | read protection off, write protection not as `stage1_layout.h` has it, or the boot ROM's downloads on; at `SECURITY_LEVEL` 2 (`Common/security.h`) the V5F then stays off (`doc/admin/firmware-updates.md`, *The lock-down check*) |
| `V3F update: ...` | only when stage 1 did something: a package installed, refused, on trial, rolled back, or an image that fails its check (`V3F/User/stage1.c`, `doc/admin/firmware-updates.md`) |
| `V3F: recovery mode: ...` | no working V5F image: the V5F stays off, and stage 1 answers on the USB terminal instead (send a package with kwai's `~x FILE -2`; `doc/admin/firmware-updates.md` section 7) |
| `V3F: starting the V5F` | V3F booted, woke the V5F and went to sleep |
| `V3F: the V5F didn't start within ...` | the V5F didn't release HSEM0 within `STAGE1_V5F_START_MS`: the V3F resets the chip (a trial boot, or after 3 in a row, recovery) |
| `V3F: the V5F is running` | the V5F booted and released HSEM0, waking the V3F; if this never comes, look at the V5F image (flashing) |
| `V3F: built ..., GCC ...` | the V3F's build time; USB device started |
| `V5F crumbs: reset by ...` | why the chip reset, and how many crumbs the previous run left: listed after a watchdog or lockup reset (not a software one, such as `reboot`), or always with `CRUMBS_TRAIL 1` (`V5F/User/crumbs.c`) |
| `V5F: RTC failed ...` | only if the RTC didn't start |
| `V5F blocks: ...` | what the V5F found on the SD card (see *Blocks*) |
| `V5F: LiteForth starting, ...` | booting from flash, or flash is blank |
| `V3F: USB configured` | enumeration finished; the COM port should appear |

Each line costs about 1 ms per character at 9600 baud, and `printf` waits
for the UART, so the boot prints only what tells you something.

### Debug output

Both cores' `printf` go to USART1 (WCH's examples give the V5F USART8).
`_write` in `SRC/Debug/debug.c` holds hardware semaphore HSEM2 while it
sends a string, so a string from one core is never split by the other's;
whole strings from the two cores still interleave as they come. If the
other core holds the semaphore while the UART sits idle for a while (it was
restarted or hung mid-string), `_write` prints anyway, so a stuck V5F can't
silence the V3F. `USART_Printf_Init` leaves USART1 alone once it's enabled,
so the second core, or a restarted V5F, doesn't reconfigure it under a
character in flight; the first core's baud rate (9600) wins.

HSEM use: HSEM0 wakes the V3F at boot, HSEM1 restarts the V5F (Ctrl+X),
HSEM2 guards the debug UART.

## LiteForth flash and blocks (V5F)

### Flash: `V5F/User/flash.c`

The code flash is mapped at `0x08000000` (960K in dual-flash mode, 480K in
single). The V3F's image takes the first 64K and the V5F's the next 128K.
`SRC/Ld/V5F/Link_v5f.ld` reserves the next **256K, `0x08030000`-`0x0806FFFF`**,
for LiteForth (`__lf_flash_start`, `__lf_flash_size`), and fails the link
if the V5F image would grow into it. That range is valid in either flash
mode.

`V5F/User/options.h` divides it into `RAM_PAGE` = 4 flash pages of
`FLASH_PAGE_CELLS` = 16384 cells (64K each), so page 0, which holds the
dictionary, is 64K instead of the desktop's 16K. `VM_LOG2_PAGES` stays 3.
Reading the flash costs wait states, and the VM reads each instruction it
runs from page 0, so `flash.c` keeps a copy of the first
`FLASH_SHADOW_PAGES` pages (1 by default, in `options.h`) in a static
array in DTCM and maps those pages to the copy (`flash_page`); the VM reads
the other pages in place. `close-flash` calls `flash_program`, which
erases the page (one 64K block) and programs it from the RAM cache with
WCH's `FLASH_ROM_ERASE` / `FLASH_ROM_WRITE`, reads it back to check, and
refreshes the page's RAM copy from the flash.
A page that hasn't changed isn't rewritten. Erased flash on this chip reads `0xE339E339`, not
`0xFFFFFFFF` (`FLASH_ERASED_WORD` in `flash.h`), so `0 12 dump` of blank
flash shows that pattern. While a page is open its 64K
cache comes from the memory pool (`POOL_CAPACITY` = 96K, in the V5F's
256K DTCM). Other geometries work too, as long as a page is a whole number
of 8K sectors and all pages fit in the 256K; `options.h` checks both.

**The board's key record: `0x08070000`.** The 8K sector after LiteForth's
flash holds the board's record for sealed block files (`V5F/User/board.c`,
[doc/admin/encrypted-blocks.md](../../../doc/admin/encrypted-blocks.md)): its X25519
private key, made by `enroll`, and the lowest revision and release key
serial it takes. It lies inside the 480K of single-flash mode. A download
keeps it (below), but *Erase All* wipes it, so the board then needs
`enroll` again (a new key, so a new line in `boards.csv` and files sealed
again).

**Sealed block files.** If `lfblocks.fb4` on the card is sealed, `blk_init`
mounts it with the board's record and the root key in
`V5F/User/sealed_root.h`, which the application's build supplies (copy
`sealed_root.example.h`, or `sealblocks.py root-header`; git ignores it).
At `SECURITY_LEVEL` 2 the board takes only sealed files: a plain
`lfblocks.fb4` offers no blocks and `-1 ymodem` refuses plain files
([doc/admin/updates.md](../../../doc/admin/updates.md) covers the whole process and
these options).

**Security level.** `SECURITY_LEVEL` in `Common/security.h` (0, 1 or 2;
or `-DSECURITY_LEVEL=n` in both projects) sets the security of both
cores. 0, the repository's setting, is development: the regular Forth
from a plain `lfblocks.fb4`, every peripheral register, the board's key
from its UID, and stage 1's `adopt`. 1 is the same with the product's
I/O: Forth reaches only the peripherals in `V5F/User/io_allow.h`. 2 is a
product: sealed files only, the key from the RNG (`enroll` again, and a
new `boards.csv` line), no `adopt`, and no V5F on a board that isn't
locked down: it stays in recovery, and the debugger can still reprogram
it.

After adding or changing `sealed_root.h`, clean the V5F project before
building: `board.c` finds it with `__has_include`, and MounRiver's
dependency lists don't notice a header that didn't exist at the last
build, so an incremental build keeps the old `board.o` (the board then
says `no root key built in`).
The debug UART then says, for example,
`V5F blocks: sealed file, revision 5, release key 1, AES-128, 121 blocks (checked in 31 ms)`,
or why it refused the file. Its blocks are checked and decrypted in
`blk_status` as each read finishes.

**Peripheral registers in page 5.** `VM_IO_PAGE` = 5 in `options.h` maps
the peripheral block at `0x40000000` (`IO_BASE`, through UHSIF, `0x38400`
bytes) as VM page 5: writable, not executable. A register at
`0x40000000 + o` is at VM address `0x280000 + o/4`, so GPIOA's first
register (`0x40010800`) is `0x284200`. Nothing checks the gaps between
peripherals, so stay on real registers. Reading a register can have side
effects (a data register pops its FIFO), and a bit-field store reads,
modifies and writes the whole register, so use whole-cell `!` on
write-1-to-clear flags.

**Downloading keeps it.** The V5F project downloads `Merge.Bin` (both
cores' code, which ends below `0x08030000`) with Clear CodeFlash off
(`"clearcodeflash": false` in `LiteForth_V5F.wvproj`), so only the sectors
the image covers are erased: LiteForth's flash pages and the key record
survive a download, and neither `1 load` nor `enroll` is needed after it.
*Erase All* in MounRiver's download dialog (or Clear CodeFlash turned back
on) wipes both.

### Blocks: `V5F/User/blocks.c`

Blocks live in `lfblocks.fb4`, a file in the root directory of the
microSD card's exFAT volume; [doc/sdcard.md](../../../doc/sdcard.md)
explains how to prepare the card. The card is on the SDIO peripheral,
which on the nanoCH32H417 uses CLK PB11, CMD PB10 and D0-D3 PE8-PE11
(alternate function 8). `blocks.c` uses WCH's driver (`sdio.c`) only to
bring the card up; transfers are its own (below).

At startup `blk_init` brings up the card and calls `card_find_blocks`
(`src/card.c`), which mounts the exFAT volume (`src/exfat.c`: the first
MBR partition of type `07`, or a volume at sector 0) and looks for
`lfblocks.fb4` in the root directory:

| Card | Capacity | Writable |
|---|---|---|
| No card, no exFAT volume, no `lfblocks.fb4`, or a fragmented one | 0 | no |
| `lfblocks.fb4` whose block 0 doesn't start with `LITEFORTH` | 1 (block 0, to see what's there) | no |
| `lfblocks.fb4` starting with `LITEFORTH` | its ValidDataLength / 4096 | yes |

Block *n* is card sectors `base + 8n` to `base + 8n + 7`, where `base` is
the file's first sector. USART1 shows the outcome, e.g.
`V5F blocks: lfblocks.fb4, 121 blocks at sector 34816 (exFAT)`. The card
is read once at startup, so a card inserted later needs a restart (Ctrl+X
three times will do). If transfers fail, try a slower SD clock: raise
`SDIO_TRANSFER_CLK_DIV` in `sdio.h`.

Underneath, every transfer is a run of 1 to 64 card sectors
(`blk_sector_read_imm`, `blk_sector_write_imm`); a block is 8 of them.
Sector writes are fenced to the exFAT volume (`blk_write_fence`), so they
can't reach the partition table. They are there for exFAT code in Forth,
which doesn't have words for them yet ([doc/exfat.md](../../../doc/exfat.md)).

`blk_read_imm` and `blk_write_imm` start a transfer of 1 to 8
consecutive blocks (`BLK_MAX_LENGTH`, up to 32 KB, one CMD18 or CMD25)
and `blk_status` reports on it (0 done, negative failed, 1 reading,
2 writing); the Forth words are `read-block` and `write-block`
(`( a u n -- )`, n blocks from block u) and `block-status`. `blk_read` and `blk_write` are built
on them and wait.

Block transfers run by DMA, driven from `blocks.c` rather than WCH's
driver, whose DMA mode waits for the transfer and uses DMA1 channel 1:
the V3F's USART4 code has DMA1 channels 1 (TX) and 2 (RX), as the DMA
registers on a running board show. `blocks.c` uses DMA2 channel 1,
routed to the SDIO request (111) by DMAMUX channel 9 (`DMA_MuxChannelConfig`
stores the request minus 1: USART4's 91 and 92 read back as `5A` and
`5B`). `blk_*_imm` sets up the data path, sends CMD18 or CMD25 and starts
the DMA; each `blk_status` call then checks the SDIO flags, sends CMD12
when the data is through and, for a write, asks the card (CMD13) until it
has finished programming. No interrupts are used. A step that makes no
progress for `BLK_TIMEOUT_US` (1 s) fails with -126; the time is checked
only while the step is unfinished, so asking late is harmless. DMA can reach the V5F's DTCM,
where the block buffers are (the RM lists DMA among the DTCM's bus
masters, `MEMARY_CFGR`). `BLK_DMA 0` in `blocks.c` goes back to WCH's
polling transfers.

### LCD: `V5F/User/lcd.c`

The nanoCH32H417's LCD connector takes a 1.54" 240 x 240 ST7789 IPS
module. `lcd.c` drives it on SPI2 behind the same interface as the
desktop simulator (`lcd_init`, `lcd_tx`, `lcd_decode`), so `core.f`'s
`LCD`, `lemit` and `lcls` work unchanged:

| Signal | Pin |
|---|---|
| SCLK | PB13 (SPI2, AF5) |
| MOSI | PB15 (SPI2, AF5) |
| CS | PB12 |
| D/C | PD9 |
| RES | PD8 |

SPI2 runs in mode 3 at its fastest prescaler, as in the board's SPI_LCD
example. The V3F's `main` first sets VIO18, the supply of the high-speed
GPIOs, to 3.3 V in software (`PWR_VIO18LevelCfg(PWR_VIO18Level_MODE3)`),
as that example does: at the board's hardware setting SPI2 runs but the
module doesn't see valid highs, and the panel never changes. `lcd_init` sets up the pins and SPI2 the first time and keeps
the palette; `lcd_tx` drives RES, CS and D/C from bits 15 to 13 and sends
the byte; `lcd_decode` streams a font code's pixels. Bytes go out as
soon as the transmit buffer is free, and the driver waits for the bus to
go idle only before it changes RES, CS or D/C. SPI2 is full duplex and
stops sending while its receive buffer is full, so every wait reads and
discards what came in. A wait that times out (SPI2 stuck) makes `lcd-tx`
and `lcd-decode` give -21 until the next `lcd-init`, instead of hanging
the V5F and with it the terminal. The panel shows inverted
colors until INVON, which `LCD` sends (the simulator's controller 1 does
the same).

## LiteForth on the V5F

### How it's built

MounRiver compiles every `.c` file in `V5F/User/`. LiteForth's portable
core (`src/*.c`) comes in through one wrapper per file, `lf_forth.c`,
`lf_vm.c` and so on, each a single `#include "../../../../forth.c"`. So the
project files need no changes when the core gains or loses code, and each
core file's quoted includes find the core headers beside it in `src/`,
and the target headers (`options.h`, `serial_io.h`, `flash.h`, `blocks.h`,
`lftime.h`, `main.h`) here in `V5F/User/`. The target files reach
`src/` headers by relative path for the same reason. A new core source file
needs a new wrapper.

The VM itself runs in assembly: `options.h` defines `VM_ASM`, so
`lf_vm_rv32.S` assembles `src/vm_rv32.S` (vmExec for RV32IMC) and `vm.c`
calls it instead of its C `vmExec`. It takes about half the RV32
instructions per VM instruction of the C version built by clang at `-O2`,
and 45% of it at `-Os` (`make bench` in `src/target/desktop/unit_tests/vmasm`). `V5F/User` is on the assembler's
include path (*GNU RISC-V Cross Assembler > Includes*) so that its
`#include "options.h"` works. The micro-op handlers sit in fixed 64-byte
slots; if a change makes one too big, the assembler stops with an
`.org` error. Comment out `VM_ASM` to go back to the C version.

The V5F project compiles as **gnu11** (WCH's projects use gnu99): the core
uses a C11 `u8"..."` string. If MounRiver ever shows a `u8` error in
`forth.c`, check *Project Properties > C/C++ Build > Settings > GNU RISC-V
Cross C Compiler > Optimization > Language standard*.

The image uses about 35K of the V5F's 128K for code (all of it runs from
ITCM), and about 164K of its 256K DTCM, mostly the 96K memory pool and the
64K copy of flash page 0. The
C stack is 16K (`__stack_size` in `Link_v5f.ld`).

### Startup (`V5F/User/main.c`)

After it wakes the V3F, the V5F starts the time base (`lfTimeInit`), opens
the terminal (first, because starting the SD card can take a second and
`serial_open` is what tells the V3F a restart worked), maps
LiteForth's memory as the desktop `main.c` does (flash pages 0-3 in the
code flash, the RAM page from the pool, the block file on the SD
card, the rest unmapped), and runs
`lfQuit` on the USB terminal. If flash page 0 holds a saved image (cell 1,
the boot record pointer that `core.f` stores, isn't `0xE339E339` or
`0xFFFFFFFF`), it boots from it
(`SYS_OPTION_BOOTING`). `bye` restarts `lfQuit`. USART1 shows
`V5F: LiteForth starting, booting from flash` or `..., flash is blank`.

`lftime.c` drives `lfGetTimeMicroSec` from SysTick1: a 1 ms interrupt
counts milliseconds in 64 bits, and the counter supplies the
microseconds. WCH's `Delay_Us`/`Delay_Ms` reprogram SysTick1, so the V5F
must not call them after `lfTimeInit`. `lfWatchdogPing` does nothing yet.

### Real-time clock and standby (`V5F/User/lftime.c`)

The RTC (reference manual chapter 6) is a 32-bit seconds counter with a
prescaler and an alarm, in the backup domain, so it keeps counting
through resets and Stop mode. `lfRtcInit`, called after `lfTimeInit`,
leaves a running RTC alone. After the backup domain has lost power it
starts the board's 32.768 kHz crystal (LSE), or the LSI (about 40 kHz, a
few percent off) if the crystal doesn't start within 3 s, sets the
prescaler for 1 Hz and zeroes the counter and the alarm. It writes
`RCC_BDCTLR` (the backup domain control register) only as whole 32-bit
values, one step at a time, each read back before the next (`bd_write`),
as typing them at the REPL did (BDRST, `1`, `41`, `141`). With WCH's
helpers, which write the register a byte at a time (`RCC_LSEConfig`) or
read-modify-write it, the board ended up with BDCTLR at `100`: RTCEN,
but no clock and the crystal off. An RTC found enabled with no clock is
set up again from a backup domain reset (BDRST). USART1 shows `V5F: RTC
running, N s`, with BDCTLR and RTC_CTLRL (`143` and `20` or `28` when it
runs on the crystal). The nanoCH32H417 has no battery on VBAT, so the
time is lost when the board loses power; `rtc@` below 1767225600 (2026)
means it hasn't been set since. If the RTC doesn't count at all, check
whether VBAT needs tying to 3.3 V (the RM's advice without a battery).

The words are `rtc@`, `rtc!`, `alarm!`, `alarm?` and `standby`
(`doc/glossary.md`). The CH32H417 has no Standby mode, so `standby`
(`lfStandby`) uses Stop mode, which the chip enters only when both cores
are in deep sleep:

1. The V5F gives the terminal up to 200 ms to take what was printed,
   sets `standby` in the shared block, and waits up to 100 ms for the
   V3F to sleep (`NVIC->CSTAR[0]`).
2. The V3F's bridge sees the request (`cdc_standby` in `cdc_bridge.c`),
   detaches USB (the host sees the port go away) and sleeps for good.
3. The V5F turns off its interrupts and SysTick1, sets up EXTI line 17
   (the RTC alarm) as an event, and enters Stop with WFE, as WCH's
   `PWR/Stop_Mode` example does with a pin.
4. The alarm wakes it. The V5F writes `CDC_RTC_WAKE_MAGIC` to `rtc_wake`
   in the shared block and resets the chip (`NVIC_SystemReset`).
5. Everything boots as after the reset pin, and `lfRtcInit` takes the
   mark, so `alarm?` is true once. A press of the reset pin during Stop
   leaves no mark, so `alarm?` stays false.

The voltage regulator stays in normal mode during Stop. Low-power mode
also needs VDDK lowered first (WCH's example does it with undocumented
`SYS_CFGR0` bits), which can come later.

### Using it

Open the COM port with **local echo on**: LiteForth doesn't echo what you
type. Enter ends a line; a terminal that sends CR LF gets an extra empty
line, which is harmless. LiteForth doesn't handle Backspace in a line yet.

The flash starts blank, so the first thing to do is load `forth/core.f`
by sending it as a text file from the terminal. USB flow control makes
the host wait while LiteForth works, so it can go at full speed. `core.f`
compiles into flash page 0 and saves a boot image; after the next reset
the V5F boots it. Remember that a MounRiver download erases it (see
*Flash* above).

### Escape hatch: Ctrl+X three times

Pressing **Ctrl+X three times in a row** restarts the V5F in safe mode:
it boots the saved dictionary but doesn't start the app, and the
terminal says so (`Safe boot (Ctrl+X x3): the app is not running.`);
`cold` starts the app. So an app that misbehaves at boot can always be stopped and
fixed. USB stays up, and the terminal stays connected.

How it works:

1. The V3F's bridge (`Common/cdc_bridge.c`) counts consecutive `0x18`
   bytes as it copies input into the rx ring. The bytes still go to the
   V5F, so a single Ctrl+X is an ordinary character. While `ymodem`
   receives a file, the V5F sets `binary` in the shared memory
   (`serial_binary`) and the V3F doesn't count them: the file may hold any
   bytes.
2. On the third, the V3F sets `safe_boot` in the shared memory to
   `CDC_SAFE_BOOT_MAGIC` and takes and releases HSEM1.
3. The V5F's `HSEM_Handler` (`V5F/User/main.c`) jumps to its reset entry,
   `handle_reset`, which reruns its startup code and `main`. That startup
   code doesn't touch the clocks or anything the V3F uses. LiteForth never
   disables interrupts, so a runaway Forth app can't block this.
4. `main` sees `safe_boot`, clears it and sets `SYS_OPTION_NO_AUTORUN`
   (`-o 256` on the desktop), so `lfQuit` boots without starting the app.
   `serial_open` drops the input that was waiting and counts the start in
   `v5f_starts`. A later `bye` boots normally again.
5. If `v5f_starts` hasn't changed after 500 ms (`CDC_RESTART_WAIT_MS`),
   the V3F resets the whole chip. Then USB drops and the terminal must
   reconnect; `safe_boot` is kept in the shared SRAM, so the V5F still
   comes up in safe mode, provided the SRAM keeps its contents through a
   system reset.

The V3F's USART1 shows `Ctrl+X x3: restarting the V5F in safe mode`,
then `V5F restarted` or `V5F didn't restart: resetting the chip`.

It can't help if the V5F stopped reading input long enough for 4K of
typing to pile up in the rx ring: the USB link then holds the PC back and
the Ctrl+X presses can't arrive. A C-level crash on the V5F already resets
the chip (WCH's `HardFault_Handler` calls `NVIC_SystemReset`).
