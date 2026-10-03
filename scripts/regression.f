.( Testing Forth primitives ) 7 >options ( validation mode )
cr  ( This file is intended to replace stdin on a console app. )
( `make test` runs it as `lf -o 39` in a temp directory holding copies )
( of bin/lfflash.bin and bin/lfblocks.bin. The first failed            )
( T{ ... -> ... }T stops lf and prints the line.                       )

0 open-flash  ( map flash page 0 to a RAM buffer so it can be compiled to )

( ===================================================================== )
( Micro-ops: every 5-bit VM instruction                                 )
( ===================================================================== )

( + )
T{  0  5 +       ->    5 }T
T{  5  0 +       ->    5 }T
T{  0 -5 +       ->   -5 }T
T{ -5  0 +       ->   -5 }T
T{  1  2 +       ->    3 }T
T{  1 -2 +       ->   -1 }T
T{ -1  2 +       ->    1 }T
T{ -1 -2 +       ->   -3 }T
T{ -1  1 +       ->    0 }T

( cy: carry out of bit 31 of the last + )
T{  1  2 + drop cy   -> 0 }T
T{ -1  1 + drop cy   -> 1 }T
T{ -1 -1 + drop cy   -> 1 }T
T{ -1  0 + drop cy   -> 0 }T
T{ 2147483647 1 + drop cy -> 0 }T

( 2* )
T{    0 2*       ->    0 }T
T{ 4000 2*       -> 8000 }T
T{   -1 2*       ->   -2 }T

( 2/ is an arithmetic shift: it rounds toward minus infinity )
T{    0 2/       ->    0 }T
T{ 4000 2/       -> 2000 }T
T{   -4 2/       ->   -2 }T
T{   -3 2/       ->   -2 }T
T{   -1 2/       ->   -1 }T
T{    3 2/       ->    1 }T

( 2/c rotates right through carry: old cy to bit 31, bit 0 to cy )
T{ 1 2 + drop  4 2/c cy     -> 2 0 }T
T{ 1 2 + drop  5 2/c cy     -> 2 1 }T
T{ -1 1 + drop 4 2/c cy     -> -2147483646 0 }T
T{ -1 1 + drop 5 2/c 2/c cy -> -1073741823 0 }T

( inv, also named invert )
T{    0 invert   ->   -1 }T
T{   -1 invert   ->    0 }T
T{    5 inv      ->   -6 }T

( and )
T{  0  0 and     ->    0 }T
T{  0 -1 and     ->    0 }T
T{ -1  0 and     ->    0 }T
T{ -1 -1 and     ->   -1 }T
T{ 15  4 and     ->    4 }T

( xor )
T{  0  0 xor     ->    0 }T
T{  0 -1 xor     ->   -1 }T
T{ -1  0 xor     ->   -1 }T
T{ -1 -1 xor     ->    0 }T
T{ 12 10 xor     ->    6 }T

( dup drop swap over )
T{ 123 dup       -> 123 123 }T
T{ 123 456 drop  -> 123 }T
T{ 123 456 swap  -> 456 123 }T
T{ 123 456 over  -> 123 456 123 }T

( >r r@ r> )
T{ 99 >r 1 r@ 2 r>   -> 1 99 2 99 }T
T{ 42 >r 88 r>   -> 88 42 }T

( a a! b b! )
T{ 123 a! a      -> 123 }T
T{ 456 b! b      -> 456 }T

( !a @a !a+ @a+ )
T{ ram-base 31 + a! 55 !a @a -> 55 }T
T{ ram-base 32 + a! 10 !a+ 20 !a+ -> }T
T{ ram-base 32 + a! @a+ @a -> 10 20 }T
T{ ram-base 32 + a! @a+ drop a -> ram-base 33 + }T

( !b @b !b+ @b+ )
T{ ram-base 33 + b! 77 !b @b -> 77 }T
T{ ram-base 34 + b! 30 !b+ 40 !b+ -> }T
T{ ram-base 34 + b! @b+ @b -> 30 40 }T
T{ ram-base 34 + b! @b+ drop b -> ram-base 35 + }T

( +* multiply step: T:A shifts right, adding N to T when A is odd )
T{ 5 0 3 a! +* a -> 5 2 -2147483647 }T
T{ 5 0 2 a! +* a -> 5 0 1 }T
( A 32-step +* loop multiplies; see um*x after for/next below )

