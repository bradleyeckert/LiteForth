( BLOCK 30 )
( Demo ________________________________________________________ 10/12/26 BNE )
: hi        ( -- )          ." 学如不及，犹恐失之 " ;                           \ demo CJK message
: value     ( x <name> -- ) _idata align create , does> @ ;                     \ create name, which returns x; see to
: to        ( x <name> -- ) ' >body ! ;                                         \ change the x that value name returns
.( value test: ) 11 value ten   ten .  10 to ten  ten . cr                      \ a test value

variable counter                                                                \ bumped by multitasker
variable lastus                                                                 \ capusec time of the last demo-step
variable looptime                                                               \ microseconds per trip around the pause loop
: demo-step ( -- )          1 counter a! @a + dup !a                            \ application loop: count it, time it
    1023 and 0= if
        capusec x@  dup lastus @ -  looptime !  lastus ! 
    then ;

20 32 32 task t1                                                                \ sample tasks 1
10 20 20 task t2                                                                \ unused sample task

variable ctr                                                                    \ starts incrementing after you type `try`
: try       ( -- )          t1 activate  begin pause  1 ctr +!  again ;         \ give t1 something to do

:noname ( demo application )
    init-idata
    multitask  hi
    begin demo-step pause break again
; hex 80000000 ,jump decimal

:noname ( yeet handler: y@ = error code, x@ = PC after the fault )
    cr ." App error " y@ .  ." at " x@ hex . decimal cr
    begin  break  again         ( park the app until the next `cold` )
; hex 80000002 ,jump decimal
-->
( BLOCK++ )
( Resolve startup _____________________________________________ 10/12/26 BNE )

_text here 32 bit  1 !  ( Boot structure at end of text )
save-wids save-idata
_udata  ( leave data space selected: `variable` in text space corrupts headers )

( Cell 0: 1 or 2 instructions for jump to application )
( Cell 1: address of system initialization data {TBD} )
( Cell 2: 1 or 2 instructions for jump to yeet handler )
