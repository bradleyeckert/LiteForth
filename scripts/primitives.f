cr .( Testing Forth primitives ) 7 >options ( validation mode )
cr  ( This file is intended to replace stdin on a console app. )

( --- Arithmetic & Bitwise Operations --- )

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

( 2* )
T{    0 2*       ->    0 }T
T{ 4000 2*       -> 8000 }T
T{   -1 2*       ->   -2 }T

( 2/ )
T{    0 2/       ->    0 }T
T{ 4000 2/       -> 2000 }T
T{   -4 2/       ->   -2 }T

( inv )
T{    0 inv      ->   -1 }T
T{   -1 inv      ->    0 }T

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

( um* )
T{ 0 0 um*       -> 0 0 }T
T{ 1 1 um*       -> 1 0 }T
T{ 2 3 um*       -> 6 0 }T
T{ -1 -1 um*     -> 1 -2 }T

( m* )
T{  0  0 m*      -> 0 0 }T
T{  1  1 m*      -> 1 0 }T
T{ -1  1 m*      -> -1 -1 }T


( --- Data Stack Operations --- )

( dup )
T{ 123 dup       -> 123 123 }T

( drop )
T{ 123 456 drop  -> 123 }T

( swap )
T{ 123 456 swap  -> 456 123 }T

( over )
T{ 123 456 over  -> 123 456 123 }T

( nip )
T{ 123 456 nip   -> 456 }T

( 2dup )
T{ 123 456 2dup  -> 123 456 123 456 }T


( --- Return Stack Operations --- )

( >r , r@ , r> )
T{ 99 >r 1 r@ 2 r>   -> 1 99 2 99 }T
T{ 42 >r 88 r>   -> 88 42 }T


( --- Memory Access Operations --- )

( A register memory access: !a, @a, @a+, !a+ )
T{ 1 'page 31 + a! 55 !a @a -> 55 }T
T{ 1 'page 32 + a! 10 !a+ 20 !a+ -> }T
T{ 1 'page 32 + a! @a+ @a -> 10 20 }T

( B register memory access: !b, @b, @b+, !b+ )
T{ 1 'page 33 + b! 77 !b @b -> 77 }T
T{ 1 'page 34 + b! 30 !b+ 40 !b+ -> }T
T{ 1 'page 34 + b! @b+ @b -> 30 40 }T

( ! and @ )
T{ 88 1 'page 35 + ! 1 'page 35 + @ -> 88 }T

( --- Register Transfer Operations --- )

( a and a! )
T{ 123 a! a      -> 123 }T

( b and b! )
T{ 456 b! b      -> 456 }T


( --- System Constants --- )

( base )
T{ base @        -> 10 }T

ram-page page !  ram-base 200 200 init-here udata

.( Tests completed successfully ) cr bye
