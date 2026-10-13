# File Types

- `.fb4` Forth Blocks in 4K (128 x 32) format
- `.fs` Forth source code text file with BLOCK tags
- `.f` Forth source code text file

Forth source code is kept in text files for compatibility with repositories.
Code that is meant to reside in blocks uses the `.fs` file extension.
In a desktop environment, `putblocks.py` inserts `.fs` files into a blocks image file like `lfblocks.fb4`.

With RealTerm connected to an MCU target over a serial port, use `xmodem` and follow the prompts for
File > Transfer > XMODEM > Send.
