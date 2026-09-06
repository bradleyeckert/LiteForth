# Utilities

- `lfcom` is a UART-terminal bridge for terminal emulation

## lfcom

`lfcom` is a cross-platform command line utility that pipes a TTY or COM port
to stdio to provide terminal emulator functionality.
The system terminal, like Linux xterm or Windows command line, has a history buffer
in cooked mode.
The terminal lets you edit a line before you send it by pressing \<Enter>.

In raw mode, the terminal sends keystrokes immediately. Raw mode is used for editors.

The terminals of Linux and Windows have some shortcomings:

- Key events have different escape codes between Windows and Linux
- You can't switch between raw and cooked modes with escape sequences.

`lfcom` accepts escape sequences to control the system functions that switch between
raw and cooked modes.
When in raw mode, mouse and resize events are enabled,
so they will send escape sequences.
Linux key and mouse events are translated to Windows format.

Text in LiteForth source blocks assumes UTF-8 and double-width CJK,
as do most terminals.

For Linux, most distros use a modern terminal like GNOME Terminal or Konsole (KDE)
that default to system settings, which are usually UTF-8 and double-width CJK.

For the Windows command prompt, `chcp 65001` enables UTF-8.
the Windows terminal displays double-wide CJK automatically.
