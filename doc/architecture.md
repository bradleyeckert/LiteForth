# LiteForth Architecture

The QUIT loop of LiteForth is based on C. 
Execution tokens (*xt*) may refer to either C functions or Forth definitions.
They are negative for C functions, positive for Forth definitions.

There are three regions of memory in the boot chain:

| Region  |  Write access |
|:--------|:--------------|
| Root-of-Trust (RoT) | None |
| Application Flash   | RoT  |
| Application RAM     | RoT, Application |

The QUIT loop lives in the RoT. The RoT is immutable.
It contains the minimum set of C functions to run Forth and check the digital
signature of the application.
The RoT checks the signature of the application at each bootup.

The application lives in the application space as tokenized code.
An application can be compiled to flash or RAM.
A RAM application will go away upon reboot.

## Administration

LiteForth has 3 access levels: 0, 1, or 2.
The `config.h` file's SECURITY setting determines the default setting.

- 0: Lets you re-flash the system.
- 1: Allows compilation to RAM only.
- 2: Supports authentication-based level change.

A secure system boots up in access level 2.
If you don't want to mess with security, leave SECURITY at 0. 
Challenge-Response Authentication would be used to change levels:

- Enter the security level you want, e.g. `0 challenge`.
- LiteForth sends a random challenge. Copy the challenge to your clipboard.
- Paste the challenge into a management server you've logged into.
- If you have authorization, the server sends a response. Copy it.
- Paste the response, e.g. `response: e68dcf707c9bfd1765a2db4aad7476c0`.
- If the response was good, the level gets changed.

The security level is volatile. Only compiling to Flash will permanently change it.

## Design for Flash

LiteForth compiles to Flash. Modern Flash architectures make it complicated.
You can't just flip 1s to 0s because the ECC bits can't change from 0 to 1 without
erasing the sector. The Flash rules are:

- A flash word line is ECC-protected. Program once per erase cycle.
- Writing all ones to a flash word line does not count as a write.
- Page program may write multiple word lines in parallel for speed.

| MCU       | ECC block | Page size | Sector size |
|:----------|:----------|:----------|:------------|
| STM32H743 | 256-bit   | 32 bytes  | 128K bytes  |
| CH32H417  | 64-bit    | 256 bytes | 4K bytes    |

To support large sectors, the dictionary is compiled inside a 128 KB RAM buffer
rather than physical Flash. The 128 KB sector is flashed at the end,
which typically takes 1.0s on a STM32H743 but could reach 2 to 4 seconds in rare cases.

The 128 KB buffer is available for use by the application when not compiling to flash.
Translation between LiteForth addresses and physical addresses uses a table in RAM.
Changing this table transparently switches between the buffer and the target sector.

The RoT is minimal by design, since it is immutable.
You should never have to change it.
The rest of the flash in that 128 KB sector could be filled by a C API.
If some of those functions turn out to be bad, the application can put good versions
in the application's native-code API and not use the old ones.

## Mass storage

MicroSD cards are the ubiquitous mass storage for embedded devices.
For a given capacity, Flash Translation Layer (FTL) control and erase sector size
vary quite a bit. 
Cheap cards have very large sectors that could take 250 ms to erase.
High Endurance cards have better wear leveling.

SD cards want to be written in much bigger chunks than 512 byte.
Each write smaller than the sector is a RMW, which causes Write Amplification.
A block size of 4096 bytes is a good tradeoff between Write Amplification and
working buffer size.

Blocks are the default mass storage used by LiteForth to minimize RAM.
They cause much less wear on Flash-based drives than text files.

Editing text files requires a RAM buffer that fits the whole file.
An MCU does not necessarily have that kind of RAM available.
In addition, verbose error messages that list the source requires a buffer for
the file nesting of `include`.

Editing a screen only requires a 4K buffer. Deviating from the 1994 ANS standard,
Forth Blocks (and screens) are 4KB, represented as 128 x 32 chars.
If you LIST a screen, you get a 32-line screen dump up to 131 characters wide.

## Dual dictionaries

Independent dictionaries are built in Flash memory and RAM.
The RAM dictionary disappears after a hard reset (usually triggered by power loss).
The Flash dictionary is persistent.
There are 3 different compilation styles:

| Style | Headers | Code | Usage |
|:-------------|:------|:------|:------|
| `permanent`  | Flash | Flash | Compiling application words into Flash |
|              | Flash | RAM   | Unused |
| `temporary`  | RAM   | RAM   | Compiling temporary words into RAM |
| `headerless` | RAM   | Flash | Compiling headless words into Flash |

`finalize` flushes any buffers not yet written to Flash or other storage.
It also saves dictionary pointers associated with `permanent`. 

Style transitions:

