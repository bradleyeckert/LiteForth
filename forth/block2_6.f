( BLOCK 2 )
( LiteForth internal glossary lines 65 to 95 __________________ 10/10/26 BNE )
( -- )                          \ Exit LiteForth and free the memory pool it was using.
( -- )                          \ List the words in the first wordlist of the context.
( -- )                          \ Set the top of the search order to forth.
( -- )                          \ Set the search order to only the root.
( x1 -- x2 )                    \ Invert all bits of x1, giving its logical inverse x2.
( x -- )                        \ Reserved for Page Extension: Appends 32 address bits to the next memory access.
( x1 x2 -- x1 x2 x1 )           \ Place a copy of x1 on top of the stack.
( addr -- )                     \ Load the A register with a cell or slice address, or a 32-bit value.
( x1 x2 -- x3 )                 \ x3 is the bit-by-bit exclusive-or of x1 with x2.
( n1 n2 -- n3 )                 \ Add n2 to n1, giving the sum n3.
( x1 x2 -- x3 )                 \ x3 is the bit-by-bit logical "and" of x1 with x2.
( x -- ) ( R: -- x )            \ Move x from the data stack to the return stack.
( -- ) ( R: n -- n-1 | )        \ Decrement loop count `n` and jump to slot0 if it is nonzero, else drop n.
( x1 -- x2 )                    \ x2 is the logical left-shift of x1 (LSB is 0).
( x -- x x )                    \ Duplicate x.
( x -- )                        \ Remove x from the stack.
( -- u )                        \ Fetch a cell or slice from memory addressed by A, zero-extend it if it is a slice.
( -- u )                        \ Same as `@a`, but post-increment A to point to the next cell or slice.
( -- n )                        \ Fetch a cell or slice from memory addressed by A, sign-extend it if it is a slice.
( -- x ) ( R: x -- x )          \ Copy x from the return stack to the data stack.
( -- x ) ( R: x -- )            \ Move x from the return stack to the data stack.
( x1 -- x2 )                    \ Rotate x1 right through carry.
( n1 -- n2 )                    \ Arithmetic shift right n1.
( x -- )                        \ Store a cell or slice to memory addressed by A
( x -- )                        \ Same as `!a`, but post-increment A to point to the next cell or slice.
( x -- )                        \ Store a cell or slice to memory addressed by B
( x -- )                        \ Same as `!b`, but post-increment B to point to the next cell or slice.
( x1 x2 -- x2 x1 )              \ Exchange the top two stack items.
( x1 x2 -- x1 x3 )              \ Multiplication step: x2:A >> 1, first adding x1:0 if A is odd
( -- x )                        \ Read the B register
( addr -- )                     \ Load the B register with a cell or slice address, or a 32-bit value.
( BLOCK 3 )
( LiteForth internal glossary lines 97 to 127 _________________ 10/10/26 BNE )
( -- u )                        \ Fetch a cell or slice from memory addressed by B, zero-extend it if it is a slice.
( -- u )                        \ Same as `@b`, but post-increment B to point to the next cell or slice.
( -- x )                        \ Read the A register.
( -- x )                        \ Read the carry register.
( a-addr -- )                   \ Set the base address of user variables.
( x1 x2 -- x1 x2 x1 x2 )        \ Duplicate cell pair x1 x2.
( x1 x2 -- )                    \ Drop cell pair x1 x2 from the stack.
( x addr -- )                   \ Store cell or slice (depending on addr type) to addr.
( addr -- x )                   \ Fetch cell or unsigned slice (depending on addr type) from addr.
( addr -- n )                   \ Fetch cell or signed slice (depending on addr type) from addr.
( x1 x2 -- x2 )                 \ Drop the first item below the top of stack.
( x1 x2 -- x2 x1 x2 )           \ Copy the first (top) stack item below the second stack item.
( u1 u2 -- u3 carry )           \ Add u1 and u2, keeping the carry.
( a1 -- a2 )                    \ Skip to the next cell or slice address.
( x1 -- x2 )                    \ Logical shift x1 right by the number of places set by `shft[`.
( x1 -- x2 )                    \ Logical shift x1 left by the number of places set by `shft[`.
( -- )                          \ Ping the watchdog and break the simulation for the terminal.
( -- )                          \ Capture the 64-bit microseconds counter in register Y:X.
( n -- )                        \ Set the number of places for shift instructions, 0 to 31 allowed.
( rp:sp -- rp:sp )              \ Set new stack pointers (does not pop in this instruction).
( ior -- )                      \ If ior is nonzero, trigger a yeet interrupt which jumps to an error handler.
( x -- x rp:sp ) ( R: x -- x x ) \ Get the current stack pointers, pushing registers onto the stacks.
( -- u )                        \ Fetch the X register, which is the PC where a yeet occurred.
( -- u )                        \ Fetch the Y register, which is the error that caused the yeet.
( u1 u2 -- ud )                 \ Multiply u1 by u2, giving the unsigned double-cell product ud.
( n1 n2 -- d )                  \ d is the signed product of n1 times n2.
( ud1 u1 -- u2 ud2 )            \ Unsigned divide ud1 by u1, u2 is the remainder, ud2 is the quotient.
( n1 n2 n3 -- n4 n5 )           \ Multiply n1 by n2 producing d, then divide by n3 producing remainder n4 and quotient n5.
( -- flag )                     \ True if a byte is in the receive buffer.
( -- c )                        \ Get the next byte from the terminal, block if there isn't one.
( c -- )                        \ Transmit one byte to the terminal.
( BLOCK 4 )
( LiteForth internal glossary lines 129 to 159 ________________ 10/10/26 BNE )
( -- flag )                     \ True if the transmitter is busy (not ready to send).
( C: "<spaces>name" -- colon-sys ) \ Create a new colon definition for `name` and set STATE=1.
( C: colon-sys -- )             \ End a colon definition by compiling an exit or tail-recursing; set STATE=0.
( flag -- )                     \ Set system options: XOR flag with current options, or clear options if flag=0.
( -- )                          \ Empty the dictionary
( "ccc<paren>"-- )              \ Ignore input until `)` is encountered or input runs out.
( "ccc<paren>"-- )              \ Ignore input until `)` is encountered or input runs out.
( "ccc<paren>"-- )              \ Echo input to terminal until `)` is encountered or input runs out.
( "<spaces>name" -- )           \ Create a name that returns the selected data pointer's value when name was created.
( -- )                          \ Cause subsequent output to appear at the beginning of the next line.
( x -- )                        \ Compile literal x as code
( wid -- )                      \ Display the name of a wid (used by `order`)
( "<spaces>name" -- xt aux )    \ Find name and return its xt and aux value.
( x -- )                        \ XOR x with the aux value of the most recent definition.
( "<spaces>name" -- char )      \ Parse name delimited by a space. Put the value of its first character onto the stack.
( page -- addr )                \ Return the base address of a memory page.
( -- wid )                      \ Create a new empty word list, returning its word list identifier wid.
( page -- )                     \ Render a flash memory page writable by replacing it with a RAM buffer.
( -- )                          \ Close the open flash page by re-flashing the page from the RAM buffer.
( -- )                          \ Enter compilation state.
( -- )                          \ Enter interpretation state. [ is an immediate word.
( -- )                          \ Compile an exit or tail-recurse.
( x "<spaces>name" -- )         \ Parse name, create a definition for name that places x on the stack.
( x "<spaces>name" -- )         \ Parse name, create a definition for name that returns a slice variable of x bits.
( "ccc<quote>" -- )             \ Compile ccc, with some `\` sequences allowed, to text space with a leading count.
( a1 -- a2 )                    \ Set a new slice size for a1 and align its position to the next valid slice.
( inst16 -- )                   \ Compile a 16-bit instruction to code space.
( -- )                          \ Make the most recent definition an immediate word.
( u -- a-addr )                 \ a-addr is the base address of the block buffer assigned to mass-storage block u.
( u -- a-addr )                 \ Assign a buffer for block u without reading the block.
( -- )                          \ Mark the current block buffer as modified.
( BLOCK 5 )
( LiteForth internal glossary lines 161 to 191 ________________ 10/10/26 BNE )
( -- )                          \ Transfer the contents of each UPDATEd block buffer to mass storage.
( -- )                          \ Perform the function of SAVE-BUFFERS, then unassign all block buffers.
( -- )                          \ Unassign all block buffers. Do not save the contents of any UPDATEd block buffers.
( i*x u -- j*x )                \ Save the current input-source, assign block u as input, set >IN to zero, and interpret.
( -- u )                        \ The number of 4 KB blocks available in the mass storage system.
( i*x -- j*x )                  \ Stop loading the current block and switch to the next block.
( "<spaces>name" -- )           \ Find name. If it immediate, compile it. Otherwise, compile its xt and `,compile`.
( xt -- )                       \ COMPILE, is the compilation equivalent of EXECUTE. Compiles a call to xt.
( -- )                          \ Flushes pending opcodes to memory to start a new instruction group.
( -- )                          \ Launch the Forth app starting from address 0.
( C: -- colon-sys) ( S: -- xt ) \ Create an execution token xt, enter compilation state and start the current definition.
( -- )                          \ Compile the system wids as a data structure to be restored by boot code.
( "<spaces>name" -- )           \ Create a forward reference for a matching colon definition.
( expected -- )                 \ End an assertion test, yeeting if the expected stack does not match the actual.
( actual -- )                   \ Save the test results for comparison.
( -- )                          \ Begin an assertion test.
( -- )                          \ Set contents of BASE to sixteen (16).
( -- )                          \ Set contents of BASE to ten (10).
( n -- )                        \ Display status of page n.
( -- )                          \ Display status of all pages.
( a-addr u -- )                 \ Display a hex dump of u cells of memory.
( inst16 -- )                   \ Disassemble an instruction.
( xt u -- )                     \ Disassemble u instructions.
( -- )                          \ Copy and display the values currently on the data stack, usually for debugging.
( n -- )                        \ Display n in free field format, except if BASE is hex, display slice addresses.
( "<spaces>name" -- )           \ Disassemble name.
( -- )                          \ Make the headers that follow temporary, until `-heads` drops them.
( -- )                          \ Make the headers that follow permanent; temporary words stay usable.
( -- )                          \ Drop the temporary headers, ending the scope of their words.
( n -- )                        \ Receive files by YMODEM: source by its ( BLOCK ) tags if n=0, else binary from block n.
( w h ctrl palette -- )         \ Open a w x h pixel LCD panel (ctrl 0: ST7789); palette: the cell address of 16 RGB565 colors.
( BLOCK 6 )
( LiteForth internal glossary lines 193 to 223 ________________ 10/13/26 BNE )
( u -- )                        \ Drive the LCD bus: bit 15 reset, 14 CSX, 13 command, 7:0 the byte (lcd_tx).
( n1 -- n2 )                    \ Shift n1 right, copying the sign bit, by the count set with shft[.
( u16 -- )                      \ Expand a font pixel code into palette colors and send them to the LCD as data.
