# Terminals

LiteForth may use either a UART or `stdio` as its terminal connection.
It expects an xterm-compliant terminal like GNOME Terminal, Alacritty, iTerm2,
Kitty, or Windows Terminal. It also expects UTF-8 and double-width CJK.

Sometimes you want your terminal to be in cooked mode.
Sometimes you want it in raw mode.

- Cooked mode: REPL loop (console input)
- Raw mode: Screen editors or other cursor-heavy apps

The Linux and Windows ports of LiteForth natively switch the terminal between raw and cooked modes.
The embedded (MCU) ports cannot switch, you must do it manually.

LiteForth expects a line to end in either \n or \r.
Sending \r\n will give you `ok>ok>`.
Terminals (that supply stdin) only send \n.
Serial terminals like PuTTY send \r.

-----
## LiteForth in Linux 

The default terminal is okay.

You can give a serial port name as an argument to connect to a serial port instead of `stdio`.

## Linux terminal, Embedded LiteForth

To connect the terminal to a UART:
```bash
socat -G -,raw,echo=0 /dev/ttyUSB0,raw,b115200,echo=0 # raw mode
socat - /dev/ttyUSB0,raw,b115200,echo=0            # cooked mode
```
## LiteForth in Windows

The default terminal is mostly okay. Use Windows Terminal, not `cmd.com`.

There are many alternatives to Windows Terminal. Some that may work are:

- Alacritty: A cross-platform, GPU-accelerated terminal emulator known for pure speed.
- WezTerm: A GPU-accelerated terminal emulator for Windows.
- Mintty: The default terminal wrapper installed alongside Git Bash, MSYS2, and Cygwin.

You can provide a serial port name as an argument to connect to a serial port instead of `stdio`.
In that case, you could use a serial cable emulator like `com0com`.
A terminal emulator (like PuTTY) would treat it as a serial-connected MCU port.

To demonstrate CJK character output, LiteForth sends "幸运狐" in UTF-8.
The following Windows terminals were tried:

- ConEmu, displays correctly.
- Command Prompt, displays square boxes instead of CJK. 
- Windows Powershell, displays square boxes instead of CJK. 

## Windows terminal, Embedded LiteForth

Some terminal emulators give you cooked mode input.

### Tera Term VT

`Setup -> Terminal` pops up the terminal settings. 

- New-line Receive: select LF
- New-line Transmit: select LF
- Local Echo: Checked-on

`Setup -> Save setup` lets you save your setup. 
Tera Term VT displays CJK correctly. 

### PuTTY

Right Click on the Title Bar, select Change Settings.

- Local Echo: Force On
- Local Line Editing: Force On

When you get PuTTY set up like you want, click on Default Settings and hit the Save button.
PuTTY displays CJK correctly.

### Raw mode only

Realterm and Wezterm. Wezterm is CLI.

`wezterm serial COM5 --baud 115200`

-----
## mouse usage

In cooked mode, the mouse belongs to the terminal.

In raw mode, (Button-event Tracking), click and release events for the
left and right mouse buttons send an escape sequence that includes the row and column.
If you press a button and drag the mouse, the terminal sends a stream of mouse positions.
Any-event Tracking is not used, or necessary. Reporting all mouse movement is a bit much.

In an on-target-hardware text editor, mouse events sent by `lfcom` could be:

- Left click: Place the cursor
- Left click and hold (over 0.25s): Pop up a tooltip if the mouse didn't move
- Left release: Remove the tooltip and copy highlighted text to the clipboard
- Left mouse drag: Hilight text to copy to the clipboard
- Right click: Paste the clipboard
- Right release: Ignore
- Right mouse drag: Ignore

The tooltip would display the word's stack picture temporarily. It would also display
glossary information below the editor window until the next tooltip is triggered.

Pasting into the block editor inserts or overwrites, depending on ins/ovr status.
Insertion simulates a text editor, eating the trailing blanks on a line.
Text that overflows a line goes onto the next line after inserting a blank line.
If this results in text being lost from the last line of the block, an error is reported.
The line overflow occurs a blank-delimited-word at a time to avoid splitting the text.

## Other terminal notes

Some OLED monitors do not use a RGB sequence of vertical bars for pixels. Who knew?
That wrecks ClearType, so you have to work without ClearType.
Terminals still look okay, but watch out for that when shopping for monitors.