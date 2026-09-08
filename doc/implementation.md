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

Flash memory supplies *regions*, which are contiguous runs of *sectors*.
The application region starts with a data structure that is followed by optional
C functions, Forth code, and Forth dictionary. This data structure is loaded at boot
to set up addressing used by the VM.

## serial_io

Terminal I/O uses these functions: 

- `int set_terminal_mode(int enable)` sets the terminal mode if using `stdio`. 1 for raw, 0 for cooked
- `int serial_open(char* name, int baudrate)` initialize the COM port or terminal I/O, return 0 if okay
- `void serial_close(void);` close the open port, if necessary
- `int serial_ready(void)` return the status of the input port: 1 if has a char, 0 if not
- `int serial_busy(void)` return the status of the output port: 0 if ready, 1 if busy, else ERR_\*
- `int serial_getc(void)` return the next byte from the input port, -1 if none
- `int serial_putc(int c)` send a byte to the output port

The console app can use either a terminal or a COM port as stdio.
Port "TERM" is the terminal.

## options

An options.h file sets LiteForth options.

- `#define RAMSIZE` sets the VM's data RAM size in 32-bit cells
- `#define ROMSIZE` sets the VM's data ROM size in 32-bit cells
- `#define BLOCKFILENAME` is the default mass storage file name
- `#define SIMNUMBLOCKS` is the number of blocks for mass storage simulation
- `#define FLASHFILENAME` is the default Flash simulation file name
- `#define FLASHAPPSECTORSIZE` sets the Flash sector size in uint32s
- `#define FLASHAPPSECTORS` sets the number of sectors in the app region of Flash

## flash

File flash.c contains functions that typically return 0 if okay, other if error:

- `uint32_t flashmem[FLASHAPPSECTORSIZE*FLASHAPPSECTORS]` is global flash memory, real or simulated.
- `int flash_init(char *filename)` initializes the flash for reading.
- `int flash_sector(uint32_t *m, int sector)` copies `m` to a Flash sector of `FLASHAPPSECTORSIZE`.
- `int flash_rndkey(void)` fills in a random key at the end of the RoT sector.

### Desktop implementation

In a console app, flash is simulated by a binary file.
The default filename is `FLASHFILENAME`, but may be changed by command line option `-f`.
`flash.c` get the flash sector size and page programming size from `options.h`.
`flash_init` check for the file's existence. If it does not exist, create a blank file.
Initialize `flashmem` with the file contents and close the file.
`flash_sector` open the file, write a segment of `flashmem` to it, and close the file.
`flash_rndkey` do nothing.

### MCU implementation

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

## blocks

The mass storage size is read from block 0. A blank file would contain mostly ASCII spaces.
Mass storage can be write-protected via its header field in block 0. 
It's like the write-protect switch on a 3.5" floppy, but more like a slider.
A blank "Block 0" starts with the following minimum text:
```
LITEFORTHBLK    1   1000       100       100
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

