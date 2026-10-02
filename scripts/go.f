( LiteForth boot code )

empty only forth  0 open-flash
( forth wid = 0, only wid = 1 )
1 current !
: dummy ;
0 current !
: cells ; immediate
: base!     ( n -- )        base ! ;
: decimal   ( -- )          10 base! ;
: hex       ( -- )          16 base! ;

( I/O bindings )
: emit      ( c -- )        t_tx! ;
: emit?     ( -- flag )     t_tx? ;
: key       ( -- c )        t_rx ;
: key?      ( -- flag )     t_rx? ;

( dictionary )
hex
: definitions  ( -- )       context @ current ! ;
: variable  ( -- )          20 bits ;
: _section  ( n -- )        dp^ ! ;
: _udata    ( -- )          0 _section ;
: _idata    ( -- )          1 _section ;
: _code     ( -- )          2 _section ;
: _text     ( -- )          3 _section ;
: 'here     ( -- a )        dp^ @ 2* dp[] + ;
: ,         ( n -- )        'here @ a! !a+ a 'here ! ;
: '         ( <name> -- xt) x' drop ;
: here      ( -- a )        'here @ ;
: align     ( -- )          here 20 bit 'here ! ;
: allot     ( n -- )        align here + 'here ! ;
: negate    ( n -- -n )     1 swap inv + ;
: amask     ( a -- a' )     3FFFFF and ;
: unused    ( -- n )        'here a! @a+ negate @a+ + amask ;
: chere     ( -- addr )     postpone |inst [ dp[] 4 cells + ] literal @ ;
: lshift    ( u1 u2 -- u3 ) shft[ ]shl ;
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

: 0=        ( x -- flag )   if 0 exit then -1 ;

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

_udata create operator  4 allot   ( the terminal's user area )

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
    _udata here  swap 4 + allot  _idata , ( header cell 1 = user area )
    _udata
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

( string output )
: @+        ( a -- a+1 n )  a! @a+ a swap ;
: 1+        ( n1 -- n2 )    1 + ;
: 1-        ( n1 -- n2 )    -1 + ;
: goodN     ( n1 -- | n1 )  1- -if  drop r> drop exit then 1+ ;
: goodAN    ( n1 n2 -- | n1 n2) 1- -if 2drop r> drop exit then 1+ ;
: type      ( ca n -- )     goodAN for @+ emit next drop ;
: $type     ( ca -- )       @+ type ;
: ."        ( string" -- )  _," postpone literal  postpone $type ; immediate
: +!        ( n a -- )      a! @a + !a ;
: -         ( n -- -n )     negate + ;

: _.map     ( -- )
   here . ." to " 'here 1+ @ .
   ." unused " unused . ." cells" cr
;
: .map      ( -- )
   _udata ." _udata " _.map
   _idata ." _idata " _.map
   _code  ." _code  " _.map
   _text  ." _text  " _.map
   _udata
;

: save-idata  ( -- )
    _idata here  dp[] tuck - amask
    _text dup , ( 'src len )
    for  @+ ,  next  drop
;

: init-idata  ( addr -- )
    1 @ @  @+ >r  a! dp[] b!  |inst
    @a+ !b+ unext
;

: rsh  >r |inst 2/ unext ;

: or        ( n1 n2 -- n3 ) inv swap inv and inv ;
: =         ( n1 n2 -- flag ) xor 0 swap if exit then invert ;

: hi  ." 学如不及，犹恐失之 " ;

variable counter

: demo-step  ( -- )
    1 counter +!
;

: ,jump  ( xt addr -- )
   _code here >r  'here ! ,compile  postpone exit
   r> 'here ! _udata
;

:noname ( demo application )
    init-idata
    hi
    begin  demo-step  break
    again
; hex 80000000 ,jump decimal

:noname ( yeet handler: y@ = error code, x@ = PC after the fault )
    cr ." App error " y@ .  ." at " x@ hex . decimal cr
    begin  break  again         ( park the app until the next `cold` )
; hex 80000002 ,jump decimal


_text here 32 bit  1 !  ( Boot structure at end of text )
save-wids save-idata
_udata  ( leave data space selected: `variable` in text space corrupts headers )

( Cell 0: 1 or 2 instructions for jump to application )
( Cell 1: address of system initialization data {TBD} )
( Cell 2: 1 or 2 instructions for jump to yeet handler )

close-flash

.( A demo application has now been compiled to flash. At this point, you can:) cr
.( - Enter `cold` to boot it up and `counter @ .` to see it working. ) cr
.( - `bye` and then `./lf -o 8` to boot and run from flash. ) cr

0 >options
