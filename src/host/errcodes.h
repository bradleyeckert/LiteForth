#ifndef ERRCODES_H
#define ERRCODES_H

/*
 * ANS Forth-94 Standard Exception Codes (Table 9.1)
 */
#define ERR_ABORT                    (-1)   /* ABORT */
#define ERR_ABORT_QUOTE              (-2)   /* ABORT" */
#define ERR_STACK_OVERFLOW           (-3)   /* stack overflow */
#define ERR_STACK_UNDERFLOW          (-4)   /* stack underflow */
#define ERR_RSTACK_OVERFLOW          (-5)   /* return stack overflow */
#define ERR_RSTACK_UNDERFLOW         (-6)   /* return stack underflow */
#define ERR_DO_LOOPS_NESTED          (-7)   /* do-loops nested too deeply */
#define ERR_DICTIONARY_OVERFLOW      (-8)   /* dictionary overflow */
#define ERR_INVALID_ADDRESS          (-9)   /* invalid memory address */
#define ERR_DIVISION_BY_ZERO        (-10)   /* division by zero */
#define ERR_RESULT_OUT_OF_RANGE     (-11)   /* result out of range */
#define ERR_ARGUMENT_TYPE_MISMATCH  (-12)   /* argument type mismatch */
#define ERR_UNDEFINED_WORD          (-13)   /* undefined word */
#define ERR_INTERPRET_ONLY          (-14)   /* interpreting a compile-only word */
#define ERR_INVALID_FORGET          (-15)   /* invalid FORGET */
#define ERR_ZERO_LENGTH_NAME        (-16)   /* attempt to use zero-length string as a name */
#define ERR_PICTURED_NUM_OVERFLOW   (-17)   /* pictured numeric output string overflow */
#define ERR_PARSED_STRING_OVERFLOW  (-18)   /* parsed string overflow */
#define ERR_DEFINITION_TOO_LONG     (-19)   /* definition name too long */
#define ERR_WRITE_PROTECTED         (-20)   /* write address prohibited */
#define ERR_UNSUPPORTED_OPERATION   (-21)   /* unsupported operation */
#define ERR_CONTROL_STRUCTURE       (-22)   /* control structure mismatch */
#define ERR_ADDRESS_ALIGNMENT       (-23)   /* address alignment exception */
#define ERR_INVALID_NUMERIC         (-24)   /* invalid numeric argument */
#define ERR_RETURN_STACK_IMBALANCE  (-25)   /* return stack imbalance */
#define ERR_LOOP_PARAMETERS_INVALID (-26)   /* loop parameters unavailable */
#define ERR_INVALID_COMPUTE_PAD     (-27)   /* invalid recursion / PAD overflow */
#define ERR_USER_INTERRUPT          (-28)   /* user interrupt / break */
#define ERR_COMPILER_NESTING        (-29)   /* compiler nesting error */
#define ERR_OBSOLETE_FEATURE        (-30)   /* obsolete feature */
#define ERR_BODY_ON_NON_CREATE      (-31)   /* >BODY used on non-CREATEd definition */
#define ERR_INVALID_NAME_ARGUMENT   (-32)   /* invalid name argument (e.g. TO name) */
#define ERR_BLOCK_READ_ERROR        (-33)   /* block read exception */
#define ERR_BLOCK_WRITE_ERROR       (-34)   /* block write exception */
#define ERR_INVALID_BLOCK_NUMBER    (-35)   /* invalid block number */
#define ERR_INVALID_FILE_POSITION   (-36)   /* invalid file position */
#define ERR_FILE_IO_ERROR           (-37)   /* file I/O exception */
#define ERR_FILE_NOT_FOUND          (-38)   /* non-existent file */

/* Extended ANS Forth-94 Standard Exception Codes (-39 to -58) */
#define ERR_UNEXPECTED_EOF          (-39)   /* unexpected end of file */
#define ERR_INVALID_BASE            (-40)   /* invalid BASE */
#define ERR_LOSS_OF_PRECISION       (-41)   /* loss of precision */
#define ERR_FP_DIVIDE_BY_ZERO       (-42)   /* floating-point divide by zero */
#define ERR_FP_RESULT_OUT_OF_RANGE  (-43)   /* floating-point result out of range */
#define ERR_FP_STACK_OVERFLOW       (-44)   /* floating-point stack overflow */
#define ERR_FP_STACK_UNDERFLOW      (-45)   /* floating-point stack underflow */
#define ERR_FP_INVALID_ARGUMENT     (-46)   /* floating-point invalid argument */
#define ERR_COMPILATION_ONLY        (-47)   /* compilation-only word */
#define ERR_INVALID_POSTPONE        (-48)   /* invalid POSTPONE */
#define ERR_SEARCH_ORDER_OVERFLOW   (-49)   /* search-order overflow */
#define ERR_SEARCH_ORDER_UNDERFLOW  (-50)   /* search-order underflow */
#define ERR_COMPILATION_PASS_ERROR  (-51)   /* compilation monitor error */
#define ERR_CONTROL_STRUCTURE_OVER  (-52)   /* control-structure stack overflow */
#define ERR_EXCEPTION_STACK_OVERFLOW (-53)  /* exception stack overflow */
#define ERR_FLOATING_POINT_UNAVAIL  (-54)   /* floating-point exception */
#define ERR_FP_UNIDENTIFIED_FAULT   (-55)   /* floating-point unknown error */
#define ERR_QUIT                    (-56)   /* QUIT */
#define ERR_EXCEPTION_IN_CHAR       (-57)   /* exception in sending or receiving a character */
#define ERR_IF_NOT_FINISHED         (-58)   /* [IF], [ELSE], or [THEN] exception */

