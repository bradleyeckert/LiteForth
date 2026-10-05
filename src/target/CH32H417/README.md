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

The projects reach `Common/` and the `SRC/` folders as linked folders
(`PARENT-1-PROJECT_LOC/...` in each `.project`), so the folder builds on
its own wherever the repo is checked out. To update the SDK, replace `SRC/`
with a newer copy of WCH's `EVT/EXAM/SRC`, then redo the one local change:
`SRC/Ld/V3F/Link_v3f.ld` ends the V3F's `RAM` 32K early to leave room for
`RAM_SHARED` (below).

MounRiver's build output (`V3F/obj/`, `V5F/obj/`) and per-machine
workspace state (`.mrs/`) are not tracked.

## Terminal over USB CDC, across the two cores

The V3F runs the USB device and nothing else; the V5F runs the app
(LiteForth, or for now an echo test) and talks to the host through
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
is used unchanged:

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
- **No terminal.** If no program has the port open, the host stops reading
  the IN endpoint. After `CDC_TX_TIMEOUT` (default 1000 ticks, 100 ms) the
  V3F discards the tx ring until the host reads again, so the V5F's output
  never blocks for long. The same happens while the device is not
  configured, so text written at power-up (such as the echo test's banner)
  is lost unless a terminal is already open.
- **Line coding is ignored.** Any baud rate works.

### Echo test

`V5F/User/main.c` runs `Echo()`: it echoes each byte, and on Enter prints
`(N bytes) V5F> `, where N is the length of the line the V5F counted, so a
reply visibly comes from the V5F. Open the port in a terminal and press
Enter to get the first prompt. Debug `printf` output goes to USART1 (V3F)
and USART8 (V5F).

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
the V3F never wakes and the device never enumerates. The V3F's debug UART
(USART1, 9600 baud, also on the WCH-LinkE) shows how far it got:

| Line | Means |
|---|---|
| `V3F SystemCoreClk:...` | V3F booted |
| `V3F waiting for V5F` | V3F woke the V5F and went to sleep |
| `V3F wake up` | the V5F signalled; if this never comes, look at the V5F (image, flashing) |
| `USB CDC bridge to V5F running on USBFS controller` | USB device started |
| `USB address N` | the host reset the device and assigned an address |
| `USB configured` | enumeration finished; the COM port should appear |

The V5F prints `V5F SystemCoreClk:...` and `V5F released HSEM0, running
echo` on its own debug UART, USART8.

### Next: LiteForth on the V5F

Add the LiteForth sources (`src/*.c`) and include path to the V5F project,
then replace `Echo()` with:

```c
    serial_open( NULL, 0 );
    lfQuit( );                       /* see src/target/desktop/main.c */
    serial_close( );
```
