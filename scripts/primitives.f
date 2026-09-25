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

( shifts )

T{ 3 SHFT[  5 ]SHL -> 40 }T
T{ 4 SHFT[ -1 ]SHL -> -16 }T
T{ 3 SHFT[ 44 ]SHR -> 5 }T
T{ 30 SHFT[ -1 ]SHR -> 3 }T

( inv )
T{    0 invert   ->   -1 }T
T{   -1 invert   ->    0 }T

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
T{ ram-base 31 + a! 55 !a @a -> 55 }T
T{ ram-base 32 + a! 10 !a+ 20 !a+ -> }T
T{ ram-base 32 + a! @a+ @a -> 10 20 }T

( B register memory access: !b, @b, @b+, !b+ )
T{ ram-base 33 + b! 77 !b @b -> 77 }T
T{ ram-base 34 + b! 30 !b+ 40 !b+ -> }T
T{ ram-base 34 + b! @b+ @b -> 30 40 }T

( ! and @ )
T{ 88 ram-base 35 + ! ram-base 35 + @ -> 88 }T

HEX
T{ 84000001 slice+ -> 80000002 }T
T{ 80000002 slice+ -> 84000002 }T
DECIMAL

( --- Register Transfer Operations --- )

( a and a! )
T{ 123 a! a      -> 123 }T

( b and b! )
T{ 456 b! b      -> 456 }T

( --- System Constants --- )

( base )
T{ base @  HEX      -> 0A }T
T{ base @  DECIMAL  -> 16 }T

( options> and >options )
T{ options> 7 >options options> -> 7 7 }T

( constant )
1234 constant TEST_CONST
T{ TEST_CONST -> 1234 }T

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

( Definitions )

( : / ; definition execution )
: ADD_TEN 10 + ;
T{ 5 ADD_TEN -> 15 }T

( exit in colon word )
: TEST_EXIT 123 exit 456 ;
T{ TEST_EXIT -> 123 }T

.( Tests completed successfully ) cr
 bye )
0 >options