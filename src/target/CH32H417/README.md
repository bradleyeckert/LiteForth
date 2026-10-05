# CH32H417 target

Not built by the makefile. Open `SimulateCDC.wvsln` in MounRiver Studio;
it holds two projects, `V3F` and `V5F`, one per core.

## Layout

| Path | What it is |
|---|---|
| `Common/` | WCH's SimulateCDC sources (USB driver, descriptors, UART bridge, `hardware.c`), shared by both cores |
| `V3F/`, `V5F/` | Per-core MounRiver projects: `User/` (`main.c`, interrupts, clock setup) and project files |
| `SRC/` | WCH's SDK, copied from [openwch/ch32h417 `EVT/EXAM/SRC`](https://github.com/openwch/ch32h417/tree/main/EVT/EXAM/SRC) (commit 6d1e469): `Core/`, `Debug/`, `Ld/` (linker scripts), `Peripheral/` (standard peripheral library), `Startup/` |
| `serial_io.c/.h` | LiteForth's terminal over USB CDC (below) |

The projects reach `Common/` and the `SRC/` folders as linked folders
(`PARENT-1-PROJECT_LOC/...` in each `.project`), so the folder builds on
its own wherever the repo is checked out. To update the SDK, replace `SRC/`
with a newer copy of WCH's `EVT/EXAM/SRC`.

MounRiver's build output (`V3F/obj/`, `V5F/obj/`) and per-machine
workspace state (`.mrs/`) are not tracked.

## serial_io.c: terminal over USB CDC

`serial_io.c` implements `serial_io.h` (the same interface as
`src/target/desktop/serial_io.h`) on the USBFS controller as a USB CDC
serial port. It uses the USB device driver from WCH's SimulateCDC example,
[EVT/EXAM/USBFS/DEVICE/SimulateCDC](https://github.com/openwch/ch32h417/tree/main/EVT/EXAM/USBFS/DEVICE/SimulateCDC),
without changes:

| From `Common/` | Used for |
|---|---|
| `ch32h417_usbfs_device.c/.h` | Enumeration, CDC class requests, the endpoint interrupts |
| `usb_desc.c/.h` | Descriptors (VID 1A86, PID FE0C) |
| `UART.c/.h` | The `Uart` state and buffers the interrupt fills, and TIM2 (100 us tick) |

That example bridges USB to USART4 by calling `UART_DataRx_Deal()` and
`UART_DataTx_Deal()` in its main loop. LiteForth replaces that loop, so
**don't call either function**: they would compete with `serial_io.c` for the
same buffers.

### Wiring it up

In the V3F or V5F project, add the LiteForth sources (`src/*.c` and
`serial_io.c`) and their include paths, then replace the
loop at the end of `Hardware()` in `Common/hardware.c`:

```c
    RCC_Configuration( );
    TIM2_Init( );                    /* serial_io.c's output timeout */
    UART_Init( 1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY );
    USBFS_RCC_Init( );
    USBFS_Device_Init( ENABLE );

    serial_open( NULL, 0 );
    lfQuit( );                       /* see src/target/desktop/main.c */
    serial_close( );
```

`UART_Init` sets up the `Uart` state. It also configures USART4 on PF3/PF4,
and the driver does that again whenever the host sets the line coding, so
those pins stay USART4. To free them, remove the USART/GPIO setup from
`UART_CfgInit` in `UART.c`.

### Behavior

- **Input.** Each OUT packet lands in one of 16 64-byte slots. When 14 are
  full the endpoint NAKs, so a fast paste waits on the host instead of being
  lost.
- **Output.** `serial_putc` fills a 64-byte packet. A full packet is sent at
  once; a partial one is sent when the program next calls `serial_ready`,
  `serial_getc` or `serial_close`. `loadTIB` polls `serial_ready` while the
  terminal is idle, so output appears without a newline. `serial_busy` is 1
  only while a full packet is waiting for the host, so `lf_putc` keeps the
  app running during long output.
- **No terminal.** If no program has the port open, the host stops reading
  endpoint 3. After `CDC_TX_TIMEOUT` (default 1000 ticks, 100 ms) output is
  discarded until the host reads again, so `emit` never hangs.
- **Line coding is ignored.** Any baud rate works.
