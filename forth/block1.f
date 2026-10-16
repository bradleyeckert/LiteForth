( BLOCK 1 )
( LiteForth home screen _______________________________________ 10/16/26 BNE )
0 open-flash  7 load    \ load the lexicon
50 load                 \ build the application image
close-flash           \ commit to flash memory

.( Dictionary loaded and committed to flash memory.) cr
.( If you need Flash writable again, use `0 open-flash` ) cr .map
