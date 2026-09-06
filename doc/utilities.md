# Utilities

- `lfcom` is a UART-terminal bridge for terminal emulation

## lfcom

The terminals of Linux and Windows (command line) have some shortcomings:

- Key events have different escape codes between Windows and Linux
- You can't switch between raw and cooked modes with escape sequences.

Both of these are fixed with `lfcom`. It is a cross-platform utility that
accepts escape sequences to switch between raw and cooked modes.
Linux key events are translated to Windows format.

When in raw mode, mouse and resize events are enabled,
so they will send escape sequences (in Windows format).
