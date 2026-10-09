( BLOCK 1 )
( LiteForth home screen _______________________________________ 10/10/26 BNE )
0 open-flash  2 load    \ load the lexicon
20 load                 \ build the application image
\ close-flash           \ commit to flash memory

.( Dictionary loaded but not committed to flash memory.) cr
.( `close-flash` will re-flash.) cr .map
