# Forth Dictionary & VM Reference Manual

Below is the complete reference table for the Forth words defined in `forth_heads[]` and system constants defined in `constant_table[]` within `forth.c`.

---

## 1. Dictionary Words (`forth_heads[]`)

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| `!` | `( x addr -- )` | Stores `x` into memory address `addr`. |
| `!a` | `( x -- )` | Stores value to memory address in register `A`. |
| `!a+` | `( x -- )` | Stores value to memory address in `A`, then increments `A`. |
| `!b` | `( x -- )` | Stores value to memory address in register `B`. |
| `!b+` | `( x -- )` | Stores value to memory address in `B`, then increments `B`. |
| `(` | `( -- )` | Skips text comment until closing bracket `)` (immediate). |
| `)` | `( -- )` | Closing parenthesis delimiter for comments. |
| `*` | `( n1 n2 -- prod )` | Multiplies two numbers. |
| `*/mod` | `( n1 n2 n3 -- rem quot )` | Scales `n1` by `n2` then divides by `n3` with 64-bit intermediate product. |
| `+` | `( n1 n2 -- sum )` | Adds top two stack items. |
| `+*` | `( -- )` | Core multiply step micro-op primitive. |
| `,` | `( n -- )` | Compiles single 32-bit cell value into current data space (`here`). |
| `,inst` | `( inst -- )` | Compiles raw 16-bit VM instruction word directly. |
| `-` | `( n1 n2 -- diff )` | Subtracts `n2` from `n1`. |
| `->` | `( ? -- )` | Separates expression execution from expected results in test cases. |
| `.` | `( n -- )` | Pops and displays signed integer in current base followed by space. |
| `.(` | `( -- )` | Parses and immediately displays text until `)`. |
| `.page` | `( n -- )` | Displays memory page `n` contents. |
| `.pages` | `( -- )` | Displays listing and limits of memory pages. |
| `.s` | `( -- )` | Non-destructively displays current stack contents. |
| `.wid` | `( wid -- )` | Displays string name of the wordlist identifier. |
| `/` | `( n1 n2 -- quot )` | Divides `n1` by `n2`. |
| `2*` | `( n1 -- n2 )` | Arithmetic left shift by 1 (multiplies by 2). |
| `2/` | `( n1 -- n2 )` | Arithmetic right shift by 1 (divides by 2). |
| `2/c` | `( n1 -- n2 )` | Right shift by 1 incorporating carry bit. |
| `2dup` | `( x1 x2 -- x1 x2 x1 x2 )` | Duplicates top pair of stack items. |
| `:` | `( <name> -- )` | Begins compilation of a new colon dictionary word. |
| `;` | `( -- )` | Ends compilation of current colon word (immediate-only). |
| `<` | `( n1 n2 -- flag )` | Returns true if `n1` is less than `n2`. |
| `=` | `( x1 x2 -- flag )` | Returns true if `x1` equals `x2`. |
| `>` | `( n1 n2 -- flag )` | Returns true if `n1` is greater than `n2`. |
| `>body` | `( xt -- addr )` | Converts word execution token to its parameter field address. |
| `>options` | `( n -- )` | Sets VM system behavior flags. |
| `>r` | `( x -- ) (R: -- x)` | Pushes a value onto the return stack. |
| `@` | `( addr -- x )` | Fetches `x` from memory address `addr`. |
| `@a` | `( -- x )` | Fetches value from memory address stored in register `A`. |
| `@a+` | `( -- x )` | Fetches value from memory address in `A`, then increments `A`. |
| `@as` | `( -- x )` | Sign-extending fetch from memory pointed to by `A`. |
| `@b` | `( -- x )` | Fetches value from memory address stored in register `B`. |
| `@b+` | `( -- x )` | Fetches value from memory address in `B`, then increments `B`. |
| `[` | `( -- )` | Enters interpretation state (`state = 1`, immediate). |
| `]` | `( -- )` | Enters compilation state (`state = 0`). |
| `]shl` | `( u1 -- u2 )` | Bitwise left-shifts value by preset bit shift count. |
| `]shr` | `( u1 -- u2 )` | Bitwise right-shifts value by preset bit shift count. |
| `]task` | `( tstate -- )` | Restores task context stack pointers. |
| `a` | `( -- x )` | Copies register `A` contents to stack. |
| `a!` | `( addr -- )` | Stores top-of-stack into internal register `A`. |
| `and` | `( x1 x2 -- x3 )` | Bitwise AND. |
| `b` | `( -- x )` | Copies register `B` contents to stack. |
| `b!` | `( addr -- )` | Stores top-of-stack into register `B`. |
| `bit` | `( n -- )` | Configures bit width for subsequent compiled memory slices. |
| `bits` | `( n <name> -- )` | Defines a bit-field structure field word of width `n`. |
| `block` | `( u -- addr )` | Maps disk block `u` to memory buffer address. |
| `buffer` | `( u -- addr )` | Allocates block buffer for block `u` without reading content. |
| `cells` | `( n1 -- n2 )` | Converts cell count to address offset (no-op on cell-addressed systems). |
| `constant` | `( n <name> -- )` | Defines named constant word returning integer value `n`. |
| `cr` | `( -- )` | Outputs a line break (CR/LF) to terminal. |
| `create` | `( <name> -- )` | Creates a dictionary header that returns its data address when executed. |
| `cy` | `( -- flag )` | Places current VM carry flag state on stack. |
| `dasm` | `( addr length -- )` | Disassembles code memory starting at `addr`. |
| `decimal` | `( -- )` | Sets numerical conversion base to Decimal (10). |
| `does>` | `( -- )` | Defines run-time execution behavior for `create` words. |
| `drop` | `( x -- )` | Removes top item from the stack. |
| `dump` | `( addr length -- )` | Formats and prints memory dump from `addr` for `length` cells. |
| `dumpi` | `( inst -- )` | Disassembles single instruction payload to console. |
| `dup` | `( x -- x x )` | Duplicates the top stack item. |
| `emit` | `( c -- )` | Transmits single character to terminal output. |
| `empty-buffers` | `( -- )` | Unmarks all block buffers without saving modifications. |
| `exit` | `( -- )` | Compiles return micro-op to exit current word execution. |
| `flash-close` | `( -- )` | Flushes modified RAM buffer to persistent flash storage. |
| `flash-open` | `( addr -- )` | Maps flash memory page to RAM cache for editing/writing. |
| `flush` | `( -- )` | Saves modified block buffers and invalidates current cache. |
| `here` | `( -- addr )` | Returns current dictionary allocation pointer. |
| `hex` | `( -- )` | Sets numerical conversion base to Hexadecimal (16). |
| `immediate` | `( -- )` | Marks most recently defined word header as immediate. |
| `inv` | `( x1 -- x2 )` | Alias for `invert`. |
| `invert` | `( x1 -- x2 )` | Bitwise bit inversion / NOT. |
| `key` | `( -- c )` | Waits for and reads next character input from terminal. |
| `key?` | `( -- flag )` | Returns true (`-1`) if input character is available, otherwise false (`0`). |
| `load` | `( u -- )` | Interprets block `u` as input text stream. |
| `m*` | `( n1 n2 -- d )` | Signed 32-bit $\times$ 32-bit to 64-bit double-cell multiplication. |
| `mu/mod` | `( ud u -- rem dquot )` | Double-precision unsigned division with remainder. |
| `nip` | `( x1 x2 -- x2 )` | Drops second item on the stack. |
| `options>` | `( -- n )` | Reads VM system behavior flags. |
| `over` | `( x1 x2 -- x1 x2 x1 )` | Copies the second item on the stack to the top. |
| `p'` | `( <name> -- w aux )` | Parses word and returns execution token `w` and auxiliary metadata. |
| `page` | `( page -- a )` | Calculates base address for a given memory page index. |
| `r>` | `( -- x ) (R: x -- )` | Pops a value from return stack to data stack. |
| `r@` | `( -- x ) (R: x -- x)` | Copies top of return stack onto data stack without popping. |
| `s@` | `( addr -- x )` | Sign-extending fetch from memory address `addr`. |
| `save-buffers` | `( -- )` | Writes modified block buffers back to non-volatile storage. |
| `shft[` | `( position -- )` | Sets the VM internal shift-amount register. |
| `slice+` | `( a1 -- a2 )` | Calculates next cell or bitfield boundary address (`vmFieldPlus`). |
| `swap` | `( x1 x2 -- x2 x1 )` | Swaps top two stack items. |
| `t{` | `( -- )` | Marks start of unit test assertion expression. |
| `task[` | `( -- tstate )` | Pushes current task context stack pointers onto stack. |
| `tuck` | `( x1 x2 -- x2 x1 x2 )` | Inserts copy of top stack item under second item. |
| `um*` | `( u1 u2 -- ud )` | Unsigned 32-bit $\times$ 32-bit to 64-bit double-cell multiplication. |
| `unext` | `( -- )` | Decrements loop counter and jumps if non-zero. |
| `update` | `( -- )` | Marks current active block buffer as dirty/modified. |
| `wordlist` | `( -- wid )` | Creates a new named or anonymous wordlist context. |
| `xor` | `( x1 x2 -- x3 )` | Bitwise exclusive OR. |
| `yeet` | `( ior -- )` | Triggers error/exception abort with provided error code. |
| `}t` | `( ? -- )` | Concludes test assertion evaluation (Hayes test suite). |

