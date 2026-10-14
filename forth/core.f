( BLOCK IGNORE )

( From the ./bin folder, you can './lf.exe -o 3 < ../forth/core.f` )
( or in the case of Windows Powershell, 'Get-Content ..\forth\core.f | .\lf.exe -o 3` )
( which will load this file into a blank `lf`. )

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
( Basic compilation ___________________________________________ 10/9/26 BNE )
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
( Basic math __________________________________________________ 10/9/26 BNE )
: -         ( n1 n2 -- n3 ) negate + ;                                          \ subtract n2 from n1
: 1+        ( n1 -- n2 )    1 + ;                                               \ increment by 1
: 1-        ( n1 -- n2 )    -1 + ;                                              \ decrement by 1
: or        ( n1 n2 -- n3 ) inv swap inv and inv ;                              \ bitwise OR
: _um*      ( u1 u2 -- ud ) a!  0  16 >r |inst +* +* unext  nip a swap ;        \ a multiply that does not use the API
: 0<        ( n -- flag )   -if dup xor inv exit then dup xor ;                 \ true if n < 0
: dnegate   ( d1 -- d2 )    inv swap inv 1+ swap cy + ;                         \ double-cell negate
: abs       ( n -- u )      -if negate then ;                                   \ absolute value
: dabs      ( d -- ud )     -if dnegate then ;                                  \ double-cell absolute value
: +!        ( n a -- )      a! @a + !a ;                                        \ add n to cell at a
: *         ( n1 n2 -- n3 ) um* drop ;                                          \ multiply
: s>d       ( n -- d )      dup 0< ;                                            \ convert single to double
: min       ( n1 n2 -- n3 ) 2dup - -if 2drop exit then drop nip ;               \ n3 is the lesser of n1 and n2
: max       ( n1 n2 -- n3 ) 2dup - -if drop nip exit then 2drop ;               \ n3 is the greater of n1 and n2
: rot       ( x1 x2 x3 -- x2 x3 x1 ) b! swap b swap ;                           \ rotate third item to top of stack
: u<=       ( u1 u2 -- flag1 ) swap - drop cy ;                                 \ unsigned less than or equal
: */        ( n1 n2 n3 -- n4 ) */mod nip ;                                      \ n1 * n2 / n3 with double intermediate product
: um/mod    ( ud u1 -- u2 u3 ) mu/mod drop ;                                    \ ud / u1; remainder u2, quotient u3
-->
( BLOCK++ )
( String output _______________________________________________ 10/14/26 BNE )
: emit      ( c -- )        t_tx! ;                                             \ dumb emit may block
: emit?     ( -- flag )     t_tx? ;                                             \ a smarter emit would pause while `emit?`
: key       ( -- c )        t_rx ;                                              \ raw xterm input, blocking
: key?      ( -- flag )     t_rx? ;                                             \ a smarter key would pause while `key?`
: 1parm     ( n1 -- | n1 )  1- -if  drop r> drop exit then 1+ ;                 \ exit the caller if there is nothing to do
: 2parms    ( n1 n2 -- | n1 n2 ) 1- -if drop r> drop drop exit then 1+ ;        \ exit the caller if there is nothing to do
: type      ( ca n -- )     2parms for @+ emit next drop ;                      \ output a string using emit
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
: fill      ( ca u c -- )   3parms swap >r swap a! |inst  dup !a+ unext drop ;  \ fill u cells or slices with c
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

-->
( BLOCK++ )
( Block support _______________________________________________ 10/9/26 BNE )
: thru      ( u1 u2 -- )    over - 1+ 2parms  for dup load 1+ next drop ;       \ load a sequence of blocks
-->
( BLOCK 18 )
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
        2dup 128 min dup >r  -trailing listcnt @ 2 u.r space type cr            \ trim trailing columns
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

( BLOCK 30 )
( Demo ________________________________________________________ 10/9/26 BNE )
: hi        ( -- )          ." 学如不及，犹恐失之 " ;                           \ demo CJK message
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

20 32 32 task t1                                                                \ sample tasks 1
10 20 20 task t2                                                                \ unused sample task

variable ctr                                                                    \ starts incrementing after you type `try`
: try       ( -- )          t1 activate  begin pause  1 ctr +!  again ;         \ give t1 something to do

:noname ( demo application )
    init-idata
    multitask  hi
    begin demo-step pause break again
; hex 80000000 ,jump decimal

:noname ( yeet handler: y@ = error code, x@ = PC after the fault )
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
