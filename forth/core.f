( BLOCK IGNORE )

( From the ./bin folder, you can './lf.exe -o 3 < ../forth/core.f` )
( or from Windows PowerShell, `cmd /c "lf.exe -o 3 < ..\forth\core.f"`, )
( which will load this file into a blank `lf`. A PowerShell pipe {Get-Content | lf.exe} turns non-ASCII )
( text such as CJK into ? unless $OutputEncoding is UTF-8 {PowerShell 7, or set by hand}. )

( The `./scripts/putblocks.py` script will insert this file into a blocks file. )
( This keeps source files on GitHub as text files rather than blocks, which GitHub cannot diff. )
( Since this source file is dual-use (loadable from the shell or used as pseudo-blocks, )
( other block sources are needed to completely define the blocks. )

( Block 0 is reserved for the LITEFORTH tag and to-be-determined metadata. )
( Block 1 is the home block )
( Blocks 2 to 6 are used to document the internal functions of `forth.c`. )
( Block 7 to ~22 build up a Forth )

0 open-flash

( BLOCK 7 )
( LiteForth configuration _____________________________________ 10/9/26 BNE )
empty only forth
: \         ( -- )          >in @ 128 + -128 and >in ! ; immediate              \ comment to next row in a 128*32 block
: base!     ( n -- )        base ! ;                                            \ `base` is a 3-instruction literal
: decimal   ( -- )          10 base! ;                                          \ set base to 10
: hex       ( -- )          16 base! ;                                          \ set base to 16
: aligned   ( ca -- a )     32 bit ;                                            \ align address to cells
32 constant bl                                                                  \ blank
-->
( BLOCK++ )
( Basic compilation ___________________________________________ 10/14/26 BNE )
hex
: variable  ( -- )          20 bits ;                                           \ a Forth variable is a 32-bit slice
: _section  ( n -- )        dp^ ! ;                                             \ dp^ is a 2-bit value (see forth.h)
: _udata    ( -- )          0 _section ;                                        \ use uninitialized data section
: _idata    ( -- )          1 _section ;                                        \ use initialized data section, persistent
: _code     ( -- )          2 _section ;                                        \ use code section, like text but in low RAM
: _text     ( -- )          3 _section ;                                        \ use text section, read-only data
: 'here     ( -- a )        dp^ @ 2* dp[] + ;                                   \ address of the currently selected here
: ,         ( n -- )        'here @ a! !a+ a 'here ! ;                          \ compile a cell or slice to the section
: '         ( <name> -- xt ) x' drop ;                                          \ look up the xt of a word
: [']       ( <name> -- )   '  postpone literal ; immediate                     \ compile the xt of a word
: here      ( -- a )        'here @ ;                                           \ address of first free cell or slice
: align     ( -- )          here 20 bit 'here ! ;                               \ convert here to a cell address
: allot     ( n -- )        align here + 'here ! ;                              \ allot n cells in the section
: negate    ( n1 -- n2 )    1 swap inv + ;                                      \ n2 = -n1
: amask     ( a -- a' )     3FFFFF and ;                                        \ strip off address metadata
: unused    ( -- n )        'here a! @a+ negate @a+ + amask ;                   \ number of cells left in the section
: chere     ( -- addr )     postpone |inst [ dp[] 4 + ] literal @ ;             \ flush instructions and return code address
: lshift    ( u1 u2 -- u3 ) shft[ ]shl ;                                        \ left shift u1 by u2 to produce u3
: rshift    ( u1 u2 -- u3 ) shft[ ]shr ;                                        \ right shift u1 by u2 to produce u3
: iaddr     ( a1 -- a2 )    dup 2* swap 1A rshift 1 and + ;                     \ convert instruction address to linear format
: caddr     ( a1 -- a2 )    1 over and 1A lshift  swap 2/ amask + 80000000 + ;  \ convert instruction address to normal format
: [char]    ( <char> -- )   char  postpone literal ; immediate                  \ compile a character as a literal
: @+        ( a -- a+1 n )  a! @a+ a swap ;                                     \ fetch next cell or slice in a stream
: >sys      ( imm -- )      7000 + ,inst ;                                      \ compile >sys
: sys>      ( imm -- )      7400 + ,inst ;                                      \ compile sys>
decimal -->
( BLOCK++ )
( Control structures __________________________________________ 10/10/26 BNE )
hex
: _again    ( a inst -- )   >r iaddr  chere iaddr inv + 1FF and r> + ,inst ;    \ compile a backward control branch
: then      ( C: a -- )     chere iaddr  over iaddr inv +  swap                 \ resolve a forward branch (if else then)
                            a! 1FF and @a + !a ; immediate
: endif     ( C: a -- )     postpone then ; immediate                           \ a more natural-reading if ... endif
: -if       ( C: -- a ) ( n -- n ) chere _pbran ,inst ; immediate               \ if n is negative, run up to then; keeps n
: if        ( C: -- a ) ( flag -- ) chere _0bran ,inst ; immediate              \ if flag is nonzero, run up to else or then
: ahead     ( C: -- a )     chere _bran ,inst ; immediate                       \ compile an unconditional forward branch
: else      ( C: a1 -- a2 ) postpone ahead  swap  postpone then ; immediate     \ start the part that runs when if fails
: begin     ( C: -- a )     chere ; immediate                                   \ start a loop
: again     ( C: a -- )     _bran _again ; immediate                            \ loop back to begin forever
: until     ( C: a -- ) ( flag -- ) _0bran _again ; immediate                   \ loop back to begin until flag is nonzero
: while     ( C: a1 -- a2 a1 ) ( flag -- ) postpone if  swap ; immediate        \ leave the loop after repeat if flag is 0
: repeat    ( C: a2 a1 -- ) postpone again  postpone then ; immediate           \ loop back to begin and resolve while
: for       ( C: -- a ) ( n -- ) postpone >r  chere ; immediate                 \ start a loop that runs n times
: next      ( C: a -- )     _next _again ; immediate                            \ loop back to for until the count runs out
: 0=        ( x -- flag )   if 0 exit then -1 ;                                 \ true if x = 0, else false
: 0<>       ( x -- flag )   0= inv ;                                            \ true if x != 0, else false
: =         ( n1 n2 -- flag )  xor 0= ;                                         \ true if n1 = n2, else false
: execute   ( xt -- )       dup 0=  over 3FFFFF inv and + 0<> -15 and yeet >r ; \ safe execute, may remove limits later
: ,jump     ( xt ta -- )    _code here >r  'here ! compile,  postpone exit      \ compile a jump to xt at ta
                            r> 'here ! _udata ;
: _does     ( -- ) ( R: ra -- )  r> created @  ,jump ;                          \ run-time part of `does>`
: does>     ( -- )          postpone _does ; immediate                          \ create ... does>
: >body     ( xt -- a )     0 swap caddr  begin  @+     ( acc a inst )          \ extract body address from code
    dup E000 and _lit = if 1FFF and nip swap 0D lshift + exit then              \ trailing lit instruction detected
    3FF and >r  swap 0A lshift r> + swap  ( acc a )  again ;                    \ otherwise, assume prefix instructions
decimal -->
( BLOCK++ )
( Multitasking 1/2 ____________________________________________ 10/9/26 BNE )
:noname     ( next a -- )   drop u! ; constant asleep                           \ xt of sleeping task
: pause     ( -- next a | ) [ _user ,inst ] a if @a+ @a+ >r a exit then ;       \ skip to the next task, or nothing if no task
: stop      ( -- )          asleep [ _user 1 + ,inst ] !a pause ;               \ stop the current task
:noname     ( next a -- )
    a! u! task[ dup !a                 ( ix r:d |R: ra ? \ save current r:d )   \ saves the current task's stack window
    [ _user 2 + ,inst ] @a ]task r> drop drop                                   \ and loads the next task's stack window
; constant awake                                                                \ xt of awake task

hex _idata variable stackused  300030 stackused !   ( reserved for terminal )   \ stack space in use, as rp:sp; stacks grow up
decimal _udata here 3 allot constant operator                                   \ the operator task is launched by multitask

: multitask ( -- ) ( R: ra -- )                                                 \ launch the multitasker before any activate
    operator                              ( this bulky literal is used once )   \ The operator task belongs to the terminal.
    dup u! dup a! !a+  awake !a+     ( ix |R: ra \ populate next and action )   \ Create a task loop with a single task in it
    task[  !a+ r> drop       ( capture the operator's r:d and start address )   \ and assign it to the operator.
;
-->
( BLOCK++ )
( Multitasking 2/2 ____________________________________________ 10/9/26 BNE )
: activate  ( task -- ) ( R: ra -- )                                            \ start task running the code after activate
    a! @a+ @a+                                                   ( r:d user )   \ get task parameters
    [ _user ,inst ] a 0= -121 and yeet          ( the task queue must exist )   \ make sure multitask has set up the task loop
    @a over !a  swap a!               ( r:d opnext ) ( link in the new task )   \ link this task into the queue
    !a+  awake !a+                 ( r:d \ NEXT and ACTION set, A = its R:D )   \ initialize this task's NEXT and ACTION values
    task[ !a  ]task           ( r:d \ park our rp:sp in R:D, use its stacks )   \ this is where Claude took over...
    task[  @a swap !a  ]task  ( our-r:d \ R:D = its rp:sp with ra on top, back )
    r> drop drop drop  r> drop         ( drop ra: return to caller's caller )
;

: task      ( user_cells data_stack return_stack <name> -- )                    \ create a task with stacks and a user area
    _idata align create  16 lshift +                               ( uc r:d )   \ in the idata section.
    stackused a! @a  swap over +                               ( uc r:d new )   \ the stacks grow upward
    dup stack-masks inv and 0<> -118 and yeet  ( check against upper limits )   \ yeet if not enough room in the stack space
    swap a swap  , !  ( uc )  _udata here       ( CELL 0 = stack base rp:sp )   \ compile stack base to idata
    swap 3 + allot  _idata ,  _udata    ( CELL 1 = address of the user area )   \ allocate uninitialized data space for task
;
-->
( BLOCK++ )
( Multiplier and divider math ________________________________ 10/25/26 BNE )
: mcwait    ( -- )          begin [ 43 sys> ] while pause repeat ;              \ wait for math coprocessor to finish
: um*       ( u1 u2 -- ud )      \ Multiply u1 by u2, giving the unsigned double-cell product ud.
                            [ 40 >sys 32 >sys ] mcwait [ 40 sys> 41 sys> ] ;
: m*        ( n1 n2 -- d )       \ d is the signed product of n1 times n2.
                            [ 40 >sys 33 >sys ] mcwait [ 40 sys> 41 sys> ] ;
: mu/mod    ( ud1 u1 -- u2 ud2 ) \ Unsigned divide ud1 by u1, u2 is the remainder, ud2 is the quotient.
                            [ 41 >sys 40 >sys 34 >sys ] mcwait [ 42 sys> 40 sys> 41 sys> ] ;
: */mod     ( n1 n2 n3 -- r q )  \ Multiply n1 by n2 producing d, then divide by n3 producing remainder n4 and quotient n5.
                            [ 41 >sys 40 >sys 35 >sys ] mcwait [ 42 sys> 40 sys> ] ;
: _um*      ( u1 u2 -- ud ) a!  0  16 >r |inst +* +* unext  nip a swap ;        \ a multiply that does not use extra hardware
: *         ( n1 n2 -- n3 ) um* drop ;                                          \ multiply
: u/mod     ( u1 u2 -- rem quot ) 0 swap mu/mod drop ;                          \ unsigned divide: remainder and quotient
: */        ( n1 n2 n3 -- n4 ) */mod nip ;                                      \ n1 * n2 / n3 with double intermediate product
: um/mod    ( ud u1 -- u2 u3 ) mu/mod drop ;                                    \ ud / u1; remainder u2, quotient u3
-->
( BLOCK++ )
( Basic math __________________________________________________ 10/25/26 BNE )
: -         ( n1 n2 -- n3 ) negate + ;                                          \ subtract n2 from n1
: 1+        ( n1 -- n2 )    1 + ;                                               \ increment by 1
: 1-        ( n1 -- n2 )    -1 + ;                                              \ decrement by 1
: or        ( n1 n2 -- n3 ) inv swap inv and inv ;                              \ bitwise OR
: 0<        ( n -- flag )   -if dup xor inv exit then dup xor ;                 \ true if n < 0
: dnegate   ( d1 -- d2 )    inv swap inv 1+ swap cy + ;                         \ double-cell negate
: abs       ( n -- u )      -if negate then ;                                   \ absolute value
: dabs      ( d -- ud )     -if dnegate then ;                                  \ double-cell absolute value
: +!        ( n a -- )      a! @a + !a ;                                        \ add n to cell at a
: s>d       ( n -- d )      dup 0< ;                                            \ convert single to double
: min       ( n1 n2 -- n3 ) 2dup - -if 2drop exit then drop nip ;               \ n3 is the lesser of n1 and n2
: max       ( n1 n2 -- n3 ) 2dup - -if drop nip exit then 2drop ;               \ n3 is the greater of n1 and n2
: rot       ( x1 x2 x3 -- x2 x3 x1 ) b! swap b swap ;                           \ rotate third item to top of stack
: u<=       ( u1 u2 -- flag1 ) swap - drop cy ;                                 \ unsigned less than or equal
: u<        ( u1 u2 -- flag ) swap u<= 0= ;                                     \ unsigned less than
: usec      ( u -- )        capusec x@ +                                        \ pause until u microseconds have passed
                            begin  pause  capusec x@ over - 0< 0= until  drop ;
: ms        ( u -- )        1000 * usec ;                                       \ pause until u milliseconds have passed
-->
( BLOCK++ )
( String output _______________________________________________ 10/14/26 BNE )
_idata variable emit_xt
: conemit   ( c -- )        begin [ 18 sys> ] while pause repeat [ 16 >sys ] ;  \ emit to console
: con       ( -- )          ['] conemit emit_xt ! ;  con                        \ direct emit to console
: emit      ( c -- )        emit_xt @ >r ;                                      \ late-bound emit
: key       ( -- c )        [ 16 sys> ] ;                                       \ raw xterm input, blocking
: key?      ( -- flag )     [ 17 sys> ] ;                                       \ a smarter key would pause while `key?`
: 1parm     ( n1 -- | n1 )  1- -if  drop r> drop exit then 1+ ;                 \ exit the caller if there is nothing to do
: 2parms    ( n1 n2 -- | n1 n2 ) 1- -if drop r> drop drop exit then 1+ ;        \ exit the caller if there is nothing to do
: type      ( ca n -- )     2parms for @+ emit next drop ;                      \ output a string using emit
: safeemit  ( c -- )        -32 + -if drop [ char . 32 - ] literal then 32 + emit ; \ safer emit, filter out control chars
: safetype  ( ca n -- )     2parms for @+ safeemit next drop ;                  \ output a string using safeemit
: $type     ( ca -- )       @+ type ;                                           \ output counted string
: $abort    ( flag ca -- )  swap if $type -2 yeet exit then  drop ;             \ output counted string if flag is nonzero
: ."        ( "ccc<quote>" -- ) _," postpone literal  postpone $type ; immediate \ compile a string to type at run time
: abort"    ( "ccc<quote>" -- ) _," postpone literal  postpone $abort ; immediate \ compile abort" message
: digit     ( n -- char )   dup -10 + 0< -7 and + [char] 7 + ;                  \ convert to ASCII digit
: space     ( -- )          bl emit ;                                           \ send a space
: spaces    ( n -- )        1parm for space next ;                              \ send 0 or more spaces
-->
( BLOCK++ )
( String support ______________________________________________ 10/10/26 BNE )
: bytepos   ( a -- n )      25 rshift 3 and ;                                   \ get the byte offset
: posbyte   ( a pos -- a' ) 25 lshift  swap amask [ 8 27 lshift ] literal + + ; \ set the byte offset for a byte address
: chars+    ( a n -- a' )   over bytepos + 3 over and >r  2/ 2/ +  r> posbyte ; \ skip forward n chars, chars must be 8-bit
: char-     ( a -- a' )   dup bytepos 1- -if drop 3 swap 1- swap then posbyte ; \ step a byte address backward
: bytespan  ( a1 a0 -- n ) over bytepos  over bytepos - >r - amask 2* 2* r> + ; \ number of bytes within cell addresses
: 3parms    ( x1 x2 n -- | x1 x2 n ) 1- -if drop r> drop 2drop exit then 1+ ;   \ exit the caller if there is nothing to do
: cmove     ( a1 a2 n -- )  3parms >r b! a! |inst @a+ !b+ unext ;               \ move cells or slices (note address types)
: cmove>    ( a1 a2 n -- )  3parms dup >r chars+  swap r@ chars+ ( a1 a2 | n )  \ move bytes forward by small offset
    begin  char- swap char- swap  dup a! over b! @a !b  next  2drop ;
: -trailing ( a len -- a' len' )                                                \ trim trailing blank bytes
    >r 8 bit dup r> chars+  ( a0 a1 )                                           \ convert to byte addresses
    begin 2dup xor while
        char- dup @  bl xor  if  over bytespan  1+ exit  then                   \ seek backward for nonblank
    repeat  dup xor ;       ( a' 0 )
: fill      ( ca u c -- )   swap 3parms >r swap a! |inst  dup !a+ unext drop ;  \ fill u cells or slices with c
: blank     ( ca u -- )     bl fill ;                                           \ fill u chars with spaces
-->
( BLOCK++ )
( Numeric output ______________________________________________ 10/9/26 BNE )
_udata variable hld                                                             \ position of the last held char in numbuf
create numbuf 10 allot                                                          \ pictured numeric output buffer, 40 bytes
: <#        ( ud -- ud )    40  hld ! ;                                         \ start numeric conversion
: #>        ( ud -- ca u )  2drop  numbuf hld @ tuck chars+  swap negate 40 + ; \ end numeric conversion
: hold      ( c -- )        hld a! @a 1-  dup 0< -17 and yeet  dup !a           \ append c to the numeric conversion buffer
                            numbuf swap chars+ ! ;                              \ yeet -17 if the buffer overflows
: #         ( ud1 -- ud2 )  base @ mu/mod rot digit hold ;                      \ append next digit
: #s        ( ud -- 0 )     begin # 2dup or 0= until ;                          \ append remaining digits
: sign      ( n -- )        -if [char] - hold exit then drop ;                  \ append minus sign if negative
: s.r       ( ca u width -- ) over - spaces type ;                              \ type a string right-justified in width
: d.r       ( d width -- )  >r dup >r dabs  <# #s r> sign #> r> s.r ;           \ signed double with width
: u.r       ( u width -- )  0 swap d.r ;                                        \ unsigned with width
: .r        ( n width -- )  >r s>d r> d.r ;                                     \ signed with width
: d.        ( d -- )        0 d.r space ;                                       \ signed double
: u.        ( u -- )        0 d. ;                                              \ unsigned
-->
( BLOCK++ )
( Search order ________________________________________________ 10/10/26 BNE )
: definitions ( -- )        context @ current ! ;                               \ compile into the first context wordlist
: contexts  ( -- n )        0 context begin @+ 128 and 0=                       \ number of wordlists in the search order
                            while swap 1+ swap repeat drop ;
: previous  ( -- )          contexts 1parm >r context dup slice+                \ drop the first wordlist of the search order
                            swap r> cmove ;
: also      ( -- )          context dup slice+  contexts 1+ cmove>              \ duplicate top of context stack
                            255  [ context |context| chars+ ] literal  ! ;      \ backstop the terminator
: vocabulary ( <name> -- )  _text align create wordlist , does> @ context ! ;   \ create a new vocabulary
: _contexts ( -- )          context contexts 2parms for @+ .wid next drop ;     \ list contexts with first on the left
: order     ( -- )          ." Context: " _contexts cr                          \ list the search order
                            ." Current: " current @ .wid ;
: thru     ( u1 u2 -- )    over - 1+ 2parms  for dup load 1+ next drop ;       \ load a sequence of blocks
-->
( BLOCK++ )
( Block editor 1/3 ____________________________________________ 10/11/26 BNE )
vocabulary editor  also editor definitions                                      \ vocabulary for the editor
_udata  variable scr                                                            \ block being edited or listed
variable listcnt                                                                \ line number for list
variable row                                                                    \ current line, 0 to 31
variable col                                                                    \ cursor column in the current line
variable #fbuf                                                                  \ length of the find string
create fbuf 32 allot                                                            \ find string, up to 128 bytes
: list      ( n -- )        dup scr !  0 listcnt !                              \ list block n and make it current
    block [ |block| 2* 2* ] literal -trailing  ( ca u )                         \ trim trailing rows
    begin dup while
        2dup 128 min dup >r  -trailing listcnt @ 2 u.r space safetype cr        \ trim trailing columns
        r@ - swap  r> 2/ 2/ + swap  1 listcnt +!                                \ step address in cells, count in bytes
    repeat  2drop ;
: .index    ( blk -- )      block 128 -trailing  type cr ;                      \ display the block index
: index     ( u1 u2 -- )    over - 1+ 2parms  for dup .index 1+ next drop ;     \ display an index of blocks
: 'line     ( n -- ca )     scr @ block 8 bit  swap 7 lshift chars+ ;           \ address of line n of the current block
: 'cursor   ( -- ca )       row @ 'line col @ chars+ ;                          \ address of the cursor
: remain    ( -- u )        128 col @ - ;                                       \ chars from the cursor to the end of the line
: me        ( -- )          row @ dup 'line 128 -trailing                       \ type the current line with its number
                            rot 2 u.r space type cr ;
head-
: _caret    ( -- )          col @ 3 + spaces [char] ^ emit cr ;                 \ mark the cursor under the line
: _upd      ( -- )          update save-buffers ;                               \ save the edit; QUIT empties the buffers
-->
( BLOCK++ )
( Block editor 2/3 ____________________________________________ 10/11/26 BNE )
: _len0     ( ca u -- ca n ) >r 0  begin dup r@ xor while                       \ n = chars before the first NUL, at most u
                            2dup chars+ @ 0= if r> drop exit then  1+           \ stop at the end of a terminal line
                            repeat  r> drop ;
: _src      ( -- ca )       blk @ if blk @ block 8 bit exit then TIB ;          \ address of the input source
: _rest     ( -- ca u )     _src >in @ chars+  >in @ 128 + -128 and             \ parse the rest of the line
                            dup >in @ - swap >in !  _len0 -trailing ;
: _gap      ( n -- ca n' )  remain min  dup remain swap - >r  'cursor swap      \ open n blank chars at the cursor
                            2dup chars+ >r over r> r> cmove>  2dup blank ;      \ shift the rest of the line right
: _cut      ( n -- )        remain min  dup remain swap - >r  'cursor           \ delete n chars at the cursor
                            2dup swap chars+ over r> cmove                      \ shift the rest of the line left
                            over remain swap - chars+ swap blank ;              \ blank the end of the line
: _ins      ( ca u -- )     dup _gap rot drop  dup col +!  cmove ;              \ insert text at the cursor and move past it
: _put      ( ca u -- )     row @ 'line dup 128 blank  swap 128 min cmove ;     \ make text the current line
: _fbuf     ( -- ca )       fbuf 8 bit ;                                        \ address of the find string
: _same     ( ca1 ca2 u -- flag ) for  over @ over @ xor                        \ true if u chars at ca1 and ca2 match
                            if 2drop r> drop 0 exit then                        \ a difference: no match
                            1 chars+ swap 1 chars+ swap  next  2drop -1 ;
: _at       ( pos -- flag ) dup 127 and #fbuf @ + 128 u<= if                    \ true if the find string is at pos
                            0 'line swap chars+  _fbuf #fbuf @ _same exit       \ in the block, within a line
                            then  drop 0 ;
: _find     ( -- flag )     row @ 7 lshift col @ +  dup #fbuf @ + 4097 swap -   \ search from the cursor; move past a match
                            1- -if 2drop 0 exit then 1+  ( pos n )              \ n = positions left to try
                            for  dup _at if  dup 7 rshift row !  127 and #fbuf @ + col !
                            r> drop -1 exit then  1+  next  drop 0 ;
: _found    ( -- flag )     #fbuf @ 0= if 0 exit then  col @ #fbuf @ -          \ true if the find string ends at the cursor
                            -if drop 0 exit then  row @ 'line swap chars+  _fbuf #fbuf @ _same ;
head+ -->
( BLOCK++ )
( Block editor 3/3 ____________________________________________ 10/11/26 BNE )
: edit      ( n -- )        0 row !  0 col !  list ;                            \ list block n and edit it from line 0
: l         ( -- )          scr @ list ;                                        \ list the current block
: n         ( -- )          scr @ 1+ list ;                                     \ list the next block
: b         ( -- )          scr @ 1- list ;                                     \ list the previous block
: fl        ( -- )          flush ;                                             \ save modified blocks to storage
: t         ( n -- )        31 and row !  0 col !  me ;                         \ make line n current and type it
: p         ( "text" -- )   _rest _put _upd me ;                                \ put text into the current line
: i         ( "text" -- )   _rest  row @ 'line dup 128 chars+                   \ insert text as a line above the current one
                            31 row @ - 7 lshift cmove>  _put _upd me ;          \ line 31 is lost
: d         ( n -- )        31 and dup row !  0 col !  'line dup 128 chars+ swap \ delete line n, moving the rowes below up
                            31 row @ - 7 lshift cmove  31 'line 128 blank  _upd me ;
: clear     ( -- )          0 'line 4096 blank _upd ;                           \ fill the current block with spaces
: f         ( "text" -- )   _rest dup if 128 min dup #fbuf ! _fbuf swap cmove   \ find text from the cursor; no text: again
                            else 2drop then  #fbuf @ 0= if ." nothing to find" cr exit then
                            _find if me _caret exit then  ." not found" cr ;
: c         ( "text" -- )   _rest _found 0= if 2drop ." find first" cr exit then \ change the found string to text
                            #fbuf @ negate col +!  #fbuf @ _cut  _ins _upd me ;
: e         ( -- )          'cursor remain blank _upd me ;                      \ erase from the cursor to the end of the line
: r         ( "text" -- )   _rest 'cursor remain 2dup blank  rot min cmove      \ replace the rest of the line with text
                            _upd me ;
: s         ( n -- )        _gap 2drop _upd me ;                                \ insert n spaces at the cursor
: x         ( n -- )        _cut _upd me ;                                      \ delete n chars at the cursor
-heads forth definitions                                                        \ leave the search order as [forth, forth]
-->
( BLOCK++ )
( LCD display _________________________________________________ 10/14/26 BNE )
\ 4bpp Font data needs a 16-color palette in 5:6:5 format. 
_udata create palette 8 ( cells ) allot                                         \ uint16_t[16] color table
head-
: 'palette  ( -- a )        hld @  palette over 2/ +                            \ address the palette uint16_t
                            swap 1 and if [ 16 22 lshift ] literal + then ;     \ re-use `hld`
: 'red      ( -- a5 )       'palette [ 5 27 lshift 11 22 lshift + ] literal + ; \ red slice address
: 'green    ( -- a6 )       'palette [ 6 27 lshift  5 22 lshift + ] literal + ; \ green slice address
: 'blue     ( -- a5 )       'palette [ 5 27 lshift  0 22 lshift + ] literal + ; \ blue slice address
variable fgcolor                                                                \ foreground color in RGB 6:6:6 format
variable bgcolor                                                                \ background color in RGB 6:6:6 format
: shade     ( .. -- .. shade ) swap @+ rot @+ rot  over -  hld @  15 */ + ;     \ get the next shade in the B,G,R sequence
: reshade   ( -- )          fgcolor 6 bit  bgcolor 6 bit                        \ interpolate one table entry
    shade 2/ 'blue !  shade 'green !  shade 2/ 'red !  2drop ;
: repalet   ( -- )          16 for r@ 1- hld !  reshade  next ;                 \ reshade the entire palette
head+
: fg!       ( color -- )    fgcolor ! repalet ;                                 \ set the foreground color
: bg!       ( color -- )    bgcolor ! repalet ;                                 \ set the background color
-heads
: color:    ( r g b <name> -- ) >r swap 6 lshift + 6 lshift r> + constant ;     \ define a color
63 63 63 color: white
240 constant LCDwidth                                                           \ panel width in pixels
240 constant LCDheight                                                          \ panel height in pixels
_text create font0  100 , 0 ,                                                   \ default font chain
_idata variable font   font0 font !                                             \ address of a list of font blocks, ending in 0
_udata variable lcd_x                                                           \ left edge of the next glyph's cell
variable lcd_y                                                                  \ top edge of the next glyph's cell
variable kerning                                                                \ extra pixels between glyphs
: at        ( x y -- )      lcd_y !  lcd_x ! ;                                  \ set the position of the next glyph
-->
( BLOCK++ )
( Font lookup _________________________________________________ 10/14/26 BNE )
\ Fonts from mkfont.py --blocks, written at byte 32 of their first block by putbin.py. Flash page 3 can
\ cache one font: its first 16 blocks, with the font's block in cell 0 and its cached bytes in cell 1.
3 page constant fontpage                                                        \ flash page 3, the font cache
_udata variable fblk                                                            \ block where the font being read starts
head-
: _fcached  ( -- u )        fontpage @ fblk @ = if fontpage 1+ @ exit then 0 ;  \ bytes of the font in page 3, or 0
: _fcell    ( o -- a )      32 +  dup 1+ _fcached u<=                           \ cell address of font byte o
                            if 2/ 2/ fontpage + exit then
                            dup 12 rshift fblk @ + block  swap 4095 and 2/ 2/ + ;
: _f16      ( o -- a )      dup _fcell 16 bit  swap 2 and                       \ the 16-bit slice at font byte o
                            if [ 16 22 lshift ] literal + then ;
: _fentry   ( i -- x )      2* 2* 528 + _fcell @ ;                              \ index entry i: low byte, record offset
: _fend     ( -- o )        0  8 _f16 @ 1parm                                   \ highest glyph record offset
                            for  r@ 1- _fentry 16777215 and max  next ;
head+
: glyph     ( u blk -- a )  fblk !  dup 255 and >r                              \ u's glyph record as 16-bit slices, or 0
    8 rshift 2* 12 + _f16 @+ swap @             ( lo hi |R: low byte of u )
    begin  2dup xor while
        2dup + 2/  dup _fentry                  ( lo hi mid x )
        dup 24 rshift r@ = if  nip nip nip 16777215 and  r> drop _f16 exit  then
        24 rshift r@ u<= if  1+ rot drop swap  else  nip  then
    repeat  2drop r> drop 0 ;
: font-cache ( blk -- )     fontpage open-flash  -1 fontpage !  fblk !          \ cache the font at blk in flash page 3
    _fend 32 + 12 rshift 1+ 16 min              ( blocks )
    dup for  r@ 1- dup fblk @ + block  swap 10 lshift fontpage +  1024 cmove  next
    fblk @  fontpage a!  !a+  12 lshift !a  close-flash ;
: fontheight ( -- u )       font @ @ fblk !  6 _f16 @ 255 and ;                 \ cell height of the first font in the list
-heads
-->
( BLOCK++ )
( LCD text 1/3 ________________________________________________ 10/25/26 BNE )
: lcd-tx    ( u -- )        [ 24 >sys ] ; \ Drive the LCD bus: bit 15 reset, 14 CSX, 13 command, 7:0 the byte (lcd_tx)
head-
: _cmd      ( b -- )        8192 or lcd-tx ;                                    \ send a command byte to the LCD
: _w16      ( u -- )        dup 8 rshift lcd-tx  255 and lcd-tx ;               \ send a 16-bit parameter, high byte first
: _span     ( u1 u2 b -- )  _cmd  swap _w16 _w16 ;                              \ send command b with the range u1 to u2
: _window   ( x y w h -- )  >r  rot tuck + 1- 42 _span                          \ write w x h pixels from x,y on (RAMWR)
                            r> over + 1- 43 _span  44 _cmd ;
head+
: clear-rect ( x y w h -- ) 2dup * >r  _window  r>                              \ paint w x h pixels at x,y with palette entry 0
                            begin dup while  dup 512 min  dup 1- 16384 or lcd-decode  -  repeat  drop ;
head-
: _npx      ( x -- n )      dup 14 rshift  dup 0= if drop                       \ number of pixels a font code describes
                            dup 7 rshift 7 and  swap 7 and + 2 + exit then
                            dup 1 = if drop 511 and 1+ exit then
                            2 = if 31 and 2 + exit then  3 and 3 + ;
: _draw     ( a n -- )      begin  dup 1- 0< 0= while                           \ send codes from a until n pixels are out
                            >r @+ dup lcd-decode _npx r> swap -  repeat  2drop ;
: _lookup   ( c -- a )      font @  begin dup @ while  2dup @ glyph             \ c's glyph in the first font that has it
                            dup if nip nip exit then  drop 1+  repeat  2drop 0 ;
_udata variable _wh                                                             \ glyph width and height bytes
variable _xy                                                                    \ glyph x and y offsets in the cell
: _w        ( -- u )        _wh @ 255 and ;                                     \ glyph width
: _h        ( -- u )        _wh @ 8 rshift ;                                    \ glyph height
: _wrap     ( fh -- )       lcd_x @ _xy @ 255 and + _w +  LCDwidth u<=          \ next row if the glyph crosses the right edge
                            if drop exit then  0 lcd_x !  dup lcd_y +!
                            lcd_y @ + LCDheight u<= if exit then  0 lcd_y ! ;
-->
( BLOCK++ )
( LCD text 2/3 ________________________________________________ 10/14/26 BNE )
head+
: lemit     ( c -- )        fontheight swap  _lookup                            \ draw c at lcd_x,lcd_y in the current font
    dup 0= if drop [char] ? _lookup then  dup 0= if 2drop exit then     ( fh a )
    @+ _wh !  @+ _xy !  @+ 255 and >r  swap _wrap                   ( codes |R: advance )
    _w _h *  dup if  >r  lcd_x @ _xy @ 255 and +  lcd_y @ _xy @ 8 rshift +  _w _h _window
                     r> _draw  else  2drop  then
    r> kerning @ +  lcd_x +! ;
: lcls      ( -- )          0 0 LCDwidth LCDheight clear-rect  0 0 at ;         \ clear the panel to palette entry 0
: LCDstart  ( -- )          LCDwidth LCDheight 1 palette lcd-init  white fg!    \ start the IPS panel: 16-bit pixels, cleared
    32768 lcd-tx 1 ms  16384 lcd-tx 5 ms  17 _cmd 120 ms               ( reset, SLPOUT )
    58 _cmd 85 lcd-tx  54 _cmd 0 lcd-tx  33 _cmd  41 _cmd  lcls ; ( 16-bit pixels, INVON, DISPON )
-heads head-
variable u8cp                                                                   \ code point being assembled from UTF-8
variable u8more                                                                 \ UTF-8 continuation bytes still to come
: _u8put    ( u -- )        dup 65535 u<= 0= if drop 65533 then  lemit ;        \ draw a code point, U+FFFD past U+FFFF
: _u8ctl    ( c -- )        dup 13 = if drop 0 lcd_x ! exit then                \ CR: line start; LF: next line; others: none
                            10 = if  0 lcd_x !  fontheight dup lcd_y +!
                            lcd_y @ + LCDheight u<= if exit then  0 lcd_y !  then ;
head+
: utf8reset ( -- )          0 u8more ! ;                                        \ start decoding UTF-8 afresh
: utf8lcd   ( c -- )        255 and  dup 128 and 0=                             \ feed a UTF-8 byte; draw each code point
    if  u8more @ if 65533 lemit utf8reset then  dup 31 u<= if _u8ctl exit then  _u8put exit  then
    dup 64 and 0= if  u8more @ 0= if drop 65533 lemit exit then      ( continuation )
        63 and  u8cp @ 6 lshift +  u8cp !  -1 u8more +!
        u8more @ if exit then  u8cp @ _u8put exit  then
    u8more @ if 65533 lemit then                        ( a lead byte cuts one short )
    dup 32 and 0= if 31 and 1 else  dup 16 and 0= if 15 and 2 else 7 and 3 then  then  u8more ! u8cp ! ;
-heads -->
( BLOCK++ )
( LCD text 3/3 ________________________________________________ 10/14/26 BNE )
: lcd       ( -- )          ['] utf8lcd emit_xt ! ;                             \ send output to LCD

: lcd!      ( -- )          LCDstart lcd ." Hello " ;
-->
( BLOCK++ )
( Save and restore idata ______________________________________ 10/9/26 BNE )
: save-idata ( -- )                                                             \ compile idata initialization structure
    _idata here  dp[] tuck - amask
    _text dup , ( 'src len )  for  @+ ,  next  drop                             \ first the length, then the data
;

: init-idata ( -- )                                                             \ load idata from idata structure
    1 @ @  @+ >r  a! dp[] b!
    |inst @a+ !b+ unext
;
-->
( BLOCK++ )
( System stats ________________________________________________ 10/9/26 BNE )
: _.map     ( a0 -- )                                                           \ map for selected section
    ." used "  here swap - amask .  ." unused " unused . ." cells" cr
;
: .map      ( -- )                                                              \ display map of all sections
    _udata ." _udata " _udata0 _.map  _idata ." _idata " dp[] _.map
    _code  ." _code  "       0 _.map  _text  ." _text  " _text0 _.map  _udata
;
-->
( BLOCK++ )
( Calendar ____________________________________________________ 10/16/26 BNE )
\ A time is `rtc@` seconds since 1970-01-01 00:00:00 UTC; the dates run from 1970 to 2106.
: leap?     ( year -- flag ) dup 400 u/mod drop 0= if drop -1 exit then         \ true if year has a February 29
                            dup 100 u/mod drop 0= if drop 0 exit then  3 and 0= ;
head-
: year-days ( year -- n )   leap? 1 and 365 + ;                                 \ number of days in year
_text create month-days                                                         \ days in each month of a common year
    31 , 28 , 31 , 30 , 31 , 30 , 31 , 31 , 30 , 31 , 30 , 31 ,
_udata variable year                                                            \ the year being converted
: month-length ( month -- n ) dup 1- month-days + @                             \ number of days in month of year
                            swap 2 =  year @ leap? and  1 and + ;
head+
: >time     ( u -- sec min hour ) 86400 u/mod drop  60 u/mod  60 u/mod ;        \ the time of day of time u
: >date     ( u -- day month year ) 86400 u/mod nip  1970 year !                \ the date of time u
    begin  year @ year-days  2dup u< 0= while  -  1 year +!  repeat  drop       ( day of the year from 0 )
    1 swap  begin  over month-length  2dup u< 0= while  -  swap 1+ swap  repeat
    drop  1+ swap  year @ ;
: time>     ( sec min hour -- u ) 60 * +  60 * + ;                              \ seconds from midnight to a time of day
: date>     ( day month year -- days ) year !  0 swap                           \ days from 1970-01-01 to a date
    begin  1- dup while  dup month-length  rot + swap  repeat  drop  + 1-       ( day of the year from 0 )
    year @ 1970 -  dup if  for  r@ 1969 + year-days +  next  exit  then  drop ;
: clock!    ( sec min hour day month year -- )                                  \ set the RTC to a date and time, UTC
                            date> 86400 *  >r  time>  r> +  rtc! ;
-heads
: .digits   ( u n -- )      base @ >r  decimal                                  \ print u in decimal as n digits, zero-filled
                            >r 0 <#  r> for # next  #>  type  r> base! ;
: .date     ( u -- )        >date  4 .digits  [char] - emit                     \ print the date of time u as yyyy-mm-dd
                            2 .digits  [char] - emit  2 .digits ;
: .time     ( u -- )        >time  2 .digits  [char] : emit                     \ print the time of day of u as hh:mm:ss
                            2 .digits  [char] : emit  2 .digits ;
-->
( BLOCK++ )
( LCD clock ___________________________________________________ 10/17/26 BNE )
_text create bigfont  120 , 100 , 0 ,                                           \ the double-size digits, then font 100
: _line     ( y -- )        8 swap  2dup at  LCDwidth 16 - fontheight clear-rect ; \ clear a line of text at y and start it
: show-time ( u -- )        emit_xt @ >r  font @ >r  lcd utf8reset              \ draw the date and time of u on the LCD
    16 _line  dup .date  16 fontheight +                            ( u y )
    bigfont font !  _line  .time  r> font !  r> emit_xt ! ;         ( the time in double size )
_udata variable shown                                                           \ the time show-time last drew
: clock     ( -- )          begin  pause  rtc@ dup shown @ xor                  \ redraw the clock on the LCD each second
                            if dup show-time then  shown !  again ;

( BLOCK 50 )
( Demo ________________________________________________________ 10/9/26 BNE )
: hi        ( -- )          ." 学如不及, 犹恐失之 " ;                            \ demo CJK message
: value     ( x <name> -- ) _idata align create , does> @ ;                     \ create name, which returns x; see to
: to        ( x <name> -- ) ' >body ! ;                                         \ change the x that value name returns
.( value test: ) 11 value ten   ten .  10 to ten  ten . cr                      \ a test value

variable counter                                                                \ bumped by multitasker
variable lastus                                                                 \ capusec time of the last demo-step
variable looptime                                                               \ microseconds per trip around the pause loop
: demo-step ( -- )          1 counter a! @a + dup !a                            \ application loop: count it, time it
    1023 and 0= if
        capusec x@  dup lastus @ -  looptime !  lastus !
    then ;

20 32 32 task t1                                                                \ sample task 1: the LCD clock (try)
10 20 20 task t2                                                                \ unused sample task

: try       ( -- )          LCDstart  t1 activate  clock ;                      \ start the LCD and run a clock on it in t1

:noname ( demo application )
    init-idata
    multitask  hi
    begin demo-step pause break again
; hex 80000000 ,jump decimal

:noname ( yeet handler: y@ = error code, x@ = PC after the fault )
    0 ]task  con  multitask     ( empty stacks, console output, one task )
    cr ." App error " y@ .  ." at " x@ hex . decimal cr
    begin  break  again         ( park the app until the next `cold` )
; hex 80000002 ,jump decimal
-->
( BLOCK++ )
( Resolve startup _____________________________________________ 10/9/26 BNE )

_text here 32 bit  1 !  ( Boot structure at end of text )
save-wids save-idata
_udata  ( leave data space selected: `variable` in text space corrupts headers )

( Cell 0: 1 or 2 instructions for jump to application )
( Cell 1: address of system initialization data {TBD} )
( Cell 2: 1 or 2 instructions for jump to yeet handler )

( BLOCK IGNORE )

close-flash

.( A demo application has now been compiled to flash. At this point, you can:) cr
.( - Enter `cold` to boot it up and `counter @ .` to see it working. ) cr
.( - `bye` and then `./lf -o 8` to boot and run from flash. ) cr

0 >options
