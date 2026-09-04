# LiteForth Architecture

The QUIT loop of LiteForth is based on C. It uses the dual-xt scheme of Gforth.
Execution tokens may refer to either C functions or Forth definitions.
Execution tokens are negative for C functions, positive for definitions.
The `find` function returns a name token that points to a 3-cell `w, xte, xtc` structure.
Depending on whether IMMEDIATE is 0 or not, each blank-delimited token fed to `find`
results in either `xte` or `xtc` being executed.

There are three regions of Flash the boot chain:

| Region | Code type | Write access |
| ---------------- | --------- | ------------ |
| Root-of-Trust (RoT) | Native | None | 
| Sandbox             | Native | RoT, Sandbox |
| Application         | Forth  | RoT, Sandbox, Application |

The QUIT loop lives in the RoT. The RoT is immutable.
It contains the minimum set of C functions to run Forth and check the digital
signature of the sandbox and application.
Security depends on access to the QUIT loop. It should be password-locked.
The RoT checks the signature of the application before running it.

The sandbox contains C functions such as middleware and other features that may
need to be updated, for security patching, bug fixes, etc.
Firmware updaters would live in the sandbox.

The application lives in the application space as tokenized code running in the sandbox.
An application can be compiled to flash or RAM.
A RAM application will go away upon reboot.

## Design for Flash

LiteForth compiles to Flash. The notion of compiling code directly to Flash
is attractive, but modern Flash architectures make it complicated.
You can't just flip 1s to 0s because the ECC bits can't change from 0 to 1 without
erasing the sector. The Flash rules (inspired by the CH32H417) are:

- A flash word line is 64-bit, ECC-protected. Program once per erase cycle.
- Writing all ones to a flash word line does not count as a write.
- A 256-byte page write flashes 32 word lines in parallel. Use when possible.
- Erase sectors are 4K bytes (16 pages).

Compilation to Flash is more like streaming to Flash.
The stream goes into a 256-byte buffer that flushes into a page write,
prefixed by a sector erase when beginning a new sector.
If an 8-byte-aligned 8-bit value is left blank, it can be programmed later.
To work within these constraints,

- `align` advances the dictionary pointer to the next 8-byte-aligned address.
- Strings and `c,` pack bytes, so the dictionary pointer addresses bytes.
- Non-volatile store, `n2! ( d a -- )` writes needs the 64-bit data
at `a` to be -1 (`a` is 8-byte-aligned).
- Definitions are 8-byte-aligned.
- `:` compiles into a RAM buffer, which is flushed by `;`.
- Long forward references (`later foo`) compile an aligned 64-bit -1.

## Fat vs Thin

Fat Forths add content to the dictionary whose only purpose is to help the programmer.
This extra data includes source code links, stack pictures, etc.
Given that there is support for files, the extra dictionary is stored in mass storage.
To make the data usable across minor revisions, each header entry gets a 128-byte block.
1000 headers would need 128 KB , fairly small for an SD card.

## Mass storage

MicroSD cards are the ubiquitous mass storage for embedded devices.
For a given capacity, Flash Translation Layer (FTL) control and erase sector size
vary quite a bit. Cheap cards have very large sectors that could take 250 ms to erase.
High Endurance cards have better wear leveling.

SD cards want to be written in much bigger chunks than 512 byte.
Each write smaller than the sector is a RMW, which causes Write Amplification.
A block size of 4096 bytes is a good tradeoff between Write Amplification and
working buffer size.

Blocks are the default mass storage used by LiteForth to minimize RAM.

Editing text files requires a RAM buffer that fits the whole file.
An MCU does not necessarily have that kind of RAM available.
In addition, verbose error messages that list the source requires a buffer for
the file nesting of `include`.

Editing a screen only requires a 4K buffer. Deviating from the 1994 ANS standard,
Forth Blocks (and screens) are 4KB, represented as 128 x 32 chars.
If you LIST a screen, you get a 32-line screen dump up to 131 characters wide.

## How find works

Headers exist as static const structs in Flash. Static lists are not extensible.
`find` traverses a singly linked list. It squirrels away the value field of the previous
header for use by `see`.
`find` returns a pass/fail flag. Once found,
Forth access to the various fields relies on C API calls due to their being outside
the sandbox.

At bootup, only the RoT list is findable. 
If a valid HMAC is found, the other lists are added.

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

|----|---------|
| BYTES | USAGE |
| 4/8 | size_t link to previous header
| 4/8 | size_t pointer to name string
| 4 | value (number, CFA, text pointer, etc.)
| 3 | xte (execution xt) xt of execution semantics, negative for C fn
| 1 | flags (immediate, call-only, smudge)
| 3 | xtc (compilation xt) xt of compilation semantics, negative for C fn
| 1 | spare
| 2 | exdex index
| 2 | padding or start of name string

A dedicated 256-byte RAM buffer is used when adding a header to Flash.

static const headers defined at compile time use #define for semantic sugar.

## Boot sequence

LiteForth boots into immutable root of trust (RoT) code.
The RoT performs a self-test at startup to verify it has been properly provisioned,
JTAG is disabled and the RoT is write-protected.
The RoT checks the application image against its HMAC signature before launching it.
If the HMAC does not match, it does not launch. Instead, it enters a QUIT loop.
An MCU pin should be assigned to override the launch
so that an app cannot brick the device.
LiteForth generates the HMAC upon application installation using a hardware-locked token
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
1. `gild` the new system to save the initialization values and generate the HMAC.

The original QUIT is inaccessible after this extension, but the words it is built from
are part of the C API. The RoT memory region contains an execution table with them.

The application region of flash includes an execution table for the RoT QUIT loop
to access the application's C API. The start of the application section has:

- 32-byte HMAC
- 4-byte offset to the initialization table
- 4-byte offset to C code startup
- Initialization table

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

## Memory

Memory is cell-addressed. Cells are 32-bit.
If bytes or halves are needed, they are bit fields.
Data space is protected against out-of-bounds access.
These run-time checks perform the functions of an MMU, which most MCUs don't have.

Instead, the code runs in a virtual machine (VM).
"VM" has a completely different meaning than it did in the 20th century.
The meaning of VM here is a token interpreter that executes the binary of an ISA.
To make things interesting, the ISA is designed for eventual hardware implementation.

The ISA has dedicated Flash and RAM regions, each of which are either code or data spaces.
These are static arrays.
`#defines` in the config.h file determine their sizes and accessibility.
You can change them and recompile if your app is running short of space.
`unused` ( n -- m ) takes a memory region and returns the number of 32-bit cells remaining.
The source code could use `FlashCode unused .` to show remaining code space.

### Regions

Addresses are 22-bit. 22-bit literals are supported by the `ok` ISA using
a 9-bit prefix and 13-bit literal (two 16-bit instructions).

The address space is split into eight 512K-cell regions, selected by bits \[21:19].
@ and ! use indices (masked to keep the address within bounds) to address
the various C arrays. 19-bit region size leaves room for growth.
A CH32H417 could get by with 15-bit region sizes, considering data cells are 4-byte.

Fixed-size C arrays are declared for various memory regions:

0. Flash code space, used by Forth code to build permanent application words.
2. Flash data space, readable by @. Writable by , and !.
3. Flash text space, strings and other const data
4. RAM code space, used by Forth code to build temporary words.
6. RAM data space, read/write.
7. I/O data space, read/write.

