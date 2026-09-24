#include <stdint.h>
#include "errcodes.h"
#include "forth.h"
#include "serial_io.h"
#include "vm.h"
#include "vm_labels.h"
#include "tools.h"
#include "utils.h"

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
    { ERR_INTERPRET_COMPILE_ONLY, "interpreting a compile-only word" },
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
    { ERR_TERM_TX_FAILED, "TTY transmission failed" },
    { ERR_TERM_RX_FAILED, "TTY reception failed" },
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
    { ERR_WRONG_NUM_RESULTS, "assertion - wrong number of results" },
    { ERR_INVALID_MEMORY_PAGE, "invalid memory page (>= `pages`)" },
    { ERR_TIB_OVERFLOW, "TIB filled up before EOL was seen" },
    { ERR_WID_OVERFLOW, "Wordlist allocation overflowed" },
    { ERR_TOO_MANY_BITS, "Only 1 to 32 bits are allowed in a bit field" },
    { ERR_INTERPRETATION_ONLY, "Word may not be compiled" },
};

/**
 * Returns the description string for a given error code.
 */
const char* get_error_message(int err_code) {
    if (err_code == 0) {
        return "No error";
    }

    int table_size = sizeof(error_table) / sizeof(error_table[0]);
    
    // Check if error fits standard array index bounds (-1 to -106)
    int index = (-err_code) - 1;
    if (index >= 0 && (int)index < table_size && error_table[index].code == err_code) {
        return error_table[index].msg;
    }

    // Fallback search for out-of-order codes
    for (int i = 0; i < table_size; i++) {
        if (error_table[i].code == err_code) {
            return error_table[i].msg;
        }
    }

    return "Unknown error code";
}

/*==========================================================================
* Extra API words
==========================================================================*/

/* `.` */
int vmAPI_dot(void) {
    return lfDot(vmPop());
}

/* `.S` */
int vmAPI_dotEss(void) {
    return lfDotS();
}

/*
 * Assertion tests for Forth words
 *
 * From the John Hayes test suite, for example:
 * T{ 0 0 AND -> 0 }T
 */

static uint8_t sp0;
static uint8_t expected_sp;
static int32_t expected_results[STACK_CAPACITY];

int lfAPI_beginTest(void) { // t{
    sp0 = vmPeek(VM_REG_sp);
    return 0;
}

int lfAPI_doTest(void) { // ->
    expected_sp = vmPeek(VM_REG_sp);
    int i = expected_sp;
    if (i >= STACK_MASK) return ERR_STACK_OVERFLOW;
    while (i--) {
        expected_results[i] = vmPeek(i);
    }
    vmPoke(VM_REG_sp, sp0);
    return 0;
}

int lfAPI_endTest(void) { // }t 
    if (expected_sp != vmPeek(VM_REG_sp)) {
        return ERR_WRONG_NUM_RESULTS;
    }
    int i = expected_sp;
    if (i >= STACK_MASK) return ERR_STACK_OVERFLOW;
    while (i--) {
        if (expected_results[i] != vmPeek(i)) {
            return ERR_WRONG_RESULTS;
        }
    }
    vmPoke(VM_REG_sp, sp0);
    vmPoke(0, VM_EMPTYSTACK);
    return 0;
}

int lfAPI_decimal(void) {
    lfBASEstore(10);
    return 0;
}

int lfAPI_hex(void) {
    lfBASEstore(16);
    return 0;
}

/**
 * Page allocation listing
 *
 * `.page` lists the current page's memory allocations
 * `.pages` lists every page's memory allocations
 */

int lfEmitses(int n, char c) {
    while (n--) serial_putc(c);
    return 0;
}

int lfSpaces(int n) {
    return lfEmitses(n, ' ');
}

static void dotFieldHex(int32_t n, int digits) {
    lfSpace();
    if (n) {
        lfDotB(n, 16, 0, digits);
    }
    else {
        while (digits--) serial_putc('-');
    }
}

static void dotPageHeader(void) {
    serial_puts("PAGE _ADDR_ __WP_ _SIZE _EXEC  _TYPE___\r\n");
}

static int APIdotPageX(int page) { // list specifics for the current page
    if (page >= VM_MEM_PAGES) return ERR_INVALID_MEMORY_PAGE;
    lfSpace();    lfDotB(page, 16, 0, 2);
    int32_t base = page << (22 - VM_LOG2_PAGES);
    lfSpaces(2);  lfDotB(base, 16, 0, 6);
    dotFieldHex(vm_memory_wp_limit[page], 5);
    dotFieldHex(vm_memory_rd_limit[page], 5);
    dotFieldHex(vm_memory_executable[page], 5);
    lfSpaces(2); serial_puts(vm_memory_name[page]);
    lfSpace();
    return 0;
}

int lfAPI_dotPage(void) {
    uint32_t current_page = vmPeek(-1);
    dotPageHeader();
    APIdotPageX(current_page);
    return 0;
}

