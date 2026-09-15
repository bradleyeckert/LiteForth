#include <stdint.h>
#include "errcodes.h"
#include "forth.h"
#include "vm.h"

typedef struct {
    int code;
    const char *msg;
} ErrorMapping;

static const ErrorMapping error_table[] = {
    /* ANS Forth-94 Standard Exception Codes */
    { ERR_ABORT, "ABORT" },
    { ERR_ABORT_QUOTE, "ABORT\"" },
    { ERR_STACK_OVERFLOW, "stack overflow" },
    { ERR_STACK_UNDERFLOW, "stack underflow" },
    { ERR_RSTACK_OVERFLOW, "return stack overflow" },
    { ERR_RSTACK_UNDERFLOW, "return stack underflow" },
    { ERR_DO_LOOPS_NESTED, "do-loops nested too deeply" },
    { ERR_DICTIONARY_OVERFLOW, "dictionary overflow" },
    { ERR_INVALID_ADDRESS, "invalid memory address" },
    { ERR_DIVISION_BY_ZERO, "division by zero" },
    { ERR_RESULT_OUT_OF_RANGE, "result out of range" },
    { ERR_ARGUMENT_TYPE_MISMATCH, "argument type mismatch" },
    { ERR_UNDEFINED_WORD, "undefined word" },
    { ERR_INTERPRET_ONLY, "interpreting a compile-only word" },
    { ERR_INVALID_FORGET, "invalid FORGET" },
    { ERR_ZERO_LENGTH_NAME, "attempt to use zero-length string as a name" },
    { ERR_PICTURED_NUM_OVERFLOW, "pictured numeric output string overflow" },
    { ERR_PARSED_STRING_OVERFLOW, "parsed string overflow" },
    { ERR_DEFINITION_TOO_LONG, "definition name too long" },
    { ERR_WRITE_PROTECTED, "write address prohibited" },
    { ERR_UNSUPPORTED_OPERATION, "unsupported operation" },
    { ERR_CONTROL_STRUCTURE, "control structure mismatch" },
    { ERR_ADDRESS_ALIGNMENT, "address alignment exception" },
    { ERR_INVALID_NUMERIC, "invalid numeric argument" },
    { ERR_RETURN_STACK_IMBALANCE, "return stack imbalance" },
    { ERR_LOOP_PARAMETERS_INVALID, "loop parameters unavailable" },
    { ERR_INVALID_COMPUTE_PAD, "invalid recursion / PAD overflow" },
    { ERR_USER_INTERRUPT, "user interrupt / break" },
    { ERR_COMPILER_NESTING, "compiler nesting error" },
    { ERR_OBSOLETE_FEATURE, "obsolete feature" },
    { ERR_BODY_ON_NON_CREATE, ">BODY used on non-CREATEd definition" },
    { ERR_INVALID_NAME_ARGUMENT, "invalid name argument (e.g. TO name)" },
    { ERR_BLOCK_READ_ERROR, "block read exception" },
    { ERR_BLOCK_WRITE_ERROR, "block write exception" },
    { ERR_INVALID_BLOCK_NUMBER, "invalid block number" },
    { ERR_INVALID_FILE_POSITION, "invalid file position" },
    { ERR_FILE_IO_ERROR, "file I/O exception" },
    { ERR_FILE_NOT_FOUND, "non-existent file" },
    { ERR_UNEXPECTED_EOF, "unexpected end of file" },
    { ERR_INVALID_BASE, "invalid BASE" },
    { ERR_LOSS_OF_PRECISION, "loss of precision" },
    { ERR_FP_DIVIDE_BY_ZERO, "floating-point divide by zero" },
    { ERR_FP_RESULT_OUT_OF_RANGE, "floating-point result out of range" },
    { ERR_FP_STACK_OVERFLOW, "floating-point stack overflow" },
    { ERR_FP_STACK_UNDERFLOW, "floating-point stack underflow" },
    { ERR_FP_INVALID_ARGUMENT, "floating-point invalid argument" },
    { ERR_COMPILATION_ONLY, "compilation-only word" },
    { ERR_INVALID_POSTPONE, "invalid POSTPONE" },
    { ERR_SEARCH_ORDER_OVERFLOW, "search-order overflow" },
    { ERR_SEARCH_ORDER_UNDERFLOW, "search-order underflow" },
    { ERR_COMPILATION_PASS_ERROR, "compilation monitor error" },
    { ERR_CONTROL_STRUCTURE_OVER, "control-structure stack overflow" },
    { ERR_EXCEPTION_STACK_OVERFLOW, "exception stack overflow" },
    { ERR_FLOATING_POINT_UNAVAIL, "floating-point exception" },
    { ERR_FP_UNIDENTIFIED_FAULT, "floating-point unknown error" },
    { ERR_QUIT, "QUIT" },
    { ERR_EXCEPTION_IN_CHAR, "exception in sending or receiving a character" },
    { ERR_IF_NOT_FINISHED, "[IF], [ELSE], or [THEN] exception" },

    /* Forth-2012 Standard Exception Codes */
    { ERR_ALLOCATE_FAILED, "ALLOCATE failed" },
    { ERR_FREE_FAILED, "FREE failed" },
    { ERR_RERESIZE_FAILED, "RESIZE failed" },
    { ERR_CLOSE_FILE_FAILED, "CLOSE-FILE failed" },
    { ERR_CREATE_FILE_FAILED, "CREATE-FILE failed" },
    { ERR_DELETE_FILE_FAILED, "DELETE-FILE failed" },
    { ERR_FILE_POSITION_FAILED, "FILE-POSITION failed" },
    { ERR_FILE_SIZE_FAILED, "FILE-SIZE failed" },
    { ERR_INCLUDE_FILE_FAILED, "INCLUDE-FILE failed" },
    { ERR_LINE_READ_FAILED, "READ-LINE failed" },
    { ERR_FILE_READ_FAILED, "READ-FILE failed" },
    { ERR_RENAME_FILE_FAILED, "RENAME-FILE failed" },
    { ERR_SET_FILE_POS_FAILED, "REPOSITION-FILE failed" },
    { ERR_RESIZE_FILE_FAILED, "RESIZE-FILE failed" },
    { ERR_FLUSH_FILE_FAILED, "FLUSH-FILE failed" },
    { ERR_WRITE_FILE_FAILED, "WRITE-FILE failed" },
    { ERR_WRITE_LINE_FAILED, "WRITE-LINE failed" },
    { ERR_MALFORMED_UTF8, "malformed UTF-8 string" },
    { ERR_INVALID_NAME, "System definition name error" },
    { ERR_CALLBACK_FAILED, "CALLBACK failed" },
    { ERR_RESERVED_FORTH2012, "ERASE error / invalid memory region" },

    /* LiteForth-specific Exception Codes */
    { ERR_INVALID_HANDLE, "The active port handle is invalid or uninitialized" },
    { ERR_TERM_NOT_A_TTY, "Device is not an interactive terminal" },
    { ERR_PORT_OPEN_FAILED, "OS failed to open the serial port" },
    { ERR_GET_TERM_FAILED, "Failed to read port/terminal capabilities" },
    { ERR_SET_TERM_FAILED, "Failed to write new configurations" },
    { ERR_IO_CHECK_FAILED, "Driver query or select() polling failed" },
    { ERR_TERM_TX_FAILED, "Driver query or select() polling failed" },
    { ERR_BLK_CREATE_FAIL, "Failed to create the binary simulation file" },
    { ERR_BLK_WRITE_INIT, "Failed to format initial blank file template" },
    { ERR_BLK_OPEN_FAIL, "Failed to open the mass storage file" },
    { ERR_BLK_PARSE_FAIL, "Block 0 header missing signature or parsing failed" },
    { ERR_BLK_BOUNDS, "Target block index exceeds allocated file capacity" },
    { ERR_BLK_SEEK_FAIL, "Failed to seek to target block offset" },
    { ERR_BLK_READ_FAIL, "Disk read failed to yield full 4KB payload" },
    { ERR_BLK_WRITE_FAIL, "Disk write failed to persist full 4KB payload" },
    { ERR_BLK_WRITE_PROTECTED, "Attempted write onto a write-protected block range" },
    { ERR_FLASH_CREATE_FAIL, "Failed to create the binary simulation file" },
    { ERR_FLASH_WRITE_INIT, "Failed to write the full initial blank state to disk" },
    { ERR_FLASH_INVALID_SECTOR, "The requested sector index falls outside valid bounds" },
    { ERR_FLASH_OPEN_WRITE, "Failed to open the simulation file for writing/updating" },
    { ERR_FLASH_SEEK_FAIL, "Failed to seek to the start offset of the requested sector" },
    { ERR_FLASH_WRITE_SECTOR, "Failed to write the complete block data to the sector slot" },
    { ERR_EXEC_PROTECTED, "execution address prohibited" },
    { ERR_INVALID_API_CALL, "invalid API call" },
    { ERR_INVALID_OPCODE, "invalid VM opcode" },
    { ERR_WRONG_RESULTS, "assertion - wrong results" },
    { ERR_WRONG_NUM_RESULTS, "assertion - wrong number of results" }
};

/**
 * Returns the description string for a given error code.
 */
const char* get_error_message(int err_code) {
    if (err_code == 0) {
        return "No error";
    }

    size_t table_size = sizeof(error_table) / sizeof(error_table[0]);
    
    // Check if error fits standard array index bounds (-1 to -106)
    int index = (-err_code) - 1;
    if (index >= 0 && (size_t)index < table_size && error_table[index].code == err_code) {
        return error_table[index].msg;
    }

    // Fallback search for out-of-order codes
    for (size_t i = 0; i < table_size; i++) {
        if (error_table[i].code == err_code) {
            return error_table[i].msg;
        }
    }

    return "Unknown error code";
}

/*==========================================================================
* Extra API words
==========================================================================*/

int lfAPIdecimal(void) {
    BASE = 10;
    return 0;
}

int lfAPIhex(void) {
    BASE = 16;
    return 0;
}

