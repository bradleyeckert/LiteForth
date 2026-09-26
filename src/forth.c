#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "comp.h"
#include "tools.h"
#include "lfblocks.h"
#include "options.h"

// wsl: // cd /mnt/c/Users/User/Documents/GitHub/LiteForth

#if (FAT_FORTH & 1)
#include "utils.h"
#endif

static int case_insensitive = CASE_INSENSITIVE;

static int lfTOINfetch(void) {
    int32_t result;
    vmFetch(LF_TOIN, &result);
    return result;
}

static int lfTOINstore(int position) {
    return vmStore(LF_TOIN, position);
}

static int lfTIBSTATEfetch(void) {
    int32_t result;
    vmFetch(LF_TIBSTATE, &result);
    return result;
}

static int lfTIBSTATEstore(int state) {
    return vmStore(LF_TIBSTATE, state);
}

/*=========================================================================
* Define a Forth
=========================================================================*/

#define UOP(val) (W_PRIMITIVE | VM_UOPS | ((val) << SLOT0_POSITION) )
#define MACRO(s0, s1, s2) \
    (W_PRIMITIVE | W_MACRO | VM_UOPS | \
    ((s0) << SLOT0_POSITION) | ((s1) << LAST_SLOT_WIDTH)| (s2) )
#define DATA(idx) ((RAM_PAGE << (22 - VM_LOG2_PAGES)) + (idx))
#define API0(idx) (W_PRIMITIVE | VMI_API0 | (idx))
#define SYS(idx) (W_PRIMITIVE | VMI_SYS | (idx))
#define SYSTO(idx) (W_PRIMITIVE | VMI_TOSYS | (idx))
#define SYSFM(idx) (W_PRIMITIVE | VMI_FROMSYS | (idx))

#define LINKO(val) (struct s_head*)&only_heads[(val)]

static const struct s_head only_heads[] = {
    { NULL,     "bye",      API0(0), 0},
    { LINKO(0), "words",    API0(1), 0},
    { LINKO(1), "forth",    API0(2), 0},
    { LINKO(2), "only",     API0(3), 0},
};

#define LINK(val) (struct s_head*)&forth_heads[(val)]