/*
 * Forth-2012 Standard Exception Codes (Table 9.1 Additions)
 */
#define ERR_ALLOCATE_FAILED         (-59)   /* ALLOCATE failed */
#define ERR_FREE_FAILED             (-60)   /* FREE failed */
#define ERR_RERESIZE_FAILED         (-61)   /* RESIZE failed */
#define ERR_CLOSE_FILE_FAILED       (-62)   /* CLOSE-FILE failed */
#define ERR_CREATE_FILE_FAILED      (-63)   /* CREATE-FILE failed */
#define ERR_DELETE_FILE_FAILED      (-64)   /* DELETE-FILE failed */
#define ERR_FILE_POSITION_FAILED    (-65)   /* FILE-POSITION failed */
#define ERR_FILE_SIZE_FAILED        (-66)   /* FILE-SIZE failed */
#define ERR_INCLUDE_FILE_FAILED     (-67)   /* INCLUDE-FILE failed */
#define ERR_LINE_READ_FAILED        (-68)   /* READ-LINE failed */
#define ERR_FILE_READ_FAILED        (-69)   /* READ-FILE failed */
#define ERR_RENAME_FILE_FAILED      (-70)   /* RENAME-FILE failed */
#define ERR_SET_FILE_POS_FAILED     (-71)   /* REPOSITION-FILE failed */
#define ERR_RESIZE_FILE_FAILED      (-72)   /* RESIZE-FILE failed */
#define ERR_FLUSH_FILE_FAILED       (-73)   /* FLUSH-FILE failed */
#define ERR_WRITE_FILE_FAILED       (-74)   /* WRITE-FILE failed */
#define ERR_WRITE_LINE_FAILED       (-75)   /* WRITE-LINE failed */
#define ERR_MALFORMED_UTF8          (-76)   /* malformed UTF-8 string */
#define ERR_INVALID_NAME            (-77)   /* System definition name error */
#define ERR_CALLBACK_FAILED         (-78)   /* CALLBACK failed */
#define ERR_RESERVED_FORTH2012      (-79)   /* ERASE error / invalid memory region */

/*
 * LiteForth-specific Exception Codes
 */
// serial_io.c
#define ERR_INVALID_HANDLE         (-100)   /* The active port handle is invalid or uninitialized */
#define ERR_TERM_NOT_A_TTY         (-101)   /* Device is not an interactive terminal */
#define ERR_PORT_OPEN_FAILED       (-102)   /* OS failed to open the serial port */
#define ERR_GET_TERM_FAILED        (-103)   /* Failed to read port/terminal capabilities */
#define ERR_SET_TERM_FAILED        (-104)   /* Failed to write new configurations */
#define ERR_IO_CHECK_FAILED        (-105)   /* Driver query or select() polling failed */
// block.c
#define ERR_BLK_CREATE_FAIL        (-110)   /* Failed to create the binary simulation file */
#define ERR_BLK_WRITE_INIT         (-111)   /* Failed to format initial blank file template */
#define ERR_BLK_OPEN_FAIL          (-112)   /* Failed to open the mass storage file */
#define ERR_BLK_PARSE_FAIL         (-113)   /* Block 0 header missing signature or parsing failed */
#define ERR_BLK_BOUNDS             (-114)   /* Target block index exceeds allocated file capacity */
#define ERR_BLK_SEEK_FAIL          (-115)   /* Failed to seek to target block offset */
#define ERR_BLK_READ_FAIL          (-116)   /* Disk read failed to yield full 4KB payload */
#define ERR_BLK_WRITE_FAIL         (-117)   /* Disk write failed to persist full 4KB payload */
#define ERR_BLK_WRITE_PROTECTED    (-118)   /* Attempted write onto a write-protected block range */
// flash.c                             
#define ERR_FLASH_CREATE_FAIL      (-120)   /* Failed to create the binary simulation file */
#define ERR_FLASH_WRITE_INIT       (-121)   /* Failed to write the full initial blank state to disk */
#define ERR_FLASH_INVALID_SECTOR   (-122)   /* The requested sector index falls outside valid bounds */
#define ERR_FLASH_OPEN_WRITE       (-123)   /* Failed to open the simulation file for writing/updating */
#define ERR_FLASH_SEEK_FAIL        (-124)   /* Failed to seek to the start offset of the requested sector */
#define ERR_FLASH_WRITE_SECTOR     (-125)   /* Failed to write the complete block data to the sector slot */

#endif // ERRCODES_H
