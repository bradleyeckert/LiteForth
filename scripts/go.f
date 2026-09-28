0 open-flash
: cells ; immediate

( I/O bindings )
: emit      _emit ;         ( c -- )
: key       _key ;          ( -- c )
: key?      _key? ;         ( -- flag )

( variables )
: variable  32 bits ;       ( -- )
: _data     0 dp^ ! ;       ( -- )
: _idata    1 dp^ ! ;       ( -- )
: _code     2 dp^ ! ;       ( -- )
: _text     3 dp^ ! ;       ( -- )
: unused    dp^ @ 2* dp[] + ; ( bad )

: foo over swap nip ;
: bar cells foo foo ;

: negate  1 swap inv + ;