// Flash cost per entry: 16 bytes plus name string (length+1 bytes).
// 64 entries is about 1.4 KB
static const struct s_head forth_heads[] = {  
    { LINKO(3), "invert",       UOP(VMU_INV),                             0},
    { LINK( 0), "inv",          UOP(VMU_INV),                             0},
    { LINK( 1), "over",         UOP(VMU_OVER),                            0},
    { LINK( 2), "a!",           UOP(VMU_ASTORE),                          0},
    { LINK( 3), "xor",          UOP(VMU_XOR),                             0},
    { LINK( 4), "+",            UOP(VMU_PLUS),                            0},
    { LINK( 5), "and",          UOP(VMU_AND),                             0},
    { LINK( 6), ">r",           UOP(VMU_PUSH),                            0},
    { LINK( 7), "unext",        UOP(VMU_UNEXT),                           0},
    { LINK( 8), "2*",           UOP(VMU_TWOSTAR),                         0},
    { LINK( 9), "dup",          UOP(VMU_DUP),                             0},
    { LINK(10), "drop",         UOP(VMU_DROP),                            0},
    { LINK(11), "@a",           UOP(VMU_FETCHA),                          0},
    { LINK(12), "@a+",          UOP(VMU_FETCHAPLUS),                      0},
    { LINK(13), "@as",          UOP(VMU_FETCHASIGN),                      0},
    { LINK(14), "r@",           UOP(VMU_R),                               0},
    { LINK(15), "r>",           UOP(VMU_POP),                             0},
    { LINK(16), "2/c",          UOP(VMU_TWODIVC),                         0},
    { LINK(17), "2/",           UOP(VMU_TWODIV),                          0},
    { LINK(18), "!a",           UOP(VMU_STOREA),                          0},
    { LINK(19), "!a+",          UOP(VMU_STOREAPLUS),                      0},
    { LINK(20), "!b",           UOP(VMU_STOREB),                          0},
    { LINK(21), "!b+",          UOP(VMU_STOREBPLUS),                      0},
    { LINK(22), "swap",         UOP(VMU_SWAP),                            0},
    { LINK(23), "+*",           UOP(VMU_PLUSSTAR),                        0},
    { LINK(24), "b",            UOP(VMU_B),                               0},
    { LINK(25), "b!",           UOP(VMU_BSTORE),                          0},
    { LINK(26), "@b",           UOP(VMU_FETCHB),                          0},
    { LINK(27), "@b+",          UOP(VMU_FETCHBPLUS),                      0},
    { LINK(28), "a",            UOP(VMU_A),                               0},
    { LINK(29), "cy",           UOP(VMU_CY),                              0},
    { LINK(30), "cells",        0,                            A_NOTHING | 0},
    { LINK(31), "2dup",         MACRO(VMU_OVER,VMU_OVER,VMU_NOP),         0},
    { LINK(32), "!",            MACRO(VMU_ASTORE,VMU_STOREA,VMU_NOP),     0},
    { LINK(33), "@",            MACRO(VMU_ASTORE,VMU_FETCHA,VMU_NOP),     0},
    { LINK(34), "s@",           MACRO(VMU_ASTORE,VMU_FETCHASIGN,VMU_NOP), 0},
    { LINK(35), "nip",          MACRO(VMU_SWAP,VMU_DROP,VMU_NOP),         0},
    { LINK(36), "tuck",         MACRO(VMU_SWAP,VMU_OVER,VMU_NOP),         0},
    { LINK(37), "slice+",       SYS(VMS_FIELDPLUS),  /* a1 -- a2      */  0},
    { LINK(38), "]shr",         SYS(VMS_SHR),        /* u1 -- u2      */  0},
    { LINK(39), "]shl",         SYS(VMS_SHL),        /* u1 -- u2      */  0},
    { LINK(40), "shft[",        SYSTO(VMSTO_SHIFT),  /* position --   */  0},
    { LINK(41), "]task",        SYSTO(VMSTO_TASK),   /* tstate --     */  0},
    { LINK(42), "yeet",         SYSTO(VMSTO_YEET),   /* ior --        */  0},
    { LINK(43), "task[",        SYSFM(VMSFROM_TASK), /* -- tstate     */  0},
    { LINK(44), "um*",          API0( 4), /* u1 u2 -- ud              */  0},
    { LINK(45), "m*",           API0( 5), /* n1 n2 -- d               */  0},
    { LINK(46), "mu/mod",       API0( 6), /* ud u -- rem dquot        */  0},
    { LINK(47), "*/mod",        API0( 7), /* n1 n2 -- rem quot        */  0},
    { LINK(48), "key?",         API0( 8), /* -- flag                  */  0},
    { LINK(49), "key",          API0( 9), /* -- c                     */  0},
    { LINK(50), "emit",         API0(10), /* c --                     */  0},
    { LINK(51), ":",            API0(11), /* <name> --                */  0},
    { LINK(52), ";",            API0(12),                   A_IMMEDIATE | 0},
    { LINK(53), ">options",     API0(13), /* n --                     */  0},
    { LINK(54), "options>",     API0(14), /* -- n                     */  0},
    { LINK(55), "(",            API0(15), /* -- */          A_IMMEDIATE | 0},
    { LINK(56), ".(",           API0(16), /* --                       */  0},
    { LINK(57), "does>",        API0(17), /* --                       */  0},
    { LINK(58), "create",       API0(18), /* -- | -- addr             */  0},
    { LINK(59), "cr",           API0(19), /* --                       */  0},
    { LINK(60), "here",         API0(20), /* -- addr                  */  0},
    { LINK(61), ".wid",         API0(21), /* wid --                   */  0},
    { LINK(62), "p'",           API0(22), /* <name> -- w aux          */  0},
    { LINK(63), "page",         API0(23), /* page -- a                */  0},
    { LINK(64), "wordlist",     API0(24), /* -- wid                   */  0},
    { LINK(65), "flash-open",   API0(25), /* addr --                  */  0},
    { LINK(66), "flash-close",  API0(26), /* --                       */  0},
    { LINK(67), "]",            API0(27), /* --                       */  0},
    { LINK(68), "[",            API0(28), /* -- */          A_IMMEDIATE | 0},
    { LINK(69), "exit",         API0(29), /* -- */          A_IMMEDIATE | 0},
    { LINK(70), "constant",     API0(30), /* n <name> --              */  0},
    { LINK(71), "bits",         API0(31), /* n <name> --              */  0},
    { LINK(72), ">body",        API0(32), /* xt -- addr               */  0},
    { LINK(73), ",",            API0(33), /* n --                     */  0},
    { LINK(74), "bit",          API0(34), /* n --                     */  0},
    { LINK(75), ",inst",        API0(35), /* inst --                  */  0},
    { LINK(76), "immediate",    API0(36), /* --                       */  0},
    { LINK(77), "block",        API0(37), /* u -- addr                */  0},
    { LINK(78), "buffer",       API0(38), /* u -- addr                */  0},
    { LINK(79), "update",       API0(39), /* --                       */  0},
    { LINK(80), "save-buffers", API0(40), /* --                       */  0},
    { LINK(81), "flush",        API0(41), /* --                       */  0},
    { LINK(82), "empty-buffers",API0(42), /* --                       */  0},
    { LINK(83), "load",         API0(43), /* u --                     */  0},
    { LINK(84), "capacity",     API0(44), /* -- u                     */  0},
    { LINK(85), "-->",          API0(45), /* --                       */  0},
#if (FAT_FORTH & 1)                                                     
    { LINK(86), "}t",           API0(46), /* ? --                     */  0},
    { LINK(87), "->",           API0(47), /* ? --                     */  0},
    { LINK(88), "t{",           API0(48), /* --                       */  0},
    { LINK(89), "hex",          API0(49), /* --                       */  0},
    { LINK(90), "decimal",      API0(50), /* --                       */  0},
    { LINK(91), ".page",        API0(51), /* n --                     */  0},
    { LINK(92), ".pages",       API0(52), /* --                       */  0},
    { LINK(93), "dump",         API0(53), /* addr length --           */  0},
    { LINK(94), "dumpi",        API0(54), /* inst --                  */  0},
    { LINK(95), "dasm",         API0(55), /* addr length --           */  0},
    { LINK(96), ".s",           API0(56), /* --                       */  0},
    { LINK(97), ".",            API0(57), /* n --                     */  0},
    { LINK(98), "see",          API0(58), /* <name> --                */  0 },
#endif
};