( unext repeats its instruction group while R counts down )
: 4*   ( n -- 4n ) 2 >r |inst 2* unext ;
T{ 3 4* -> 12 }T
: 16*  ( n -- 16n ) 4 >r |inst 2* unext ;
T{ 1 16* -> 16 }T

( u! sets the user pointer; a qlit instruction pushes U + u9 )
: u@   ( -- u ) [ 30208 ,inst ] ;
T{ 1234 u! u@ -> 1234 }T
T{ 0 u! u@ -> 0 }T


( ===================================================================== )
( Macros: two micro-ops in one word                                     )
( ===================================================================== )

T{ 123 456 nip   -> 456 }T
T{ 123 456 tuck  -> 456 123 456 }T
T{ 123 456 2dup  -> 123 456 123 456 }T
T{ 1 2 3 2drop   -> 1 }T
T{ 88 ram-base 35 + ! ram-base 35 + @ -> 88 }T
T{  1  2 um+     -> 3 0 }T
T{ -1  1 um+     -> 0 1 }T
T{ -1 -1 um+     -> -2 1 }T


( ===================================================================== )
( Bit fields: slice addresses, @as, s@, slice+, bits                    )
( ===================================================================== )

8 bits sbyte
4 bits nib
T{ 128 sbyte ! sbyte @  -> 128 }T
T{ sbyte s@             -> -128 }T
T{ 127 sbyte ! sbyte s@ -> 127 }T
T{ 15 nib ! nib a! @a   -> 15 }T
T{ nib a! @as           -> -1 }T
T{ 7 nib ! nib s@       -> 7 }T
T{ 5 nib ! sbyte @      -> 127 }T  ( neighbours are untouched )

HEX
T{ 84000001 slice+ -> 80000002 }T
T{ 80000002 slice+ -> 84000002 }T
T{ nib 1B shft[ ]shr -> 4 }T       ( slice width is in bits 31:27 )
T{ sbyte 1B shft[ ]shr -> 8 }T
DECIMAL


( ===================================================================== )
( Sys instructions: shifts, task state                                  )
( ===================================================================== )

T{ 3 SHFT[  5 ]SHL -> 40 }T
T{ 4 SHFT[ -1 ]SHL -> -16 }T
T{ 3 SHFT[ 44 ]SHR -> 5 }T
T{ 30 SHFT[ -1 ]SHR -> 3 }T
T{ 0 SHFT[ 7 ]SHR -> 7 }T

