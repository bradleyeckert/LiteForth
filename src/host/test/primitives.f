cr .( Testing Forth ) .( primitives )  1 >options ( validation mode )
cr

T{        0  5 + ->          5 }T
T{        5  0 + ->          5 }T
T{        0 -5 + ->         -5 }T
T{       -5  0 + ->         -5 }T
T{        1  2 + ->          3 }T
T{        1 -2 + ->         -1 }T
T{       -1  2 + ->          1 }T
T{       -1 -2 + ->         -3 }T
T{       -1  1 + ->          0 }T

T{    0 2*       ->    0 }T
T{ 4000 2*       -> 8000 }T
T{   -1 2*       ->   -2 }T

T{    0 2/       ->    0 }T
T{ 4000 2/       -> 2000 }T
T{   -4 2/       ->   -2 }T