static const ConstantMapping constant_table[] = {
    { -1,               "true"},
    { 0,                "false"},
    { LF_STATE,         "state"},
    { LF_BASE,          "base"},
    { LF_CURRENT,       "current"},
    { LF_CONTEXT,       "context"},
    { CONTEXT_MAX,      "|context|"},
    { LF_DPL,           "dpl"},
    { LF_TOIN,          ">in"},
    { VARIABLE(F_BLK),  "blk"},
    { BLOCK_SIZE_CELLS, "|block|"},
//  { SYSTEM_BLOCKS,    "system-block-buffers"},
//  { LF_BLOCKBUFS,     "raw-blocks"},
    { LF_TIB,           "tib"},
    { VARIABLE(F_PTRS), "dp[]"},
    { LF_MSPACE,        "dp^" },
    { VM_LOG2_PAGES,    "log2pages"},
    { VARIABLE(F_HERE0),"ram-base"},
    { VMI_CALL,         "_call"},
    { VMI_JUMP,         "_jump"},
    { VMI_LIT,          "_lit"},
    { VMI_ZBRAN,        "_0bran"},
    { VMI_BRAN,         "_bran"},
    { VMI_PBRAN,        "_pbran"},
    { VMI_RCALL,        "_rcall"},
    { VMI_NEXT,         "_next"},
    { VMI_PFX,          "_pfx"},
    { VMI_USER,         "_user"},
    { VMI_QLIT,         "_qlit"},
    { VMI_API0,         "_api0"},
    { VMI_API1,         "_api1"},
    { W_PRIMITIVE,      "w_primitive"},
    { W_MACRO,          "w_macro"},
    { A_SMUDGED,        "a_smudged"},
    { A_IMMEDIATE,      "a_immediate"},
    { A_CONSTANT,       "a_constant"}
};

