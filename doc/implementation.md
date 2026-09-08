# LiteForth Implementation

LiteForth runs on either a PC or MCU. Functionality that differs between them are:

- `termio.c` is the terminal interface, UART for MCU, terminal or TTY/COM for PC
- `flash.c` is the flash memory implementation or simulation
- `periph.c` is optional peripheral simulation
- `main.c` is initialization and startup code
- `vm.s` is the platform-specific token interpreter, if `vm.c` is not used.

The common functionality is:

- `forth.c` is the QUIT interpreter
- `config.h` is the system configuration
- `vm.c` is the token interpreter

The `src` folder contains the common functionality.
Platform-specific functionality is in a folder for that platform.

For a PC-based console app, the language is C11, which uses safe file io and threads.
Threads are used to make console I/O non-blocking.

For an MCU-based platform, the chip vendor's IDE is used.

## termio

Terminal I/O uses 6 functions: 

- `int kbopen(char *port_name, int baud)` initializes the COM port or terminal I/O, return 0 if okay
- `int kbfull(void)` returns the status of the input port: -1 if full, 0 if no char
- `int kbready(void)` returns the status of the output port: -1 if ready, 0 if busy
- `int kbgetc(void)` returns the next byte from the input port, -1 if none
- `void kbputc(char c)` sends a byte to the output port
- `void kbclose(void)` closes the open port, if necessary

The console app can use either a terminal or a COM port as stdio.
Port "TERM" is the terminal.

## flash

In a console app, flash and mass storage are simulated by binary files.
The default filenames are `lfflash.bin` and `lfblocks.bin`,
but may be changed by command line options `-f` and `-b`.
`flash.c` gets the flash sector size and page programming size from `options.h`.
File options.h contains #defines for SIMNUMBLOCKS and FLASHSECTORSIZE.
File flash.c contains functions that typically return 0 if okay, other if error:

- `#define SIMNUMBLOCKS` is the number of blocks the simulator has for mass storage.
- `int mass_init(char *filename)` initializes the SD card or mass storage.
- `int mass_read(uint32_t blk, uint32_t *dest)` reads a 4KB block, return 0 if okay.
- `int mass_write(uint32_t blk, uint32_t *src)` writes a 4KB block.
- `uint32_t flashmem[SIMNUMBLOCKS<<10]` is the flash memory, real or simulated.
- `int flash_init(char *filename)` initializes the flash for reading.
- `int flash_sector(uint32_t *m)` copies `m` to a Flash sector of size FLASHSECTORSIZE.
- `int flash_rndkey(void)` fills in a random key at the end of the sector.

MCU Flash is the physical flash, in a section placed by the linker file.
It is read-only, except for a "flash and erase" function that flashes an entire sector
from RAM.

MCU sector flashing takes some time, depending on the sector size. For a 128 KB sector,

| MCU | Typ erase | Max erase | Typ prog rate | Min prog rate |
|-----|-----------|-----------|---------------|---------------|
| STM32H743 | 1.1s | 2.2s | 240 KB/s | 120 KB/s |
| CH32H417 | 0.06s | 0.3s | 300 KB/s | 150 KB/s |

Erase is a blocking operation, so the terminal may hang for a second or two
while the sector erases and programs.

The mass storage size is read from block 0.
Mass storage can be write-protected via its header field in block 0. 
It's like the write-protect switch on a 3.5" floppy, but more like a slider.

## periph

Specialized devices on the APB and AHB busses may be simulated in the desktop version.
Maybe not all of the devices, since most development will be done on the target.

- `periph-wr` writes to peripheral space
- `periph-rd` reads from peripheral space

## main

The MCU app contains startup code and calls `quit` in `forth.c`.

The console app has command line options that allow you to:

- Select a COM port instead of stdio
- Change the mass storage filename
- Change the flash memory filename

## forth

Implements the QUIT interpreter and the C API.

- `quit` is the QUIT loop

## vm

The token interpreter is in either C or assembly.

- `vmstep` runs 1 step of infinite steps until the return stack underflows.

