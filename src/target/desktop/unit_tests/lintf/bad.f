( lintf.py fixture: one problem of each kind. Text before the first block is skipped. )
( BLOCK 3 )
( Good rows ___________________________________________________ 10/11/26 BNE )
: \         ( -- )          >in @ 128 + -128 and >in ! ; immediate              \ comment to the next row
: (x)       ( n -- n )      ( a comment ) dup drop ;                            \ a name with parentheses
: twice     ( C: -- ) ( n -- 2n ) 2* ;                                          \ picture with two groups
: definitions ( -- )        context @ current ! ;                               \ a long name pushes the picture
: mk        ( <name> -- )   create , ;                                          \ a defining word
5 mk five                                                                       \ a header made by mk
: str       ( -- )          ." a ( b \ c" ;                                     \ parsed text is skipped
: long      ( -- )          0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 drop ; \ code may run past 79
-->
( BLOCK 4 )
( Bad title ___ 10/11/26 BNE )
: nodesc    ( -- )          ;
: nopic                     0 drop ;                                            \ no stack picture
: wrongcol  ( -- )          ;                                         \ description at 70
: cap       ( -- )          ;                                                   \ Capital start
: period    ( -- )          ;                                                   \ ends with a period.
: nodash    ( n )           ;                                                   \ picture without dashes
: tight     ( n --)         ;                                                   \ no space before the paren
: piccol  ( -- )            ;                                                   \ picture at column 10
: codecol   ( -- )      0 drop ;                                                \ code at column 24
variable a  variable b                                                          \ two names on one row
: trail     ( -- )          ;                                                   \ trailing spaces  
: tab       ( -- )          	;                                                  \ tab in the code
: big       ( -- )          ;                                                   \ xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
-->
( BLOCK IGNORE )
: ignored  ;
