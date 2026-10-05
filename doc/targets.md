# Targets

## Desktop

`main.c` is a console app that connects to a terminal through either stdio or a serial port.

## CH32H417

Three board options for CH32H417 development are:

- MuseLab [nanoCH32H417](https://www.aliexpress.us/item/3256811965302801.html)
- Maker go [CH32H417 Development Board](https://www.aliexpress.us/item/3256812798140393.html)
- WCH [CH32H417 Evaluation Board](https://www.aliexpress.us/item/3256810611289080.html)

### nanoCH32H417

The [nanoCH32H417](https://github.com/wuxx/nanoCH32H417) is sold on
[Tindie] (https://www.tindie.com/products/johnnywu/nanoch32h417-development-board/)
, [Alibaba](https://www.aliexpress.us/item/3256812394981804.html)
, and Amazon.
It has a built-in WCHLinkE for ISP and USB UART. It also has an SDcard slot and FPC for LCD.
For implementing USB-CDC, there is a USB-C connector on the MCU's FS port (not HS or SS).
The CC pins can be used for PD, but the board can only handle 5V.

The most relevant MCU pins are:

- WCHLinkE UART: PA9 and PA10
- LCD (DC CS SCLK MOSI RST) = PD9, PB12, PB13, PB15, PD8
- SD (CLK CMD D0-D3 ) = PB11, PB10, PE8, PE9, PE10, PE11

[sdcard.md](sdcard.md) explains how to partition a microSD card for LiteForth's blocks.

The UART pins conflict with USB OTG_ID and OTG_VBUS pins, but that is not a problem if you only use
the USB-C USB-FS connector as a device (such as a CDC port)

VBAT is not connected to anything but a capacitor. That domain would be powered by an internal switch.

### CH32H417 black board

[CH32H417 Development Board](https://wiki.icbbuy.com/doku.php?id=developmentboard:ch32h417qeu6)
has a buck regulator for accepting 5-20V from the USB-C.
It has a built-in WCHLinkE for ISP and USB UART.
The large number of header pins (3-row pin headers on the bottom) make it a great dev board for prototyping.
VBAT is on one of those pins. Other relevant MCU pins are:

- WCHLinkE UART: PB6 and PB7

### CH32H417 green board

The [CH32H417QEU6-R0-1v1](https://www.wch.cn/downloads/CH32H417EVT_ZIP.html)
can be found on Alibaba [here](https://www.aliexpress.us/item/3256812607953519.html)
\( or [here](https://www.aliexpress.us/item/3256810602171927.html)
or [here](https://www.aliexpress.us/item/3256810611289080.html) \).

It does not have a built-in WCHLinkE. Theoretically, if you could get the board into boot mode,
you could download over USB (P4) using `WCHISPTool.exe`.
I could not do it, so I wired up a WCHLinkE dongle. Hopefully, WCH stopped promoting boot mode.
Factory bootloaders should not be a thing. They are just one more attack vector.

You do get a VBAT pin, which you can wire to a coin cell to keep the RTC running through power cycles.

#### ISP

The pinout of P3 aligns with the WCHLinkE pinout, so wiring one up is easy.
If you plug the WCHLinkE into your PC's USB and the blue LED is on, it is in ARM mode.
You have to turn that blue LED off.
Select "WCH-LinkRV" from the **Active WCH-Link Mode** dropdown list, near the bottom.
Click the Set button. The WCHLinkE will disconnect and re-enumerate with the blue LED off.
Then it will be in RISC-V mode.

The MounRiver IDE `Tools` --> `WCH-LinkUtility` launches the ISP tool.
Below the tool bar, there are dropdown menus for **Core** and **Series**.
Select RISC-V and CH32H41X. Query Chip Info (Alt+F3) gets the chip's UID and flash size.

#### UART

Wire the WCHLinkE `TX` and `RX` pins (as labeled on the silkscreen) to match the UART usage
of either of the other boards.

| LinkE | nano | WCH |
|-------|------|-----|
| TX | PA10 | PB7 |
| RX | PA9  | PB6 |
| | UART1 | UART8 |

#### MounRiver

You are going to use both cores of the CH32H417, V3F and V5F, whether you like it or not.
I tried unchecking the "V3F" checkbox with "New MounRiver Project" and the binary would not run.
The V3F core runs first, then starts up the V5F core.
The V3F and V5F are separate projects, which you can compile and update separately.

The demo project strips down the V3F code so it launches the V5F and spins forever.
The V5F code prints "V5F running..." once a second, but occasionally there is a "5F running..."
or a "Vg..." or a "5g...".
Dropping the baud rate from 115200 to 9600 baud fixed it. But the LinkE was connected over 20 cm fly wires.
The 25 MHz crystal (HSE) is being used as the timebase.

The green board has P1 and P4 (USB-C and USB-A) wired in parallel, so they are USB-FS like the other boards.
The trick then is to get P1 (or P4) to enumerate as CDC and use that instead of a UART.