static int TheStringsMatch(char* s1, char* s2) {
    char c1;
    char c2;
    while (1) {
        c1 = *s1++;
        c2 = *s2++;
        if (c1 == '\0') break;
        if (c2 == '\0') break;
        if (case_insensitive) {
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
            if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        }
        if (c1 != c2) break;
    }
    return ((c1 | c2) == 0);
}

static int findConstant(char* name, int32_t* val) {
    int table_size = sizeof(constant_table) / sizeof(constant_table[0]);

    for (int i = 0; i < table_size; i++) {
        if (TheStringsMatch(name, (char*)constant_table[i].name)) {
            *val = constant_table[i].value;
            return 0;
        }
    }
    return ERR_UNDEFINED_WORD;
}

/* ========================================================================= */
/* WORDLIST TABLE                                                            */
/* ========================================================================= */

struct s_wid wids[WIDS_MAX] = { // wordlists
    [0] = {.head = &forth_heads[(sizeof(forth_heads) / sizeof(s_head)) - 1],
           .name = "`forth" },
    [1] = {.head = &only_heads[(sizeof(only_heads) / sizeof(s_head)) - 1],
           .name = "`only" }
};

static int wids_pointer = 2;

// Start a new wordlist
int lfAddWordlist(char* name) {
    if (wids_pointer >= WIDS_MAX) return ERR_WID_OVERFLOW;
    wids[wids_pointer].head = NULL;
    wids[wids_pointer].name = name;
    return wids_pointer++;
}


/* CONTEXT array holds indices into the wids array, ordered by search priority.
   Terminated with -1 to indicate the end of the search order. */

/* ONLY */
int lfAPI_only(void) {
    int8_t* ctx = CONTEXT;
    *ctx++ = 1;
    *ctx++ = -1;
    return 0;
}

/* FORTH */
int lfAPI_forth(void) {
    CONTEXT[0] = 0;
    return 0;
}

/* .WID  ( n -- ) */
int lfAPI_dotWid(void) {
    uint8_t wid = (uint8_t)vmPop();
    if (wid >= wids_pointer) return ERR_SEARCH_ORDER_OVERFLOW;
    char* s = wids[wid].name;
    if (s == NULL)  return lfDot(wid);
    serial_puts(s); return lfSpace();
}

/* WORDS */
int lfAPI_words(void) {
    const struct s_head* link = wids[CONTEXT[0]].head;
    while (link != NULL) {
        if ((link->aux & A_SMUDGED) == 0) {
            serial_puts(link->name);
            lfSpace();
        }
        link = link->link;
    }
    return 0;
}

/* ========================================================================= */
/* LOOKUP FUNCTION                                                           */
/* ========================================================================= */

uint32_t g_neighbor_w = 0; // the w of the word defined after the found one

static const struct s_head* search_wordlist(int wid_index, const char *target_name) {
    if (wid_index < 0 || wid_index >= wids_pointer) {
        return NULL;
    }
    const struct s_head *link = wids[wid_index].head;
    while (link != NULL) {
        if (TheStringsMatch(link->name, (char *)target_name)) {
            if ((link->aux & A_SMUDGED) == 0) {
                return link;
            }
        }
        g_neighbor_w = link->w;
        link = link->link;
    }
    return NULL;
}

static const struct s_head* search_context(const char *target_name) {
    for (int i = 0; i < CONTEXT_MAX; i++) {
        int wid_idx = CONTEXT[i];
        if (wid_idx == -1) { // end of search order marked by -1
            break;
        }
        const struct s_head *found = search_wordlist(wid_idx, target_name);
        if (found != NULL) {
            return found; // Return immediately upon first match in search order
        }
    }
    return NULL; // Word not found in any active wordlist
}

