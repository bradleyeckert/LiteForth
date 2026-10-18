# Running Forth on the nanoCH32H417

The nanoCH32H417 board has two USB-C ports: **WCHLinkE**, the on-board
debug probe (programming, plus a serial port for debug messages), and
**USB-FS**, where LiteForth's terminal appears as a USB serial (CDC) port.

## What you need

- The nanoCH32H417 board, two USB-C cables, and a microSD card.
- Optional: the 1.54" 240 x 240 LCD module from the kit.
- [MounRiver Studio](http://www.mounriver.com/) (Windows), to build and
  download the firmware.
- A terminal program. [kwai](https://github.com/bradleyeckert/kwai) was
  written for embedded targets with cooked input and has LiteForth-specific
  features; any terminal that raises DTR works (see below).
- Python 3, only if you rebuild the block image (`forth/build.bat`).

## 1. Program the board

- Install MounRiver and open the solution
  `src/target/CH32H417/LiteForth.wvsln`. It holds two projects, `V3F`
  (the USB core) and `V5F` (the Forth core).
- Connect the board's **WCHLinkE** port to your PC.
- The WCHLinkE must be in RISC-V mode. A solid blue light means ARM mode.
  In RISC-V mode the blue light is off, except that it blinks while
  programming. To switch modes, either:
  - in MounRiver, open *Tools > WCH LinkUtility* and change the probe's
    mode in its *Connected WCH-Link List*, or
  - unplug the WCHLinkE port, hold the board's **ModeS** button while you
    plug it back in, then let go. (The other button, **IAP**, is for
    updating the probe's firmware.)

  The probe remembers its mode.
- Click *Build All Projects* (Ctrl+B).
- Select the **V5F** project and click *Download* (F8). Its download
  programs both cores. Downloading from the V3F project erases the chip and
  writes only the V3F's half, which leaves LiteForth dead.

A download erases the whole chip, including the Forth dictionary in flash,
so after each one you load the dictionary again (step 4).

## 2. Connect a terminal

Plug the **USB-FS** port into the PC. It shows up as a new COM port;
`kwai -l` lists the ports and shows the board's (`(USB CDC)`, product
`CH32H417`, with the chip's ID as its serial number). Then:

```
kwai COM17
```

with your port's name. Press Enter: LiteForth answers `ok>`.

With another terminal program:

- **Turn local echo on.** LiteForth reads a line at a time and doesn't
  echo what you type.
- **Raise DTR.** The board sends nothing while DTR is off. PuTTY, Tera
  Term, minicom and screen raise it; kwai does too.
- The baud rate doesn't matter. Lines may end in CR, LF or CR LF.

The WCHLinkE's own COM port carries the debug messages of both cores
(USART1, 9600 baud): startup, the SD card it found, the RTC, and errors.
It's worth keeping open in a second window.

## 3. Prepare the microSD card

LiteForth keeps its source code and fonts in 4 KB blocks, in one file on
the card: `lfblocks.fb4`, in the root directory of its exFAT volume. The
repo's `bin/lfblocks.fb4` is ready-made: `core.f`, the other Forth source,
and two fonts.

- **The card must be exFAT.** Cards over 32 GB come that way; format a
  smaller one as exFAT (File Explorer: right-click, *Format...*, *exFAT*).
  [doc/sdcard.md](../../../doc/sdcard.md) has the details for Windows,
  macOS and Linux.
- **Optional: rebuild the image.** After changing the Forth source, run
  `forth/build.bat` in the `forth` folder. It writes the source into
  `bin/lfblocks.fb4`, and also rebuilds the fonts, which needs Noto Sans
  CJK SC installed and `py -m pip install pillow fonttools`.
- **Copy `bin/lfblocks.fb4` to the card's top folder** by drag and drop,
  then eject the card. Its block 0 holds the `LITEFORTH` signature, which
  LiteForth needs before it will write to it.

## 4. Load the dictionary

- If you have the LCD module, plug it in.
- Put the card in the board and power it up (or reset it). The debug port
  says what it found, for example `V5F blocks: 16384 blocks at sector ...`;
  `capacity .` at the terminal shows the same number.
- Enter `1 load`.

Block 1 compiles the Forth lexicon (`core.f`, from block 7) and the demo
application (block 50) into flash and saves a boot image. The board now
has a Forth dictionary in flash, and boots from it at every reset until
the next download from MounRiver.

## 5. Try it

The demo application starts at the next reset, or now with `cold`. It
bumps `counter` in a loop that runs whenever the terminal is idle, so
`counter @ .` shows a bigger number each time.

With the demo running, `try` starts a clock on the LCD, run by a second
task in the multitasker. The real-time clock starts at zero (1970) when the board
first gets power, and the board has no battery to keep it, so set it after
powering up. `clock!` takes the time in UTC:

```
0 30 21 16 10 2026 clock!
rtc@ dup .date space .time
try
```

(seconds, minutes, hours, day, month, year: 21:30:00 UTC on 16 October
2026). The clock keeps its time through resets, just not through a power
loss.

Some other things to type: `words` lists the dictionary, `7 list` shows a
block of source, and `hi` prints a Chinese proverb (kwai shows CJK text).

## If something goes wrong

- **MounRiver can't find the probe or won't download:** check the
  WCHLinkE's blue light. A solid light means ARM mode, which is how some
  kits ship; switch it to RISC-V mode (step 1). The probe keeps whatever
  mode it was last set to, so a board that worked before can still
  surprise you after someone else used it.
- **The app misbehaves at boot:** press Ctrl+X three times in a row (kwai:
  `~r`). The board restarts without starting the app; `cold` starts it
  again.
- **No `ok>`:** check that the terminal raises DTR, and look at the debug
  port. Downloading from the V3F project instead of the V5F one leaves
  the USB port dead; download from the V5F project again.
- **`1 load` fails at once or `capacity .` shows 0 or 1:** the board
  didn't find `lfblocks.fb4` on an exFAT card, or its block 0 lacks the
  `LITEFORTH` signature. The debug port says which (see the table in
  [doc/sdcard.md](../../../doc/sdcard.md#3-on-the-board)).

## Changing the source

With the card in the board, kwai's `~x file` sends a source file into
blocks over the terminal (YMODEM): for example `~x core.f`, run in the
`forth` folder. The `( BLOCK n )` lines in the file say where each block
goes, as with `putblocks.py`. A binary file such as a font goes in with a
block number: `~x cjk24.fnt 100`. Other terminals can send them too: type
`0 ymodem` (or `100 ymodem`), then send the file with YMODEM. Then `1 load`
compiles the dictionary again from the new source. See
[doc/blocks.md](../../../doc/blocks.md) for blocks and the block editor in
[doc/editor.md](../../../doc/editor.md).