---

## 2. System Constants & Bitmasks (`constant_table[]`)

| Name | Description / Usage |
| :--- | :--- |
| `_0bran` | Instruction bit pattern mask for zero-branch (`?branch`). |
| `_api0` | Opcode mask dispatching core C API 0 system calls. |
| `_api1` | Opcode mask dispatching secondary C API 1 calls. |
| `_bran` | Instruction bit pattern mask for unconditional relative jump. |
| `_call` | Instruction bit pattern mask for subprogram calls. |
| `_jump` | Instruction bit pattern mask for unconditional jump. |
| `_lit` | Instruction bit pattern mask for loading literals. |
| `_next` | Instruction bit pattern mask for loop decrement branch. |
| `_pbran` | Instruction bit pattern mask for positive-branch. |
| `_pfx` | Instruction bit pattern mask for literal prefix extension. |
| `_qlit` | Instruction bit pattern mask for quick user-offset literal loading. |
| `_rcall` | Instruction bit pattern mask for return-stack branch calls. |
| `_user` | Instruction bit pattern mask for user space offset addressing. |
| `a_constant` | Header flag identifying definition as constant value. |
| `a_immediate` | Header flag setting word to execute immediately in compile mode. |
| `a_smudged` | Header flag hiding incomplete word definition during compilation. |
| `base` | Pointer to radix register for numeric input/output conversion ($2 \le \text{base} \le 36$). |
| `blk` | Active block index variable address (`0` when reading TIB). |
| `context` | Base address of search-order wordlist context array. |
| `current` | Memory address holding target wordlist ID for new definitions. |
| `dp[]` | Base address for dictionary pointer tracking array. |
| `dp^` | Active memory space selector address (Data, Code, Header). |
| `dpl` | Decimal point location tracking register address. |
| `false` | Boolean false flag. |
| `log2pages` | Memory page count exponent shift value ($\log_2$). |
| `ram-base` | Base RAM memory boundary pointer. |
| `state` | Pointer to bitfield holding interpretation (`1`) vs compilation (`0`) state. |
| `tib` | Base memory address of Terminal Input Buffer. |
| `true` | Boolean true flag. |
| `w_macro` | Executable flag marking multi-slot micro-op macro word. |
| `w_primitive` | Executable flag marking primitive VM micro-op definition. |
| `>in` | Parse pointer offset inside active Terminal Input Buffer (TIB). |
| `\|block\|` | Capacity size of single block buffer measured in 32-bit cells. |
| `\|context\|` | Maximum capacity limit for wordlist search-order list. |