/*
* .S depends on "empty" marker VM_EMPTYSTACK
*/
int lfDotS(void) {
    int depth = 0;
    while (depth <= DOT_S_MAX) {
        uint32_t val = vmPeek(depth);
        if (val == VM_EMPTYSTACK) break;
        depth++;
    }
    if (depth) {
        serial_puts("( ");
        if (depth > DOT_S_MAX) {
            serial_putc('[');
            lfDotB(depth, lfBASEfetch(), 0, 0);
            serial_puts("]... ");
            depth = DOT_S_MAX;
        }
        while (depth--) {
            lfDot(vmPeek(depth));
        }
        serial_puts(") ");
    }
    return 0;
}


/* =========================================================================
>IN and BLK are the top values of an 8-deep internal block stack. They are
used to manage the input buffer and block number for file-based input.
TIB is a fixed buffer in Forth data space for terminal input.
========================================================================= */

#define TERMINAL_OVERFLOWED 0x8000
#define TERMINAL_RX_DIED    0x4000

static uint32_t system_flags = 0;
static uint32_t linecount = 0;

static int loadTIB(void) {
    char *tib = (char*)TIB; // reset the TIB pointer
	int remaining = TIBSIZE; // remaining space in TIB
    int aux_result = 0;

    if (lfTIBSTATEfetch()) {
        // Announce to Forth that the terminal is waiting for TIBSTATE = 2
        lfTIBSTATEstore(1);
    }
    while (1) {
        if (serial_ready() < 1) {
#ifdef yield2c
            yield2c(); // Yield to other tasks (ans step VM) while waiting...
#endif
            continue;
        }
        int c = serial_getc();
        if (c < 0) {
            aux_result |= TERMINAL_RX_DIED;
            break;                  // treat error as a normal terminator
        }
        if (c == '\r') {
            if (system_flags & SYS_FLAG_IGNORE_CR) continue;
            break;
        }
        if (c == '\n') break;       // CRLF inserts lines
        if (c == 0xFF) c = ' ';     // replace 0xFF with blank
        if (remaining > 1) {
            *tib++ = (char)c;
            remaining--;
        }
        else {                      // ignore input remaining until EOL
            aux_result |= TERMINAL_OVERFLOWED;
        }
    }
    *tib++ = 0;                     // Null-terminate the TIB
    remaining--;
    if (lfTIBSTATEfetch()) {
        lfTIBSTATEstore(2); // Indicate that TIB is ready for processing
#ifdef yield2c
        while (lfTIBSTATEfetch() != 3) {
            yield2c();
        }
#endif
    }
    int length = (TIBSIZE - remaining) | aux_result;
    return length;
}

static void lfDotLinecount(void) {
    lfCR();
    serial_puts("Line ");
    lfDot(linecount);
}

/*
parseStr assumes `str` is always correctly terminated with a null character.
It extracts the next terminator-delimited token from `str` and returns a pointer
to the next character after the token. If no token is found, it returns NULL.
The extracted token is stored in a static buffer for later use.
*/

static char* source = NULL; // current position in the input string
static int source_len = 0;  // explicit length bound for the current input stream

static char TOINchar(void) {
    int toin = lfTOINfetch();
    if (toin >= source_len) {
        return '\0';
    }
    return source[toin];
}

static void TOINbump(void) {
    lfTOINstore(1 + lfTOINfetch());
}

int lfParseWord(char* dest, int destSize) {
    int ior = 0;

    // 1. Skip leading whitespace
    while (1) {
        char c = TOINchar();
        if (c == '\0') break;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        TOINbump();
    }

    // 2. Copy token characters
    int i = 0;
    while (1) {
        char c = TOINchar();
        if (c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            break;
        }
        if (i < (destSize - 1)) {
            dest[i++] = c;
        }
        else {
            ior = ERR_PARSED_STRING_OVERFLOW;
        }
        TOINbump();
    }

    dest[i] = '\0';

    // 3. Skip single trailing delimiter IF we stopped on one (not EOF)
    char delimiter = TOINchar();
    if (delimiter == ' ' || delimiter == '\t' || delimiter == '\r' || delimiter == '\n') {
        TOINbump();
    }

    return ior;
}

