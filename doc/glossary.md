# Forth Dictionary & VM Reference Manual

The built-in Forth words (`forth_heads[]` and `only_heads[]`) and system constants (`constant_table[]`) defined in `forth.c`, followed by the words that `scripts/go.f` defines on top of them.

Each table is sorted by name in ASCII order, ignoring case.

---

## 1. Dictionary Words (`forth_heads[]` and `only_heads[]`)

Words marked † are tools that exist only when `FAT_FORTH` is enabled.
Name lookup ignores case.

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| `!` | `( x addr -- )` | Stores `x` at `addr` (a cell or a bit-field slice address). |
| `!a` | `( x -- )` | Stores `x` at the address in register `A`. |
| `!a+` | `( x -- )` | Stores `x` at the address in `A`, then advances `A` to the next cell or slice. |
| `!b` | `( x -- )` | Stores `x` at the address in register `B`. |
| `!b+` | `( x -- )` | Stores `x` at the address in `B`, then advances `B` to the next cell or slice. |
| `(` | `( -- )` | Skips a comment up to the closing `)`. Immediate. A second copy, prefixed with a UTF-8 BOM, lets a file that starts with a BOM and a comment load. |
| `*/mod` | `( n1 n2 n3 -- rem quot )` | Signed `n1 * n2 / n3` with a 64-bit intermediate product; symmetric division. |
| `+` | `( n1 n2 -- n3 )` | Adds. Sets `cy` to the carry out of bit 31. |
| `+*` | `( n1 n2 -- n1 n3 )` | Multiply step: if `A` is odd, adds `n1` to `n2`; then shifts `n3:A` right by one bit. 32 steps multiply `n1` by the original `A`. |
| `,compile` | `( xt -- )` | Compiles an execution token: inline micro-ops for a primitive, otherwise a call. The run-time half of `postpone`. |
| `,inst` | `( inst -- )` | Compiles a raw 16-bit VM instruction. |
| `-->` | `( -- )` | Stops interpreting the current block and continues with block `BLK`+1. |
| `->` | `( ? -- )` | † Separates the tested code from the expected results in `t{ ... -> ... }t`. |
| `.` | `( n -- )` | † Prints `n` in the current base, followed by a space. |
| `.(` | `( -- )` | Prints the following text up to `)`. |
| `.page` | `( n -- )` | † Prints the memory map entry of page `n`: base address, write-protect, read and execute limits, and name. |
| `.pages` | `( -- )` | † Prints the memory map entries of all pages. |
| `.s` | `( -- )` | † Prints the data stack without changing it. |
| `.wid` | `( wid -- )` | Prints the name of a wordlist (up to 11 characters), or its number if it has no name. |
| `2*` | `( n1 -- n2 )` | Shifts left by one bit (multiplies by 2). |
| `2/` | `( n1 -- n2 )` | Arithmetic shift right by one bit (divides by 2, rounding toward minus infinity). |
| `2/c` | `( n1 -- n2 )` | Rotates right through carry: `cy` goes into bit 31 and bit 0 goes into `cy`. |
| `2drop` | `( x1 x2 -- )` | Drops the top two items. |
| `2dup` | `( x1 x2 -- x1 x2 x1 x2 )` | Duplicates the top two items. |
| `:` | `( <name> -- )` | Starts compiling a new colon definition. If `name` is an unresolved `label`, it makes no new header: it resolves the label to this code instead. |
| `:noname` | `( -- xt )` | Starts compiling an anonymous colon definition and pushes its execution token. `;` ends it. |
| `;` | `( -- )` | Ends the current colon definition. Immediate. |
| `>aux` | `( aux -- )` | Toggles (XORs) the bits of `aux` in the aux field (the flags) of the most recent definition's header, as `immediate` does with its flag; see `x'`. |
| `>body` | `( xt -- addr )` | Returns the data address of a word made by `create`. |
| `>options` | `( n -- )` | Sets system option flags: a nonzero `n` ORs its bits in, 0 clears them all. Ignored once the options are locked. |
| `>r` | `( x -- ) (R: -- x)` | Moves `x` to the return stack. |
| `@` | `( addr -- x )` | Fetches from `addr` (a cell or a bit-field slice address). |
| `@a` | `( -- x )` | Fetches from the address in register `A`. |
| `@a+` | `( -- x )` | Fetches from the address in `A`, then advances `A` to the next cell or slice. |
| `@as` | `( -- x )` | Fetches from the address in `A`, sign-extending a bit field. |
| `@b` | `( -- x )` | Fetches from the address in register `B`. |
| `@b+` | `( -- x )` | Fetches from the address in `B`, then advances `B` to the next cell or slice. |
| `[` | `( -- )` | Switches to interpreting (`state` = 0). Immediate. |
| `]` | `( -- )` | Switches to compiling (`state` = 1). |
| `]shl` | `( u1 -- u2 )` | Shifts left by the count set with `shft[`. |
| `]shr` | `( u1 -- u2 )` | Shifts right (unsigned) by the count set with `shft[`. |
| `]task` | `( r:d -- r:d )` | Sets the stack pointers from T (`rp:sp`, `rp` in the upper half). T stays: it doesn't pop. A `sys` instruction. Used for task switching; see [Multitasking](multitasking.md). |
| `_,"` | `( <text"> -- addr )` | Compiles a counted string into text space and returns its (byte slice) address. |
| `a` | `( -- x )` | Pushes register `A`. |
| `a!` | `( x -- )` | Sets register `A`. |
| `and` | `( x1 x2 -- x3 )` | Bitwise AND. |
| `b` | `( -- x )` | Pushes register `B`. |
| `b!` | `( x -- )` | Sets register `B`. |
| `bit` | `( addr n -- addr' )` | Returns `addr` with its slice width set to `n` bits, moved up to the next `n`-bit slice position, or to the next cell if the slice would not fit. An `n` outside 1 to 31 means a whole cell. |
| `bits` | `( n <name> -- )` | Defines a variable `n` bits wide: `<name>` returns its slice address. |
| `block` | `( u -- addr )` | Returns the address of a buffer holding block `u`, reading it from storage if needed. |
| `break` | `( -- )` | Ends the app's turn: the VM returns to the terminal task, which runs the app again from the next instruction when the terminal is idle. An app that runs `VM_STEP_LIMIT` steps without a `break` is stopped, and QUIT reports `-116`. Typed at the terminal, it reports `-115`. |
| `buffer` | `( u -- addr )` | Assigns a buffer to block `u` without reading it. |
| `bye` | `( -- )` | Leaves QUIT (and exits `lf`). |
| `capacity` | `( -- u )` | Number of blocks in the block file. |
| `capusec` | `( -- )` | Captures the free-running microsecond counter: `Y` gets the upper 32 bits and `X` the lower 32 (read them with `y@` and `x@`). The counter's starting point is unknown, so only differences between captures are meaningful. |
| `close-flash` | `( -- )` | Programs the open flash page from its RAM buffer, write-protects it, and frees the buffer. |
| `cold` | `( -- )` | Resets the VM (registers and stacks) and starts the app: sets `SYS_OPTION_RUNNING`, so while QUIT waits for input it runs VM code from address 0 until each `break`. See `doc/flashmem.md`. |
| `constant` | `( n <name> -- )` | Defines `<name>`, which returns `n`. |
| `cr` | `( -- )` | Starts a new line on the terminal. |
| `create` | `( <name> -- )` | Defines `<name>`, which returns the address of `here` at the time it was created. |
| `cy` | `( -- flag )` | Pushes the carry flag (0 or 1). |
| `dasm` | `( addr length -- )` | † Disassembles code starting at `addr`. |
| `decimal` | `( -- )` | † Sets `base` to 10. |
| `does>` | `( -- )` | Immediate. Right after `create`, makes the created word jump to the code that follows. Used as `create x ... does> ] ... exit [`, not inside a defining word. |
| `drop` | `( x -- )` | Drops the top item. |
| `dump` | `( addr length -- )` | † Prints `length` cells of memory from `addr`. |
| `dumpi` | `( inst -- )` | † Disassembles one instruction. |
| `dup` | `( x -- x x )` | Duplicates the top item. |
| `empty` | `( -- )` | Resets the dictionary: removes user words and wordlists and resets the dictionary pointers. |
| `empty-buffers` | `( -- )` | Unassigns all block buffers without saving them. |
| `execute` | `( i*x xt -- j*x )` | Runs execution token `xt`: a primitive, macro or API word as one instruction, anything else (colon, `:noname` or `create` code) as a call. |
| `exit` | `( -- )` | Immediate. Compiles a return from the current word. |
| `flush` | `( -- )` | Saves modified block buffers, then unassigns all of them. |
| `forth` | `( -- )` | Makes the forth wordlist first in the search order. |
| `hex` | `( -- )` | † Sets `base` to 16. |
| `immediate` | `( -- )` | Makes the most recent definition immediate. |
| `inv` | `( x1 -- x2 )` | Bitwise NOT (same as `invert`). |
| `invert` | `( x1 -- x2 )` | Bitwise NOT. |
| `label` | `( <name> -- )` | Declares `name` for a forward reference: a header (flag `A_UNRESOLVED`) with two reserved code slots. Code can call `name` before it's defined; a later `: name` stores a jump to its code there. Until then, a call from code fails with -105 (the slots hold an invalid opcode), running it at the terminal gives -119, and so does `bye` while one is left (it prints the name). |
| `literal` | `( n -- )` | Immediate. Compiles `n` as a literal. |
| `load` | `( u -- )` | Interprets block `u`, then continues after `load`. Loads nest. |
| `m*` | `( n1 n2 -- d )` | Signed 32 × 32 to 64-bit multiply. |
| `mu/mod` | `( ud u -- rem dquot )` | Unsigned 64 / 32 division giving a remainder and a 64-bit quotient. |
| `nip` | `( x1 x2 -- x2 )` | Drops the second item. |
| `only` | `( -- )` | Sets the search order to just the only wordlist. |
| `open-flash` | `( addr -- )` | Maps the flash page holding `addr` to a writable RAM buffer so it can be compiled to. |
| `over` | `( x1 x2 -- x1 x2 x1 )` | Copies the second item to the top. |
| `page` | `( page -- a )` | Returns the base address of memory page `page`. |
| `postpone` | `( <name> -- )` | Immediate. Compiles an immediate word; for any other word, compiles code that will compile it. |
| `r>` | `( -- x ) (R: x -- )` | Moves the top of the return stack to the data stack. |
| `r@` | `( -- x ) (R: x -- x)` | Copies the top of the return stack. |
| `s@` | `( addr -- x )` | Fetches from `addr`, sign-extending a bit field. |
| `save-buffers` | `( -- )` | Writes modified block buffers to storage. |
| `save-wids` | `( -- )` | Compiles the wordlist record to text space for booting, cell aligned: the address just past the record, the number of wordlists in use, then that many raw `s_wid` structures (head link and 12-byte name), padded to a cell. Heads are tagged links, not C addresses, so the record is valid wherever flash is mapped. Store the record's address in cell 1, and use it last before `close-flash`. Booting with `-o 8` restores the wordlists from it. |
| `see` | `( <name> -- )` | † Disassembles a word. |
| `shft[` | `( n -- )` | Sets the shift count used by `]shl` and `]shr` (0 to 31). |
| `slice+` | `( a1 -- a2 )` | Advances an address to the next cell or bit-field slice. |
| `swap` | `( x1 x2 -- x2 x1 )` | Swaps the top two items. |
| `t_rx` | `( -- c )` | Reads a character from the terminal (`key` in `go.f`). |
| `t_rx?` | `( -- flag )` | Nonzero if a terminal character is waiting (`key?` in `go.f`). |
| `t_tx!` | `( c -- )` | Sends a character to the terminal (`emit` in `go.f`). |
| `t_tx?` | `( -- flag )` | Nonzero while the terminal output is busy (`emit?` in `go.f`). |
| `task[` | `( -- r:d )` | Pushes T onto the data stack and R onto the return stack, then T = `rp:sp` (`rp` in the upper half). Used for task switching. |
| `tuck` | `( x1 x2 -- x2 x1 x2 )` | Copies the top item under the second. |
| `t{` | `( -- )` | † Starts a test: `t{ ... -> ... }t`. |
| `u!` | `( x -- )` | Sets the user pointer register `U`. |
| `um*` | `( u1 u2 -- ud )` | Unsigned 32 × 32 to 64-bit multiply. |
| `um+` | `( u1 u2 -- u3 carry )` | Adds, and pushes the carry out of bit 31. |
| `unext` | `( -- )` | Repeats its instruction group from the start while `R` counts down, then drops `R`. Use `|inst` to start the group where the loop should begin. |
| `update` | `( -- )` | Marks the most recently used block buffer as modified. |
| `wordlist` | `( -- wid )` | Creates a new wordlist and returns its number. |
| `words` | `( -- )` | Lists the words in the first wordlist of the search order. |
| `x'` | `( <name> -- w aux )` | Returns the header fields of `<name>`: `w` (its execution token or value) and `aux` (its flags). |
| `x@` | `( -- x )` | Pushes register `X`. After an error in the app, `X` holds the PC just after the instruction that failed. |
| `xor` | `( x1 x2 -- x3 )` | Bitwise exclusive OR. |
| `y@` | `( -- x )` | Pushes register `Y`. After an error in the app, `Y` holds the error code. |
| `yeet` | `( ior -- )` | Raises error `ior`. When interpreting, QUIT reports it. In the app, the VM jumps to the yeet handler at cell 2 with the PC in `X` and `ior` in `Y` (read them with `x@` and `y@`). |
| `\|inst` | `( -- )` | Ends the current micro-op group, so the next primitive starts a new instruction. |
| `}t` | `( ? -- )` | † Ends a test and checks the results. |

---

## 2. System Constants & Bitmasks (`constant_table[]`)

| Name | Description / Usage |
| :--- | :--- |
| `>in` | Parse offset in the current input source (TIB or a block). |
| `_0bran` | Instruction bit pattern for branch-if-zero (`if`; drops T). |
| `_api0` | Opcode mask dispatching core C API 0 system calls. |
| `_api1` | Opcode mask dispatching secondary C API 1 calls. |
| `_bran` | Instruction bit pattern mask for unconditional relative jump. |
| `_call` | Instruction bit pattern mask for subprogram calls. |
| `_jump` | Instruction bit pattern mask for unconditional jump. |
| `_lit` | Instruction bit pattern mask for loading literals. |
| `_next` | Instruction bit pattern mask for loop decrement branch. |
| `_pbran` | Instruction bit pattern for branch-if-not-negative (`-if`; keeps T). |
| `_pfx` | Instruction bit pattern mask for literal prefix extension. |
| `_pfx1` | Instruction bit pattern for a prefix with bit 9 set (extends literals, calls and jumps). |
| `_qlit` | Instruction bit pattern mask for quick user-offset literal loading. |
| `_rcall` | Instruction bit pattern for a relative call. |
| `_user` | Instruction bit pattern mask for user space offset addressing. |
| `a_constant` | Header flag identifying definition as constant value. |
| `a_immediate` | Header flag setting word to execute immediately in compile mode. |
| `a_smudged` | Header flag hiding incomplete word definition during compilation. |
| `base` | Pointer to radix register for numeric input/output conversion ($2 \le \text{base} \le 36$). |
| `blk` | Active block index variable address (`0` when reading TIB). |
| `context` | Base address of search-order wordlist context array. |
| `current` | Memory address holding target wordlist ID for new definitions. |
| `dp[]` | Base address for dictionary pointer tracking array. |
| `dp^` | Address of the current memory space selector: 0 = udata, 1 = idata, 2 = code, 3 = text. |
| `dpl` | Decimal point location tracking register address. |
| `false` | Boolean false flag. |
| `log2pages` | Memory page count exponent shift value ($\log_2$). |
| `ram-base` | Base RAM memory boundary pointer. |
| `stack-masks` | `STACK_CAPACITY` - 1 in both halves, as `rp:sp`: the masks for the stack pointers. `task` uses it to check that stack windows fit. |
| `state` | Pointer to bitfield holding compilation (`1`) vs interpretation (`0`) state. |
| `TIB` | Base memory address of Terminal Input Buffer. |
| `true` | Boolean true flag. |
| `w_macro` | Executable flag marking multi-slot micro-op macro word. |
| `w_no_tail_call` | Header flag: `;` must not turn a call to this word into a jump. |
| `w_primitive` | Executable flag marking primitive VM micro-op definition. |
| `w_wide_inst` | Header flag: the primitive is a whole 16-bit instruction, not micro-ops. |
| `\|block\|` | Capacity size of single block buffer measured in 32-bit cells. |
| `\|context\|` | Maximum capacity limit for wordlist search-order list. |

---

## 3. Words defined in `scripts/go.f`

These are compiled by the boot script, not built in.

| Word | Stack Effect | Description |
| :--- | :--- | :--- |
| `$type` | `( ca -- )` | Prints a counted string. |
| `'` | `( <name> -- xt )` | The execution token of the next word (`x' drop`). |
| `'here` | `( -- a )` | Address of the current space's pointer. |
| `+!` | `( n a -- )` | Adds `n` to the cell at `a`. |
| `,jump` | `( xt addr -- )` | Compiles a jump to `xt` at code address `addr` (as `go.f` does at 0 for the app). |
| `-` | `( n1 n2 -- n3 )` | Subtracts `n2` from `n1`. |
| `-if` | `( -- a )` | Immediate. Branches past `then` if T is not negative; keeps T. |
| `."` | `( <text"> -- )` | Immediate. Compiles a string to print. |
| `.map` | `( -- )` | Prints the extent and free space of each memory space. |
| `0=` | `( x -- flag )` | True if `x` is 0. |
| `1+` | `( n -- n+1 )` | Adds 1. |
| `1-` | `( n -- n-1 )` | Subtracts 1. |
| `=` | `( n1 n2 -- flag )` | True (-1) if `n1` equals `n2`, else 0. |
| `@+` | `( a -- a+1 x )` | Fetches and advances the address. |
| `_code` | `( -- )` | Selects the code space. |
| `_idata` | `( -- )` | Selects the idata space. |
| `_text` | `( -- )` | Selects the text space. |
| `_udata` | `( -- )` | Selects the udata space. |
| `again` | `( a -- )` | Immediate. Branches back to `begin`. |
| `ahead` | `( -- a )` | Immediate. Unconditional forward branch. |
| `align` | `( -- )` | Aligns `here` to a whole cell. |
| `allot` | `( n -- )` | Reserves `n` cells in the current space. |
| `amask` | `( a -- a' )` | Strips the slice fields from an address, leaving the 22-bit cell address. |
| `asleep` | `( -- xt )` | Constant: the ACTION of a sleeping task. When `pause` jumps to it, it sets U to NEXT and returns to the code that called `pause`, without switching stacks. |
| `awake` | `( -- xt )` | Constant: the ACTION of a running task. When `pause` jumps to it, it saves the task's `rp:sp` in its user area, switches to the stacks of the next task in the ring, and returns into that task. |
| `base!` | `( n -- )` | Sets `base`. |
| `begin` | `( -- a )` | Immediate. Starts a loop. |
| `cells` | `( n -- n )` | Immediate no-op: addresses are cell addresses. |
| `chere` | `( -- addr )` | Next free code address. |
| `decimal` | `( -- )` | Sets `base` to 10. |
| `definitions` | `( -- )` | Makes the first wordlist in the search order the one new words go into. |
| `else` | `( a1 -- a2 )` | Immediate. |
| `emit` | `( c -- )` | Sends a character (`t_tx!`). |
| `emit?` | `( -- flag )` | Nonzero while output is busy (`t_tx?`). |
| `for` | `( -- a )` | Immediate. At run time `( n -- )`: starts a loop that runs `n` times (`R` = n..1). `0 for` runs 2^32 times. |
| `here` | `( -- a )` | Next free address in the current space. |
| `hex` | `( -- )` | Sets `base` to 16. |
| `if` | `( -- a )` | Immediate. Branches past `else`/`then` if T is 0; drops T. |
| `key` | `( -- c )` | Reads a character (`t_rx`). |
| `key?` | `( -- flag )` | Nonzero if a character is waiting (`t_rx?`). |
| `lshift` | `( u1 u2 -- u3 )` | Shifts `u1` left by `u2` bits. |
| `multitask` | `( -- )` | Makes a ring of one task, the terminal (`operator`): sets U, NEXT = itself, ACTION = `awake`, and R:D. |
| `negate` | `( n -- -n )` | Negates. |
| `next` | `( a -- )` | Immediate. Ends a `for` loop. |
| `operator` | `( -- a )` | Constant: the terminal task's user area (3 cells: NEXT, ACTION, R:D). |
| `or` | `( x1 x2 -- x3 )` | Bitwise OR. |
| `pause` | `( -- )` | Jumps to the running task's ACTION (`awake` or `asleep`), with NEXT and the address of its R:D cell on the stack. Needs a ring (`multitask`) first. See [Multitasking](multitasking.md). |
| `repeat` | `( a1 a2 -- )` | Immediate. Ends a `begin ... while ... repeat` loop. |
| `rshift` | `( u1 u2 -- u3 )` | Shifts `u1` right (unsigned) by `u2` bits. |
| `stop` | `( -- )` | Sets the running task's ACTION to `asleep`, then `pause`. |
| `then` | `( a -- )` | Immediate. Resolves `if`, `-if`, `else` or `ahead`. |
| `type` | `( ca n -- )` | Prints `n` characters from `ca`. |
| `until` | `( a -- )` | Immediate. Branches back to `begin` while T is 0; drops T. |
| `unused` | `( -- n )` | Free cells left in the current space. |
| `variable` | `( <name> -- )` | Defines a 32-bit variable. |
| `while` | `( a1 -- a1 a2 )` | Immediate. Leaves the loop when T is 0; drops T. |