int lfAPI_dotPages(void) {
    dotPageHeader();
    for (int i = 0; i < VM_MEM_PAGES; i++) {
        APIdotPageX(i);
        lfCR();
    }
    return 0;
}

/**
 * Forth DUMP implementation for 32-bit cell-addressed VM memory.
 * Displays memory in lines of 4 cells (16 bytes total).
 *
 * @param start_cell Base VM cell address to start dumping from.
 * @param cell_count Number of 32-bit cells to dump.
 * @return 0 on success, or non-zero ior error code from vmFetch.
 */
int lfAPI_dump(void) {
    int32_t length = vmPeek(-1);
    int32_t origin = vmPeek(-1);
    int tally = 0;

    while (tally < length) {
        uint32_t addr = origin + tally;

        // Print cell base address (hex formatted)
        lfDotB(addr, 16, 0, 6);
        serial_puts(": ");

        int cells_in_line = (length - tally < 4) ? (length - tally) : 4;
        int32_t line_data[4] = { 0 };

        // Fetch up to 4 cells for current line
        for (int i = 0; i < cells_in_line; i++) {
            int ior = vmFetch(addr + i, &line_data[i]);
            if (ior) return ior;
            lfDotB(line_data[i], 16, 0, 8);
            lfSpace();
        }

        // Align partial trailing lines
        for (int i = cells_in_line; i < 4; i++) {
            lfSpaces(9);
        }

        lfSpace();

        // Print ASCII equivalent (unpacking 32-bit cells byte-by-byte)
        for (int i = 0; i < cells_in_line; i++) {
            uint32_t cell_val = (uint32_t)line_data[i];
            for (int b = 0; b < 4; b++) {
                uint8_t byte = (cell_val >> (b * 8)) & 0xFF; // Little-endian byte extraction
                if ((byte < ' ') || (byte > 0x7F)) byte = '.';
                serial_putc(byte);
            }
        }

        int page = addr >> (22 - VM_LOG2_PAGES);
        uint32_t wplimit = vm_memory_wp_limit[page];
        if ((addr & VM_PAGE_MASK) <= wplimit) {
            serial_puts("  read-only");
        }

        lfCR();
        tally += cells_in_line;
    }

    return 0;
}

/*
 * Disassembler `dasm` ( addr len -- )
 * 
 * The address is a cell address. Instruction addresses are shown.
 * The instruction address is twice the cell address.
 * The length is the number of instructions.
 */

static const char* uopName[] = UOP_NAMES;
static const char* opName[] = OP_NAMES;
static const char* immName[] = IMM_NAMES; 

static int lfDotHex(int32_t n) {
    lfDotB(n, 16, 0, 0);
    return lfSpace();
}

static int DisassembleInsn(uint16_t inst) {
    static uint32_t lex;
    int32_t _lex = -1;
    lfDotB(inst, 16, 0, 4);
    lfSpace();
    if (inst & VM_UOPS) {
        int returning = inst & VM_RET;
        inst &= (VM_RET - 1);
        for (int i = SLOT0_POSITION; i > -5; i -= 5) {
            uint8_t slot;
            if (i < 0) slot = inst & LAST_SLOT_MASK;
            else slot = (inst >> i) & 0x1F;
            if (inst & ((1 << (i + 5)) - 1)) {
                serial_puts(uopName[slot]);
                lfSpace();
            }
        }
        if (returning) return serial_putc(';');
    }
    else {
        int opcode = (inst >> 13) & 3;
        int32_t immex = (lex << 13) | (inst & ((1 << 13) - 1));
        if (opcode < 3) { // call, jump, imm
            lfDotHex(immex);
            serial_puts(opName[opcode]);
        }
        else {
            uint32_t imm = inst & ((1 << 9) - 1);
            int simm = imm;
            if (simm & (1 << 8)) { // sign-extend
                simm |= ~((1 << 9) - 1);
            }
            lfDotHex(simm);
            opcode = (inst >> 9) & 0x0F;
            serial_puts(immName[opcode]);
            if (opcode == VMO_PFX) _lex = imm;
            if (opcode == VMO_API0) {
                // traverse dictionary looking for this api call name
            }
        }
    }
    if (_lex < 0) lex = 0;
    else lex = (lex << 9) | _lex;
    _lex = -1;
    return 0;
}

int lfAPI_dumpIns(void) {
    uint16_t inst = (uint16_t)vmPeek(-1);
    DisassembleInsn(inst);
    return 0;
}

int lfAPI_dasm(void) {
    int32_t length = vmPeek(-1);
    int32_t addr = vmPeek(-1);
    addr = (addr & 0x3FFFFF) << 1;
    while (length--) {
        lfDotB(addr, 16, 0, 6);
        lfSpace();
        int32_t inst = 0;
        int ior = vmFetch((addr >> 1), &inst);
        if (ior) return ior;
        if (addr & 1) inst >>= 16;
        DisassembleInsn(inst);
        lfCR();
        addr++;
    }
    return 0;
}