static InputFrame input_stack[MAX_INPUT_STACK];
static int input_stack_depth = 0;
static void dumpInputStackTrace(void);
static char* lastparsed = NULL;

/**
 * Forth Text Interpreter
 * Evaluates tokens based on explicit stream length bounds
 * 
 * Blocks are not handled recursively. Instead, a separate block stack is
 * used for nesting. The `interpret` loops until all nests close.
 *
 * @param str Character stream to interpret.
 * @param len Stream length.
 * @return    0 on normal execution, else Forth error code from errcodes.h
 */
static int interpret(char* str, int len) {
    static char token[32];      // token buffer for parsing
    if (str == NULL || len == 0) {
        return 0;
    }

    source = str;
    source_len = len;
    lfTOINstore(0);
    BLK = 0;

    int ior = 0;

    while (1) {
        if (ior) break;

        // Break if >IN reached or exceeded stream length
        if (lfTOINfetch() >= source_len) {
            if (input_stack_depth > 0) {
                // Pop nested input frame
                input_stack_depth--;
                source = input_stack[input_stack_depth].str;
                source_len = input_stack[input_stack_depth].len;
                lfTOINstore(input_stack[input_stack_depth].toin);
                BLK = input_stack[input_stack_depth].blk;
                continue;
            }

            // Exit interpreter loop once root level input is exhausted and BLK == 0
            if (BLK == 0) {
                break;
            }
        }

        ior = lfParseWord(token, sizeof(token));
        lastparsed = token;
        if (ior) break;

        // If no token was parsed (e.g. trailing whitespace at EOF), break out cleanly
        if (token[0] == '\0') {
            break;
        }

        // A. Check active wordlists
        const struct s_head* word = search_context(token);
        int state = lfSTATEfetch();

        if (word != NULL) {
            if (word->aux & A_IMMEDIATE) {
                if (state && (word->aux & A_NO_EXECUTE)) {
                    ior = ERR_INTERPRET_COMPILE_ONLY;
                }
                state = 0;
            }
            if (ior) break;

            if (state) {
                ior = lfCompileWord(word);
            }
            else {
                ior = lfExecuteWord(word);
            }

            int base = lfBASEfetch();
            if (base < 2) {
                ior = ERR_INVALID_BASE;
                lfBASEstore(10);
            }
            continue;
        }
        int32_t value;

        // B. Constant / Numeric fallback
        ior = findConstant(token, &value);
        if (ior) {
            ior = parseNumber(token, lfBASEfetch(), &value);
        }
        if (ior) break; // Exit loop on undefined word or parsing error

        if (state) {
            lfCompileLit(value);
        }
        else {
            vmPush(value);
        }
    }

    // On an abnormal exit (ior != 0), unwind and reset the file nesting stack
    if (ior != 0) {
        if (ior != ERR_QUIT) {
            dumpInputStackTrace();
        }
        input_stack_depth = 0; // ensure input stack is cleared on exit
    }
    return ior;
}

/**
 * QUIT loop 
 *
 * `>options` ( flags -- ) sets the display (etc.) options
 * `options>` ( -- flags ) gets them
 * `bye`      ( ? -- ? )   ends QUIT
 */
int lfAPI_getFlags(void) {
    return vmPush(system_flags);
}

int lfAPI_setFlags(void) {
    int32_t val = vmPop();
    if ((system_flags & SYS_FLAGS_LOCKED) == 0) {
        system_flags = val;
    }
    return 0;
}