( task[ pushes rp:sp, copies R to the return stack and loads A with U )
: sp@  ( -- sp ) task[ r> drop 65535 and ;
: task-a ( -- u ) task[ r> 2drop a ;
T{ sp@ sp@ swap invert 1 + + -> 1 }T
T{ 77 u! task-a -> 77 }T
0 u!


( ===================================================================== )
( Math API calls                                                        )
( ===================================================================== )

( um* )
T{ 0 0 um*       -> 0 0 }T
T{ 1 1 um*       -> 1 0 }T
T{ 2 3 um*       -> 6 0 }T
T{ -1 -1 um*     -> 1 -2 }T

( m* )
T{  0  0 m*      -> 0 0 }T
T{  1  1 m*      -> 1 0 }T
T{ -1  1 m*      -> -1 -1 }T
T{ -2 -3 m*      -> 6 0 }T

HEX

( Simple Division - No Overflow into Upper Quotient )
T{     0 0 1 MU/MOD -> 0 0 0 }T
T{     7 0 1 MU/MOD -> 0 7 0 }T
T{     7 0 2 MU/MOD -> 1 3 0 }T
T{    64 0 7 MU/MOD -> 2 E 0 }T

( Large Lower Dividend )
T{  FFFF 0 2 MU/MOD -> 1 7FFF 0 }T
T{ 10000 0 2 MU/MOD -> 0 8000 0 }T

( High-Cell Dividend Producing High-Cell Quotient )
T{ 0 2 2 MU/MOD -> 0 0 1 }T
T{ 1 2 2 MU/MOD -> 1 0 1 }T
T{ 5 7 2 MU/MOD -> 1 80000002 3 }T

( Max Value Boundary Checks - 32-bit cells )
T{ FFFFFFFF FFFFFFFF FFFFFFFF MU/MOD -> 0 1 1 }T
T{ FFFFFFFF FFFFFFFF 80000000 MU/MOD -> 7FFFFFFF FFFFFFFF 1 }T

( Basic Multiplication and Division )
T{     0 2 1 */MOD -> 0 0 }T
T{     1 2 1 */MOD -> 0 2 }T
T{     2 3 2 */MOD -> 0 3 }T
T{     7 3 2 */MOD -> 1 0A }T  ( 21 / 2 = 10 rem 1 )
T{    64 2 7 */MOD -> 4 1C }T ( 200 / 7 = 28 rem 4 )

( Intermediate Product Exceeds Single-Cell Range )
( Ensures intermediate calculation uses double-cell width )
T{ 10000 10000 10000 */MOD -> 0 10000 }T
T{ 40000000 2 40000000 */MOD -> 0 2 }T
T{ 7FFFFFFF 2 2 */MOD -> 0 7FFFFFFF }T

( Signed Arithmetic Checks - Symmetric Division )
T{ -7 3 2 */MOD -> -1 -0A }T
T{ 7 -3 2 */MOD -> -1 -0A }T
T{ -7 -3 2 */MOD -> 1  0A }T

( Boundary Checks - 32-bit cells )
T{ 7FFFFFFF 2 7FFFFFFF */MOD -> 0 2 }T
T{ 7FFFFFFF 2 3 */MOD -> 2 55555554 }T

DECIMAL


( ===================================================================== )
( Compiler and defining words                                           )
( ===================================================================== )

( : ; and a call )
: ADD_TEN 10 + ;
T{ 5 ADD_TEN -> 15 }T
: ADD_20 ADD_TEN ADD_TEN ;
T{ 1 ADD_20 -> 21 }T

( exit in colon word )
: TEST_EXIT 123 exit 456 ;
T{ TEST_EXIT -> 123 }T

( literals: 13-bit, negative, and ones needing prefix instructions )
: lits  4095 -1 -4096 70000 2147483647 -2147483648 ;
T{ lits -> 4095 -1 -4096 70000 2147483647 -2147483648 }T

( constant )
1234 constant TEST_CONST
T{ TEST_CONST -> 1234 }T

( [ ] literal )
: nine [ 4 5 + ] literal ;
T{ nine -> 9 }T

( immediate )
: imm7 7 ; immediate
: seven imm7 literal ;
T{ seven -> 7 }T

( x' and ,compile )
: dup2 [ x' dup drop ,compile ] ;
T{ 4 dup2 -> 4 4 }T
T{ x' ADD_TEN nip -> 0 }T

( postpone a normal word: compiles code that compiles it )
: comp-dup postpone dup ; immediate
: dd 3 comp-dup ;
T{ dd -> 3 3 }T

( :noname compiles an anonymous definition and leaves its xt )
: before-nn 7 ;
:noname before-nn 10 + ; constant xt17
: call17 [ xt17 ,compile ] ;
T{ call17 -> 17 }T
T{ before-nn -> 7 }T  ( the ; after :noname did not hide before-nn )

( execute runs any xt: colon and :noname code is called, and a       )
( primitive, macro or API word runs as one instruction               )
T{ xt17 execute -> 17 }T
T{ x' before-nn drop execute -> 7 }T
T{ 4 x' dup drop execute -> 4 4 }T
T{ 1 2 x' nip drop execute -> 2 }T
T{ 2 3 x' um* drop execute -> 6 0 }T
: ex18  xt17 execute 1 + ;
T{ ex18 -> 18 }T

( base, hex, decimal )
T{ base @  HEX      -> 0A }T
T{ base @  DECIMAL  -> 16 }T

( page gives the base address of a memory page )
T{ 0 page -> 0 }T
T{ 1 page -> 524288 }T

( terminal input: more of this file is waiting on stdin; t_rx is      )
( tested below, after if/then are defined                              )
T{ t_rx? -> 1 }T

( wordlist returns the next wordlist number )
T{ wordlist wordlist swap invert 1 + + -> 1 }T


( ===================================================================== )
( Words from go.f, compiled and tested here                             )
( ===================================================================== )

: cells ; immediate
: base!     ( n -- )        base ! ;
: decimal   ( -- )          10 base! ;
: hex       ( -- )          16 base! ;

( I/O bindings )
: emit      ( c -- )        t_tx! ;
: emit?     ( -- flag )     t_tx? ;

( dictionary )
hex
: variable  ( -- )          20 bits ;
: _section  ( n -- )        dp^ ! ;
: _data     ( -- )          0 _section ;
: _idata    ( -- )          1 _section ;
: _code     ( -- )          2 _section ;
: _text     ( -- )          3 _section ;
: 'here     ( -- a )        dp^ @ 2* dp[] + ;
: here      ( -- a )        'here @ ;
: allot     ( n -- )        here 20 bit + 'here ! ;
: negate    ( n -- -n )     1 swap inv + ;
: unused    ( -- n )        'here a! @a+ negate @a+ + 3FFFFF and ;
: chere     ( -- addr )     postpone |inst [ dp[] 4 cells + ] literal @ ;
: rshift    ( u1 u2 -- u3 ) shft[ ]shr ;
: iaddr     ( a1 -- a2 )    dup 2* swap 1A rshift 1 and + ;

( control structures )
: _again    ( a inst -- )   >r iaddr  chere iaddr inv + 1FF and r> + ,inst ;
: then      ( a -- )        chere iaddr  over iaddr inv +  swap
                            a! 1FF and @a + !a ; immediate
: -if       ( -- a )        chere _pbran ,inst ; immediate
: if        ( -- a )        chere _0bran ,inst ; immediate
: ahead     ( -- a )        chere _bran ,inst ; immediate
: else      ( a1 -- a2 )    postpone ahead  swap  postpone then ; immediate
: begin     ( -- a )        chere ; immediate
: again     ( a -- )        _bran _again ; immediate
: until     ( a -- )        _0bran _again ; immediate
: while     ( a1 -- a1 a2 ) postpone if  swap ; immediate
: repeat    ( a1 a2 -- )    postpone again  postpone then ; immediate
: for       ( -- a )        postpone >r  chere ; immediate
: next      ( a -- )        _next _again ; immediate
decimal

( string output )
: @+        ( a -- a+1 n )  a! @a+ a swap ;
: 1+        ( n1 -- n2 )    1 + ;
: 1-        ( n1 -- n2 )    -1 + ;
: goodN     ( n1 -- | n1 )  1- -if  drop r> drop exit then 1+ ;
: goodAN    ( n1 n2 -- | n1 n2) 1- -if 2drop r> drop exit then 1+ ;
: type      ( ca n -- )     goodAN for @+ emit next drop ;
: $type     ( ca -- )       @+ type ;
: ."        ( string" -- )  _," postpone literal  postpone $type ; immediate

: or        ( n1 n2 -- n3 ) inv swap inv and inv ;
: -         ( n -- -n )     1 swap inv + + ;

( base! hex decimal )
T{ hex base @ decimal -> 16 }T
T{ base @ -> 10 }T

( arithmetic )
T{  5 negate -> -5 }T
T{ -5 negate ->  5 }T
T{  0 negate ->  0 }T
T{ 7 3 -     ->  4 }T
T{ 3 7 -     -> -4 }T
T{ 5 1+ 5 1- -> 6 4 }T
T{ 5 3 or    ->  7 }T
T{ 0 0 or    ->  0 }T
T{ -1 0 or   -> -1 }T
T{ -2147483648 31 rshift -> 1 }T
T{ 240 4 rshift -> 15 }T
T{ -1 28 rshift -> 15 }T

( dictionary pointers )
variable v1
T{ 42 v1 ! v1 @ -> 42 }T
T{ v1 slice+ v1 - -> 1 }T
T{ here 3 allot here swap - -> 3 }T
T{ unused 5 allot unused - -> 5 }T
T{ dp^ @ _idata dp^ @ _data dp^ @ -> 0 1 0 }T

( create, does> and >body )
create buf 2 allot
T{ here buf - -> 2 }T
( does> runs right after create: it patches that word to jump to the  )
( code that follows, which ] compiles and exit [ ends                   )
create arr 3 allot does> ] + exit [
T{ 2 arr 0 arr - -> 2 }T
T{ x' arr drop >body -> 0 arr }T

( if else then )
: t-if   ( n -- m ) if 1 else 2 then ;
T{ 0 t-if -> 2 }T
T{ 5 t-if -> 1 }T
T{ -5 t-if -> 1 }T

( -if runs its code when T is negative, and keeps T )
: neg?   ( n -- f ) -if drop -1 exit then drop 0 ;
T{ -5 neg? -> -1 }T
T{  0 neg? ->  0 }T
T{  5 neg? ->  0 }T

( capusec captures the microsecond counter into Y:X: the low word of a )
( later capture is no smaller, unless X wraps, every 71 minutes        )
T{ capusec x@  capusec x@  swap - neg?  -> 0 }T

( save-wids compiles a record to text space: skip address, number of        )
( wordlists, flash base, then the s_wid table. t-wids leaves the number  )
( and the skip address xor HERE, which is 0 when it is just past it      )
: t-wids  ( -- n 0 )
   _text here 32 bit  save-wids  dup 1 + @  swap @  here xor  _data ;
T{ t-wids  -> 4 0 }T

( begin until: loops while the flag is 0 )
: t-until ( n -- m ) begin 2* dup 64 and until ;
T{ 1 t-until -> 64 }T
T{ 3 t-until -> 96 }T

( begin while repeat )
: to-ten ( n -- 10 ) begin 1+ dup 10 xor while repeat ;
T{ 0 to-ten -> 10 }T
T{ 7 to-ten -> 10 }T

( begin again, left with exit )
: t-again ( n -- m ) begin 2* dup 100 xor if else exit then again ;
T{ 25 t-again -> 100 }T

( for next runs n times, with R counting n..1 )
: fsum   ( n -- sum ) 0 swap for r@ + next ;
T{ 4 fsum -> 10 }T
T{ 1 fsum -> 1 }T

( +* in a for loop: unsigned 32x32 multiply )
: um*x ( u1 u2 -- ud ) a! 0 32 for +* next nip a swap ;
T{  3  5 um*x -> 15 0 }T
T{ -1  2 um*x -> -2 1 }T
T{ -1 -1 um*x -> 1 -2 }T
T{ 65536 65536 um*x -> 0 1 }T

( @+ )
T{ ram-base 40 + a! 7 !a+ 8 !a ram-base 40 + @+ nip -> 7 }T
T{ ram-base 40 + @+ drop @+ nip -> 8 }T

( ===================================================================== )
( Multitasker, copied from go.f with the helpers it needs              )
( ===================================================================== )
: ,         ( n -- )        'here @ a! !a+ a 'here ! ;
: '         ( <name> -- xt) x' drop ;
: lshift    ( u1 u2 -- u3 ) shft[ ]shl ;
: 0=        ( x -- flag )   if 0 exit then -1 ;
: +!        ( n a -- )      a! @a + !a ;

( Multitasker: cooperative, round robin. Each task has a user area, which )
( the U register points to while the task runs:                           )
(   U+0 STATUS    xt that pause jumps to: awake, asleep or [start]        )
(   U+1 FOLLOWER  user area of the next task in the ring                  )
(   U+2 TASKNOW   saved rp:sp while the task isn't running                )
(   U+3 ENTRY     where a task that hasn't run yet starts                 )
( The physical stacks are shared: each task gets its own window of them.  )

: up      ( -- a )  [ 0 _user + ,inst ] a ;       ( the current user area )
: asleep  ( -- )   [ 1 _user + ,inst ] @a u!      ( U = follower )
                   [ 0 _user + ,inst ] @a >r ;    ( jump to its STATUS )
: pause   ( -- )   task[ [ 2 _user + ,inst ] !a   ( save T, R, rp:sp )
                   asleep ;
: awake   ( -- )   [ 2 _user + ,inst ] @a ]task   ( switch stacks )
                   r> drop drop ;                 ( restore R and T, return )
: [start] ( -- )   [ 3 _user + ,inst ] @a b!      ( B = ENTRY )
                   [ 0 _user + ,inst ] [ ' awake ] literal !a
                   [ 2 _user + ,inst ] @a ]task ; ( empty stacks, R = B: go )

_data create operator  4 allot   ( the terminal's user area )

: multi   ( -- )   ( start the ring with just the terminal )
   operator u!  operator a!  [ ' awake ] literal !a+  operator !a ;

hex
_idata variable stackused  300030 stackused ! ( reserved for the terminal task )
decimal
( Task headers are kept in _idata: cell 0 = stack base rp:sp, )
( cell 1 = address of the user area                         )

: task  ( user_cells data_stack return_stack <name> -- )
    _idata create  16 lshift +           ( uc r:d )
    stackused @ +                        ( uc new )
    dup stack-masks inv and if -118 yeet then
    stackused @ ,  stackused !           ( uc ) ( header cell 0 = base )
    _data here  swap 4 + allot  _idata , ( header cell 1 = user area )
    _data
;

( Add a task to the ring after the current one. The rest of the word that )
( calls `activate` becomes the task, and the caller of that word goes on: )
( : launch  t1 activate begin {your code} pause again ; )

: activate  ( task -- )  ( R: ra -- )
    up 0= if multi then
    a! @a+ @a+  dup b!                   ( base user ) ( B = user area )
    [ ' [start] ] literal !b+            ( STATUS )
    up 1 + @ !b+                         ( FOLLOWER = ours )
    swap 65536 + !b+                     ( user ) ( TASKNOW, rp+1 for ; )
    r> !b                                ( ENTRY = the rest of the caller )
    up 1 + !                             ( link it in after this task )
;

_data
variable mt-c1
variable mt-c2
variable mt-seen
10 16 16 task mt-t1
10 16 16 task mt-t2
: mt-run1  mt-t1 activate  111 222  begin 1 mt-c1 +! 7 pause drop  over over + mt-seen ! again ;
: mt-run2  mt-t2 activate  begin 1 mt-c2 +! pause again ;
: mt-spin  ( n -- ) for pause next ;
0 mt-c1 !  0 mt-c2 !  0 mt-seen !
( a task doesn't run until something pauses )
T{ mt-run1 mt-run2  mt-c1 @ mt-c2 @ -> 0 0 }T
( round robin; the terminal's and each task's stacks survive )
T{ 11 22 33  5 mt-spin  -> 11 22 33 }T
T{ mt-c1 @ mt-c2 @ mt-seen @ -> 5 5 333 }T
( asleep skips a task, awake resumes it )
T{ ' asleep mt-t2 1 + @ !  4 mt-spin  mt-c1 @ mt-c2 @ -> 9 5 }T
T{ ' awake  mt-t2 1 + @ !  3 mt-spin  mt-c1 @ mt-c2 @ -> 12 8 }T
( each task gets its own window of the stacks )
T{ mt-t1 @  mt-t2 @  stackused @ -> 3145776 4194368 5242960 }T

( label declares a word for a forward reference; a later : resolves it )
( without a new header, so the word defined before it stays visible   )
label lb-fwd
: lb-use     lb-fwd 1 + ;
: lb-tail    lb-fwd ;
: lb-before  5 ;
: lb-fwd     41 ;
T{ lb-use lb-tail lb-before lb-fwd -> 42 41 5 41 }T
T{ x' lb-fwd drop execute -> 41 }T

( t_rx reads stdin directly, so the next unread byte is the start of  )
( the next line of this file. With CRLF line endings and without -o 32 )
( lf ends the line at the CR, leaving the LF unread: nextc skips it.   )
: nextc  ( -- c ) t_rx dup 10 xor if exit then drop t_rx ;
T{ nextc -> 88 }T
X  ( nextc above consumed the X at the start of this line )

( counted strings )
T{ _," abc" @ -> 3 }T
: s-hello ." hello" ;
T{ emit? -> 0 }T


( ===================================================================== )
( Output words: these print, so they only check that nothing fails     )
( ===================================================================== )

cr .( Output: )
65 emit 66 emit 67 emit
: s-world ." , world" ;
s-hello s-world cr
1 2 3 .s . . . cr
-7 . 255 hex . decimal cr
0 .wid 1 .wid cr
only words forth cr
1 .page cr
ram-base 2 dump
49152 dumpi cr
x' nine drop 2 dasm
see nine


empty  ( resets the dictionary, removing everything defined above )
close-flash  ( programs the page back to flash and frees the buffer )
.( Tests completed successfully ) cr
 bye )
0 >options
