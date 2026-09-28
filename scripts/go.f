( LiteForth boot code )

empty only forth  0 open-flash
: cells ; immediate

( I/O bindings )
: emit      ( c -- )        t_tx! ;
: emit?     ( -- flag )     t_tx? ;
: key       ( -- c )        t_rx ;
: key?      ( -- flag )     t_rx? ;

( dictionary )
: variable  ( -- )          32 bits ;
: _section  ( n -- )        dp^ ! ;
: _data     ( -- )          0 _section ;
: _idata    ( -- )          1 _section ;
: _code     ( -- )          2 _section ;
: _text     ( -- )          3 _section ;
: 'here     ( -- a )        dp^ @ 2* dp[] + ;
: here      ( -- a )        'here @ ;
: negate    ( n -- -n )     1 swap inv + ;
: unused    ( -- n )        'here a! @a+ negate @a+ + ;
: chere     ( -- addr )     break [ dp[] 4 cells + ] literal @ ;
: rshift    ( u1 u2 -- u3 ) shft[ ]shr ;
: iaddr     ( a1 -- a2 )    dup 2* swap 26 rshift 1 and + ;

( control structures )
: _again    ( a inst -- )   >r iaddr  chere iaddr inv + 511 and r> + ,inst ;
: then      ( a -- )        chere iaddr  over iaddr inv +  swap
                            a! 511 and @a + !a ; immediate
: -if       ( -- a )        chere _pbran ,inst ; immediate
: if        ( -- a )        chere _0bran ,inst ; immediate
: ahead     ( -- a )        chere _bran ,inst ; immediate
: else      ( a1 -- a2 )    postpone ahead  swap  postpone then ; immediate
: begin     ( -- a )        chere ; immediate
: again     ( a -- )        _bran _again ; immediate
: until     ( a -- )        _0bran _again ; immediate
: while     ( a1 -- a1 a2 ) postpone if  swap ; immediate
: repeat    ( a1 a2 -- )    postpone again  postpone then ; immediate
: for       ( -- a )        postpone >r chere ; immediate
: next      ( a -- )        _next _again ; immediate

( the basics )
: or        ( n1 n2 -- n3 ) inv swap inv xor inv ;
: -         ( n -- -n )     1 swap inv + + ;

: base!     ( n -- )        base ! ;
: decimal   ( -- )          10 base! ;
: hex       ( -- )          16 base! ;


: @+        ( a -- a+1 n )  a! @a+ a swap ;
: 1+        ( n1 -- n2 )    1 + ;
: 1-        ( n1 -- n2 )    -1 + ;
: goodN     ( n1 -- | n1 )  1- -if  drop r> drop exit then 1+ ;
: goodAN    ( n1 n2 -- | n1 n2) 1- -if 2drop r> drop exit then 1+ ;
: type      ( a n -- )      goodAN for @+ emit next drop ;


( 2 chere 2* dasm bye )