// `serial_open` before you call QUIT
int QUIT(void) {
    serial_puts(u8"幸运狐 v");
    lfDotB(TF_VERSION, 10, 2, 3);
    lfCR();
    lfAPI_only();
    lfAPI_forth();
    while (1) {
        LF_PACKEDSTATE[0] = 10;
        LF_PACKEDSTATE[1] = 0;
        linecount = 0;
        vmReset();
        // REPL until an error (or bye) occurs, starting with a clean stack.
        // The `ok>` prompt is at the beginning for compatibility with
        // cooked input. The terminal echoes newline locally.
        int32_t ior = 0;
        while (ior == 0) {
            if ((system_flags & SYS_FLAG_NO_DOTESS) == 0) {
                lfDotS();
            }
            if ((system_flags & SYS_FLAG_NO_OK) == 0) {
                ior = serial_puts("ok>");
                if (ior) break; // lost the output stream
            }
            linecount++;
            int len = loadTIB();
            if (system_flags & SYS_FLAG_VERBOSE) {
                lfDotLinecount();
                serial_puts(TIB);
            }
            ior = interpret((char*)TIB, len & 0x7FFF);
            /*
            * The stack depth is checked here for overflow or underflow.
            * We cheat by using sp as depth. Reading sp is a dependency.
            * Normally, the bottom of the stack is marked by VM_EMPTYSTACK.
            */
            int depth = vmPeek(VM_REG_sp);
            if (depth >= STACK_MASK) ior = ERR_STACK_OVERFLOW;
            else if (depth < 0) ior = ERR_STACK_UNDERFLOW;
            if (len & TERMINAL_OVERFLOWED) ior = ERR_TIB_OVERFLOW;
            if (len & TERMINAL_RX_DIED) ior = ERR_TERM_RX_FAILED;
        }
		// handle the ior here if needed (e.g., exit on BYE)
        LF_PACKEDSTATE[0] = 10; // base = decimal
        switch (ior) {
        case ERR_QUIT: return 0;
        case ERR_UNDEFINED_WORD:
            serial_puts(lastparsed);
            serial_puts(" ?\n");
            break;
        default:
            serial_puts("Error: ior=");
            lfDot(ior);
#if (FAT_FORTH & 1)
            const char* msg = get_error_message(ior);
            serial_puts(msg);
            lfCR();
#endif
            break;
		}
        if (system_flags & SYS_FLAG_VALIDATION) {
            lfDotLinecount();
            return ior; // quit after the first error
        }
    }
}

/* `D'` ( <name> -- w aux ) */
int lfAPI_tickx(void) {
    char token[32] = { 0 };
    int ior = lfParseWord(token, sizeof(token));
    if (ior) return ior;
    const struct s_head* word = search_context(token);
    if (word == NULL) return ERR_UNDEFINED_WORD;
    vmPush(word->w);
    return vmPush(word->aux);
}

static struct s_head* latest = NULL;

// Modify the last created header (immediate, etc.)
int lfToHeader(uint32_t w, uint32_t aux) {
    if (latest == NULL) return ERR_UNSUPPORTED_OPERATION;
    latest->w |= w;
    latest->aux ^= aux;
    return 0;
}

char* lfHeaderName = NULL;

// Create a header structure in Forth memory space
int lfHeader(uint32_t w, uint32_t aux, char** name) {
    char token[32] = { 0 };
    int ior = lfParseWord(token, sizeof(token));
    if (ior) return ior;

    // Resolve destination memory location in 32-bit cells
    int32_t* headptr = &vm_memory[RAM_PAGE][F_PTRS + 2];
    int32_t  f_hp = *headptr;
    int32_t  f_hmax = headptr[3];
    int page = f_hp >> (22 - VM_LOG2_PAGES);
    int32_t  start_f_hp = f_hp & 0x3FFFFF;

    int32_t* cell_dest = &vm_memory[page][start_f_hp];

    // Pack string name into 32-bit cells
    char* name_dest = (char*)cell_dest;
    if (name != NULL) {
        *name = name_dest;
    }
    int32_t ch_dest = start_f_hp | (8 << 27); // bytes
    char* src = token;
    char c = 0;
    uint8_t length = 0;

    do {
        c = *src++;
        int ior = vmStore(ch_dest, c);
        if (ior) return ior;
        ch_dest = vmFieldPlus(ch_dest);
        length++;
    } while (c);
    /*
    * vmStore did not give an error, assume struct s_head will not step on
    * anything critical.
    */
    while (length & 3) {
        vmStore(ch_dest, 0);
        ch_dest = vmFieldPlus(ch_dest);
        length++;
    }

    cell_dest += (length / sizeof(int32_t));

    // Construct struct s_head header directly in the next cell boundary
    struct s_head* target_head = (struct s_head*)cell_dest;
    latest = target_head;
    target_head->name = name_dest;
    target_head->w = w;
    target_head->aux = aux;

    // Insert header at top of current wordlist (linked list)
    s_wid* current = &wids[*CURRENT];
    target_head->link = (struct s_head*)current->head; // Point new node to current head

    // Update current wordlist head pointer
    current->head = target_head;

    // Advance cell_dest past the struct s_head
    int32_t head_cells = (sizeof(struct s_head) + sizeof(int32_t) - 1) / sizeof(int32_t);
    cell_dest += head_cells;

    // Calculate new f_hp address (start_f_hp + total cell delta)
    int32_t total_cells_used = (int32_t)(cell_dest - &vm_memory[page][start_f_hp]);
    int32_t new_f_hp = (page << (22 - VM_LOG2_PAGES)) | ((start_f_hp + total_cells_used) & 0x3FFFFF);

    // Bounds check before writing back to F_PTRS
    if (new_f_hp >= f_hmax) {
        return ERR_DICTIONARY_OVERFLOW;
    }

    headptr[0] = new_f_hp;
    return 0;
}

