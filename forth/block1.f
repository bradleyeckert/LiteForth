( BLOCK 1 )
( LiteForth home screen _______________________________________ 10/17/26 BNE )
0 open-flash  7 load    \ build the language
50 load                 \ build the application
close-flash             \ commit to flash memory

.( Dictionary loaded and committed to flash memory.) cr
.( If you need Flash writable again, use `0 open-flash`.) cr .map
cr .( Type `cold` to reboot, `try` to launch the clock demo.) cr