- permanent to temporary: Direct heads and code to RAM
- permanent to headerless: Save RAM header pointer, direct heads to RAM
- temporary to headerless: Save RAM header pointer
- temporary to permanent: Direct heads and code to Flash
- headerless to permanent: Restore RAM header pointer, direct heads to Flash
- headerless to temporary: Restore RAM header pointer

The compiler will throw an error if a permanent definition compiles temporary code.

Memory spaces are statically allocated arrays in C.
Flash spaces are 4KB-aligned in the linker configuration file.
The static const headers compile directly to C's `.text` or `.rodata` section.

The sandbox bounds-checks memory addresses and translates them to various regions:

| Memory space | Width | Address Map | Pointer | Section | #define |
|:-------------|:-------|:----------------|:----|:------------|:----|
| RAM Data     | 32-bit | 000000 - 0FFFFF | dpr | .bss | DATA_CELLS |
| Flash Data   | 32-bit | 100000 - 1FFFFF | dpf | .flash | FDATA_CELLS |
| Peripherals  | 32-bit | 200000 - 2FFFFF |     | | |
| RAM Code     | 16-bit | 300000 - 37FFFF | cpr | .bss | CODE_CELLS |
| Flash Code   | 16-bit | 380000 - 3FFFFF | cpf | .flash | FCODE_CELLS |
| Flash Headers| 32-bit | Access via C API | hpf | .flash | FHEAD_CELLS |
| RAM Headers  | 32-bit | Access via C API | hpr | .bss | HEAD_CELLS |

Note that .flash must be defined in the `.ld` linker file to be 4KB-aligned.
Its size in bytes should be at least (FDATA_CELLS\*4 + FCODE_CELLS\*2 + FHEAD_CELLS\*4).
Also, FCODE_CELLS must be a multiple of 2048, and FDATA_CELLS and FHEAD_CELLS
must be a multiple of 1024 to align the arrays on Flash sector boundaries.  

The address space is designed for ease of decoding.
The maximum size of most regions is 1M cells. IRL, they will be smaller.
Addresses above 003FFFFF are not allowed in LiteForth, but the C API may use them.

RAM and peripherals are placed in a 22-bit address space to work with `@` and `!`.
Peripheral physical cell address ranges for a CH32H417 are:

- 10000000 to 10003FFF = APB1 peripherals
- 10004000 to 10007FFF = APB2 peripherals
- 10008000 to 1000FFFF = AHB / High-Speed Subsystems
- 14000000 to 1400FFFF = Other AHB

Address translation uses a table whose index is taken from addr\[21:19].
Another table holds the upper address limit.

## How find works

Headers exist as static const structs in Flash. Links are C pointers.
`find` traverses a singly linked list and returns a pass/fail flag.
It squirrels away the value field of the previous header for use by `see`.
Forth access to the various header fields relies on C API calls due to
their being inaccessible by `@` and `!`.

In Forth, if a word is not found in the search order and it is not a number,
it is an error. LiteForth searches an EQU list before throwing an error.
In an embedded system, there are many constants covering peripherals and their
bit fields. Perhaps thousands. Rather than take up room in the dictionary,
these constants are kept in the first few mass storage blocks.

The QUIT loop, C functions, and the execution table used by the C functions are
located in an immutable "Root-of-Trust" region called the sandbox.
Memory regions and the C functions that access them are in the sandbox.
For purposes of the linker file, this is the `sandbox` region.
There are no C functions in the application region. 

The sandbox main app runs the Forth app by calling the `VMsteps` function
in a macroloop.

### Headers

Headers are created by a C function. When invoked by Forth, a buffer holds the
parameters used to populate the header.
With 32-bit pointers, the header is 24 bytes.
With 64-bit pointers, it is 32 bytes.
Either way, the compiler will not add padding to align the elements.

| BYTES | USAGE |
|:----|:---------|
| 4/8 | size_t link to previous header |
| 4/8 | size_t pointer to name string |
| 4 | value (number, CFA, text pointer, etc.) |
| 3 | xt, negative for C fn, positive for Forth |
| 1 | flags |
| 4 | documentation index |

A dedicated 256-byte RAM buffer is used when adding headers to Flash at run time.

static const headers defined at compile time use #define for semantic sugar.

The order assumes little-endian, so `flags` is the upper byte of a uint32_t.
The flags are:

- 7: smudge
- 6: immediate
- 5: call-only
- 4,3: type {word, no compile, no immediate, equ}
- 2: documentation index included
- 1: where-used list included
- 0: reserved

## Boot sequence

LiteForth boots into immutable root of trust (RoT) code.
The RoT performs a self-test at startup to verify it has been properly provisioned,
JTAG is disabled and the RoT is write-protected.
The RoT checks the application image against its HMAC signature before launching it.
If the HMAC does not match, it does not launch. Instead, it enters a QUIT loop.
An MCU pin should be assigned to override the launch
so that an app cannot brick the device.
LiteForth generates a HMAC upon application installation using a hardware-locked token
in the form of a random key generated the first time a freshly-flashed RoT boots.