static int lfParenthesis(int echo) {
    while (1) {
        char c = TOINchar();
        if (c == '\0') break;
        TOINbump();
        if (c == ')') break;
        if (echo) {
            serial_putc(c);
        }
    }
    return 0;
}

/* ( */
int lfAPI_paren(void) {
    return lfParenthesis(0);
}

/* .( */
int lfAPI_dotParen(void) {
    return lfParenthesis(1);
}


/**
 * Nests a new input stream (e.g., from an included block or file).
 * Saves current input context and switches to the provided buffer.
 */
int lfNestInput(char* src, int length, int32_t block) {
    if (input_stack_depth >= MAX_INPUT_STACK) {
        return ERR_STACK_OVERFLOW;
    }

    // Save current active stream state onto the nesting stack
    input_stack[input_stack_depth].str = source;
    input_stack[input_stack_depth].len = source_len;
    input_stack[input_stack_depth].toin = lfTOINfetch();
    input_stack[input_stack_depth].blk = BLK;
    input_stack_depth++;

    // Load new input stream
    source = src;
    source_len = length;
    lfTOINstore(0);
    BLK = block;

    return 0;
}

#ifndef SCREEN_COLUMNS
#define SCREEN_COLUMNS 128
#endif

static void dumpInputStackTrace(void) {
    if (BLK == 0 && input_stack_depth == 0) {
        return;
    }

    // Save active frame state into working variables
    char* cur_str = source;
    int cur_toin = lfTOINfetch();
    int cur_blk = BLK;

    while (1) {
        int cur_row = (cur_toin / SCREEN_COLUMNS) + 1;
        int cur_col = (cur_toin % SCREEN_COLUMNS) + 1;

        int line_start = cur_toin;
        while (line_start > 0 && cur_str[line_start - 1] != '\n' && cur_str[line_start - 1] != '\r') {
            line_start--;
        }

        serial_puts("Screen ");
        lfDot10(cur_blk);
        serial_puts(" [");
        lfDot10(cur_row);
        serial_putc(':');
        lfDot10(cur_col);
        serial_puts("] ");

        for (int p = line_start; p < cur_toin; p++) {
            serial_putc(cur_str[p]);
        }
        lfCR();

        // Stop if we've processed all frames
        if (input_stack_depth == 0) {
            break;
        }

        // Pop next frame from stack into current working variables
        input_stack_depth--;
        InputFrame* frame = &input_stack[input_stack_depth];
        cur_str = frame->str;
        cur_toin = frame->toin;
        cur_blk = frame->blk;
    }
}

/**
 * --> ( -- )
 * Immediate word / Primitive: Terminate parsing the current block
 * and load block BLK + 1.
 */
int lfAPI_nextBlock(void) {
    if (BLK == 0) {
        return ERR_INVALID_BLOCK_NUMBER;
    }
    lfTOINstore(source_len);
    vmPush(BLK + 1);
    return lfAPI_load();
}