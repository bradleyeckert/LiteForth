# LiteForth Implementation

LiteForth runs on either a PC or MCU. Functionality that differs between them are:

- `termio.c` is the terminal interface, UART for MCU, terminal or TTY/COM for PC
- `flash.c` is the flash memory implementation or simulation
- `blocks.c` is the interface to block memory, such as SPI Flash, SD card, etc.
- `main.c` is initialization and startup code
- `vm.s` is the platform-specific Machineforth interpreter, if `vm.c` is not used.

The common functionality is:

- `options.h` is the system configuration
- `forth.c` is the QUIT loop and interpreter
- `comp.c` is the Forth compiler`
- `tools.c` is a lexicon of little tools for `forth.c`, etc.
- `memalloc.c` is a bump allocator to manage RAM
- `vm.c` is the Machineforth interpreter
- `api0.c` is the C API for Forth
- `utils.c` is optional extensions to the C API

The `src` folder contains the common functionality.
Platform-specific functionality is in a folder for that platform.

For a PC-based console app, the language is C99 or C11.
For an MCU-based platform, the chip vendor's IDE is used.

## blocks

The mass storage size is read from block 0. A blank file would contain mostly ASCII spaces.
Mass storage can be write-protected via its header field in block 0. 
It's like the write-protect switch on a 3.5" floppy, but more like a slider.
A blank "Block 0" starts with the following minimum text:
```
LITEFORTHBLK 1 1000 100 100
```
The fields are:

1. Signature (Identifies it as LiteForth blocks)
1. Version Number of block format
1. Block size in bytes (hex format)
1. Total Blocks allocated in this file (hex format, use SIMNUMBLOCKS)
1. First write-protected block (hex format)

- `int blk_init(char *filename)` initializes mass storage, reads the write-protect value.
- `int blk_read(uint32_t blk, uint32_t *dest)` read a 4KB block, return 0 if okay.
- `int blk_write(uint32_t blk, uint32_t *src)` write a 4KB block, return error if write-protected.

## main

The MCU app contains startup code and calls `quit` in `forth.c`.

The console app has command line options that allow you to:

- Select a COM port instead of stdio
- Change the mass storage filename
- Change the flash memory filename