If a USB CDC uart is used, nothing is output at startup.
Fatal errors will wait for the USB to enumerate, then send messages.
If the application does not launch, QUIT waits for the USB to enumerate
and then displays its startup boilerplate.

The application image also contains initialization values for LiteForth,
giving QUIT access to whatever lexicon has been added to the dictionary.
If the application does not boot, they keep their default values
so only the RoT lexicon is visible.

To extend LiteForth without changing the RoT, you would:

1. Include a file that compiles new definitions into Flash to build a new QUIT loop.
1. Test the new QUIT to verify that it will function.
1. `finalize` the new system to save the initialization values and generate the HMAC.

The original QUIT may be inaccessible after this extension,
but the words it is built from are part of the C API.
The RoT memory region contains an execution table with them.

After the C code starts up, it enters a macroloop that calls API function 0
in the RoT to step the token interpreter.
If there is no C app running, the token interpreter is stopped until the RoT's QUIT
executes a word. Execution continues until the return stack is empty.

## The QUIT loop

The Forth QUIT loop has to not block the application.
The application runs a macroloop, which at some point in the loop,
invokes `quit?` which writes a global variable `error` if an error occurs.
When an arror is flagged, execution jumps to address 1 in the VM.

`quit?` pseudocode:

- Accept the next character from the UART or other text stream. Exit if none.
- If the character was a LF, interpret the input buffer.
- If a problem occurs during interpretation, reset the stack.

Interpretation of the input buffer follows the usual Forth REPL.
The difference is that after number conversion fails, the token is compared to
a constant list in block 1.

Blocks 1 and above are treated as a constant list made up of 64-byte blocks.
The first 4 bytes of the block are the number of table entries.

## Application region Flash contents

The RoT QUIT loop needs to know where the app's C API functions are.
Data at the beginning of application flash contains that information:

- 32-byte HMAC: If it's good, the following fields apply:
- 4-byte offset to the initialization table, used to set the lexicon, idata, etc.
- 2-byte length of size_t datatype in bytes
- 2-byte number of function pointers, N, in the API execution table
- N\*size_t API execution table, up to 512 C functions
- Initialization table

## Block usage

Each mass storage block is 4 KB, not 1 KB as with Forth 79, 83, or 94.
An SD card with two MBR partitions, with a 16 GB second partition,
would hold 4M 4KB blocks. MBR (an old classic!) supports 2TB partitions
if the drive uses 512-byte sectors.

GPT partitioning supports essentially an infinite partition.
However, a 32-bit block number can only address 16 TB of block storage.

### Block 0

No user code can be stored in Block 0.
You can't `load` block 0, but you can list it.
It contains basic boilerplate.
Essential boilerplate identifies the partition type, so block 0 starts with
the following human readable strings. Strings are ASCII with no delimiter.
Blanks (leading or trailing) are ignored.
Hex strings are hexadecimal (base 16) numbers.

| Content | Index | Data type | For |
|---------:|-------|:----------|:----|
| LITEFORTHBLK | 00 | ASCII | Magic Number / Signature (Identifies it as Forth) |
| 1        | 10 | hex | Version Number of block format |
| 1000     | 14 | hex | Block size in bytes |
| 400000   | 18 | hex | Total Blocks allocated in this partition |
| 2        | 22 | hex | First block of constants list |
| 5        | 2B | hex | Length of constants list (at 128 bytes each)|
| 80       | 30 | hex | Columns per screen |
| | 3A | | reserved (blank) |

Viewed as a 64-column screen, with column numbers:
```
LITEFORTHBLK    1   1000    400000        2  3E8      80
PartitionType   Ver Size Allocated EQUblock EQUs Columns
0000000000000000111111111111111122222222222222223333333333333333
0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF
A description of this block file is here, maybe instructions too
```
Raw drive partitions can be edited with various hex editors:

- **[HxD Hex Editor]**(https://mh-nexus.de) – A popular, free, and lightweight Windows
editor with "Open disk" menu feature.
- **[WinHex]**(https://x-ways.net) – An industry-standard commercial tool for Windows,
capable of parsing MBR/GPT structures natively.
- **[Active@ Disk Editor]**(https://disk-editor.org) – A free cross-platform tool
(Windows, Linux, macOS).

### Block 1

Block 1 is loaded at startup.

### Block 2

Supposing EQUbegin is 2, this block contains a list of EQU constants.
There are 32 EQUs per block.
The EQUs are 128-byte-aligned, with the following fields:

- 32-character string with trailing blanks
- 9-character hex value with trailing blank
- 87-character documentation text

EQUs are in alphabetical order to facilitate binary search.
If there are 1000 EQUs, the first 8 checks miss the 512-byte sector buffer.
A search requires about 8 reads from the SDcard.
At 0.2 to 0.6 ms per read, that would be 1.6 to 5 ms per lookup.
