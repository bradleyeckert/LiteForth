# CH32H417 target

Not built by the makefile. Open `SimulateCDC.wvsln` in MounRiver Studio;
it holds two projects, `V3F` and `V5F`, one per core.

## Layout

| Path | What it is |
|---|---|
| `Common/` | Compiled into both projects. WCH's SimulateCDC sources (USB driver, descriptors, `UART.c`, `hardware.c`), plus `cdc_shared.h` (the rings both cores share) and `cdc_bridge.c/.h` (the V3F's USB-to-ring pump) |
| `V3F/`, `V5F/` | Per-core MounRiver projects: `User/` (`main.c`, interrupts, clock setup) and project files |
| `SRC/` | WCH's SDK, copied from [openwch/ch32h417 `EVT/EXAM/SRC`](https://github.com/openwch/ch32h417/tree/main/EVT/EXAM/SRC) (commit 6d1e469): `Core/`, `Debug/`, `Ld/` (linker scripts), `Peripheral/` (standard peripheral library), `Startup/` |
| `V5F/User/serial_io.c/.h` | LiteForth's terminal I/O for the V5F, over the shared rings (below) |
| `V5F/User/flash.c/.h`, `blocks.c/.h`, `options.h` | LiteForth's flash pages, block storage on the microSD card, and options for the V5F (below) |
| `V5F/User/sdio.c/.h` | WCH's SD card driver for the SDIO peripheral, copied unmodified from `EVT/EXAM/SDIO/SDIO_SD` (commit 3924a05) |
| `V5F/User/main.c/.h`, `lftime.c/.h`, `lf_*.c` | LiteForth on the V5F: startup, the microsecond time base, and one-line wrappers that compile LiteForth's `src/*.c` into the project (below) |

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
is used with one change: `ch32h417_usbfs_device.c` records DTR in the
shared block (edits marked `LiteForth`).

| From `Common/` | Used for |
|---|---|
| `ch32h417_usbfs_device.c/.h` | Enumeration, CDC class requests, the endpoint interrupts |
| `usb_desc.c/.h` | Descriptors (VID 1A86, PID FE0C) |
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
| `V3F SystemCoreClk:...` | V3F booted |
| `V3F waiting for V5F` | V3F woke the V5F and went to sleep |
| `V5F SystemCoreClk:...` | V5F booted; if this never comes, look at the V5F image (flashing) |
| `V5F released HSEM0, running LiteForth` | V5F signalled the V3F |
| `V3F wake up` | V3F woke from STOP |
| `USB CDC bridge to V5F running on USBFS controller` | USB device started |
| `V5F blocks: ...` | what the V5F found on the SD card (see *Blocks*) |
| `V5F: LiteForth starting, ...` | booting from flash, or flash is blank |
| `USB address N` | the host reset the device and assigned an address |
| `USB configured` | enumeration finished; the COM port should appear |

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
The VM reads the pages in place. `close-flash` calls `flash_program`, which
erases the page (one 64K block) and programs it from the RAM cache with
WCH's `FLASH_ROM_ERASE` / `FLASH_ROM_WRITE`, then reads it back to check.
A page that hasn't changed isn't rewritten. Erased flash on this chip reads `0xE339E339`, not
`0xFFFFFFFF` (`FLASH_ERASED_WORD` in `flash.h`), so `0 12 dump` of blank
flash shows that pattern. While a page is open its 64K
cache comes from the memory pool (`POOL_CAPACITY` = 96K, in the V5F's
256K DTCM). Other geometries work too, as long as a page is a whole number
of 8K sectors and all pages fit in the 256K; `options.h` checks both.

**Downloading erases it.** The download settings erase the whole chip
(`"erase": true`, `"clearcodeflash": true` in the `.wvproj`; "Erase All" in
MounRiver's download dialog), so every download also wipes the LiteForth
flash. To keep what LiteForth has compiled across downloads, turn that off
so only the sectors in the image are erased.

### Blocks: `V5F/User/blocks.c`

Blocks live in a raw partition on the microSD card;
[doc/sdcard.md](../../../doc/sdcard.md) explains how to prepare the card.
The card is on the SDIO peripheral, which on the nanoCH32H417 uses CLK PB11,
CMD PB10 and D0-D3 PE8-PE11 (alternate function 8). `blocks.c` uses WCH's
driver (`sdio.c`) in polling mode: its DMA mode would use DMA1 channel 1,
which the V3F's UART code also uses.

At startup `blk_init` brings up the card, reads the MBR and takes the first
partition of type `DA`:

| Card | Capacity | Writable |
|---|---|---|
| No card, no MBR, or no type `DA` partition | 0 | no |
| Partition not on a 64 KB boundary, or block 0 doesn't start with `LITEFORTH` | 1 (block 0, to see what's there) | no |
| Aligned partition starting with `LITEFORTH` | its size in 4 KB blocks | yes |

Block *n* is sectors `start + 8n` to `start + 8n + 7`. USART1 shows the
outcome, e.g. `V5F blocks: 16384 blocks at sector 15392768` or
`V5F blocks: block 0 doesn't start with LITEFORTH: read-only`. The card is
read once at startup, so a card inserted later needs a restart (Ctrl+X
three times will do). If transfers fail, try a slower SD clock: raise
`SDIO_TRANSFER_CLK_DIV` in `sdio.h`.

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

The V5F project compiles as **gnu11** (WCH's projects use gnu99): the core
uses a C11 `u8"..."` string. If MounRiver ever shows a `u8` error in
`forth.c`, check *Project Properties > C/C++ Build > Settings > GNU RISC-V
Cross C Compiler > Optimization > Language standard*.

The image uses about 35K of the V5F's 128K for code (all of it runs from
ITCM), and about 100K of its 256K DTCM, mostly the 96K memory pool. The
C stack is 16K (`__stack_size` in `Link_v5f.ld`).

### Startup (`V5F/User/main.c`)

After it wakes the V3F, the V5F starts the time base (`lfTimeInit`), opens
the terminal (first, because starting the SD card can take a second and
`serial_open` is what tells the V3F a restart worked), maps
LiteForth's memory as the desktop `main.c` does (flash pages 0-3 in the
code flash, the RAM page from the pool, the block partition on the SD
card, the rest unmapped), and runs
`lfQuit` on the USB terminal. If flash page 0 holds a saved image (cell 1,
the boot record pointer that `go.f` stores, isn't `0xE339E339` or
`0xFFFFFFFF`), it boots from it
(`SYS_OPTION_BOOTING`). `bye` restarts `lfQuit`. USART1 shows
`V5F: LiteForth starting, booting from flash` or `..., flash is blank`.

`lftime.c` drives `lfGetTimeMicroSec` from SysTick1: a 1 ms interrupt
counts milliseconds in 64 bits, and the counter supplies the
microseconds. WCH's `Delay_Us`/`Delay_Ms` reprogram SysTick1, so the V5F
must not call them after `lfTimeInit`. `lfWatchdogPing` does nothing yet.

### Using it

Open the COM port with **local echo on**: LiteForth doesn't echo what you
type. Enter ends a line; a terminal that sends CR LF gets an extra empty
line, which is harmless. LiteForth doesn't handle Backspace in a line yet.

The flash starts blank, so the first thing to do is load `scripts/go.f`
by sending it as a text file from the terminal. USB flow control makes
the host wait while LiteForth works, so it can go at full speed. `go.f`
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
   V5F, so a single Ctrl+X is an ordinary character. (Three CANs in a
   row is also XMODEM's cancel sequence.)
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
