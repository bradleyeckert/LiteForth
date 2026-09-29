#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "comp.h"
#include "tools.h"
#include "lfblocks.h"
#include "options.h"
#include <string.h>

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
#define API0(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_API0 | (idx))
#define SYS(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_SYS | (idx))
#define SYSTO(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_TOSYS | (idx))
#define SYSFM(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_FROMSYS | (idx))

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
    { LINK(30), "2dup",         MACRO(VMU_OVER,VMU_OVER,VMU_NOP),         0},
    { LINK(31), "2drop",        MACRO(VMU_DROP,VMU_DROP,VMU_NOP),         0},
    { LINK(32), "!",            MACRO(VMU_ASTORE,VMU_STOREA,VMU_NOP),     0},
    { LINK(33), "@",            MACRO(VMU_ASTORE,VMU_FETCHA,VMU_NOP),     0},
    { LINK(34), "s@",           MACRO(VMU_ASTORE,VMU_FETCHASIGN,VMU_NOP), 0},
    { LINK(35), "nip",          MACRO(VMU_SWAP,VMU_DROP,VMU_NOP),         0},
    { LINK(36), "tuck",         MACRO(VMU_SWAP,VMU_OVER,VMU_NOP),         0},
    { LINK(37), "um+",          MACRO(VMU_PLUS,VMU_CY,VMU_NOP),           0},
    { LINK(38), "slice+",       SYS(VMS_FIELDPLUS),  /* a1 -- a2      */  0},
    { LINK(39), "]shr",         SYS(VMS_SHR),        /* u1 -- u2      */  0},
    { LINK(40), "]shl",         SYS(VMS_SHL),        /* u1 -- u2      */  0},
    { LINK(41), "shft[",        SYSTO(VMSTO_SHIFT),  /* position --   */  0},
    { LINK(42), "]task",        SYSTO(VMSTO_TASK),   /* tstate --     */  0},
    { LINK(43), "yeet",         SYSTO(VMSTO_YEET),   /* ior --        */  0},
    { LINK(44), "task[",        SYSFM(VMSFROM_TASK), /* -- tstate     */  0},
    { LINK(45), "um*",          API0( 4), /* u1 u2 -- ud              */  0},
    { LINK(46), "m*",           API0( 5), /* n1 n2 -- d               */  0},
    { LINK(47), "mu/mod",       API0( 6), /* ud u -- rem dquot        */  0},
    { LINK(48), "*/mod",        API0( 7), /* n1 n2 -- rem quot        */  0},
    { LINK(49), "t_rx?",        API0( 8), /* -- flag                  */  0},
    { LINK(50), "t_rx",         API0( 9), /* -- c                     */  0},
    { LINK(51), "t_tx!",        API0(10), /* c --                     */  0},
    { LINK(52), "t_tx?",        API0(11), /* -- c                     */  0},
    { LINK(53), ":",            API0(12), /* <name> --                */  0},
    { LINK(54), ";",            API0(13),                   A_IMMEDIATE | 0},
    { LINK(55), ">options",     API0(14), /* n --                     */  0},
    { LINK(56), "empty",        API0(15), /* --                       */  0},
    { LINK(57), "(",            API0(16), /* -- */          A_IMMEDIATE | 0},
    { LINK(58), ".(",           API0(17), /* --                       */  0},
    { LINK(59), "does>",        API0(18), /* -- */          A_IMMEDIATE | 0},
    { LINK(60), "create",       API0(19), /* -- | -- addr             */  0},
    { LINK(61), "cr",           API0(20), /* --                       */  0},
    { LINK(62), "literal",      API0(21), /* n -- */        A_IMMEDIATE | 0},
    { LINK(63), ".wid",         API0(22), /* wid --                   */  0},
    { LINK(64), "x'",           API0(23), /* <name> -- w aux          */  0},
    { LINK(65), "page",         API0(24), /* page -- a                */  0},
    { LINK(66), "wordlist",     API0(25), /* -- wid                   */  0},
    { LINK(67), "open-flash",   API0(26), /* addr --                  */  0},
    { LINK(68), "close-flash",  API0(27), /* --                       */  0},
    { LINK(69), "]",            API0(28), /* --                       */  0},
    { LINK(70), "[",            API0(29), /* -- */          A_IMMEDIATE | 0},
    { LINK(71), "exit",         API0(30), /* -- */          A_IMMEDIATE | 0},
    { LINK(72), "constant",     API0(31), /* n <name> --              */  0},
    { LINK(73), "bits",         API0(32), /* n <name> --              */  0},
    { LINK(74), ">body",        API0(33), /* xt -- addr               */  0},
    { LINK(75), "_,\"",         API0(34), /* string" -- addr          */  0},
    { LINK(76), "bit",          API0(35), /* n --                     */  0},
    { LINK(77), ",inst",        API0(36), /* inst --                  */  0},
    { LINK(78), "immediate",    API0(37), /* --                       */  0},
    { LINK(79), "block",        API0(38), /* u -- addr                */  0},
    { LINK(80), "buffer",       API0(39), /* u -- addr                */  0},
    { LINK(81), "update",       API0(40), /* --                       */  0},
    { LINK(82), "save-buffers", API0(41), /* --                       */  0},
    { LINK(83), "flush",        API0(42), /* --                       */  0},
    { LINK(84), "empty-buffers",API0(43), /* --                       */  0},
    { LINK(85), "load",         API0(44), /* u --                     */  0},
    { LINK(86), "capacity",     API0(45), /* -- u                     */  0},
    { LINK(87), "-->",          API0(46), /* --                       */  0},
    { LINK(88), "postpone",     API0(47), /* <name> -- */   A_IMMEDIATE | 0},
    { LINK(89), ",compile",     API0(API_COMPILE), /* xt --           */  0},
    { LINK(90), "break",        API0(49), /* --                       */  0},
#if (FAT_FORTH & 1)                                                     
    { LINK(91), "}t",           API0(50), /* ? --                     */  0},
    { LINK(92), "->",           API0(51), /* ? --                     */  0},
    { LINK(93), "t{",           API0(52), /* --                       */  0},
    { LINK(94), "hex",          API0(53), /* --                       */  0},
    { LINK(95), "decimal",      API0(54), /* --                       */  0},
    { LINK(96), ".page",        API0(55), /* n --                     */  0},
    { LINK(97), ".pages",       API0(56), /* --                       */  0},
    { LINK(98), "dump",         API0(57), /* addr length --           */  0},
    { LINK(99), "dumpi",        API0(58), /* inst --                  */  0},
    { LINK(100), "dasm",        API0(59), /* addr length --           */  0},
    { LINK(101), ".s",          API0(60), /* --                       */  0},
    { LINK(102), ".",           API0(61), /* n --                     */  0},
    { LINK(103), "see",         API0(62), /* <name> --                */  0 },
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
    { VMI_PFX1,         "_pfx1"},
    { VMI_USER,         "_user"},
    { VMI_QLIT,         "_qlit"},
    { VMI_API0,         "_api0"},
    { VMI_API1,         "_api1"},
    { W_PRIMITIVE,      "w_primitive"},
    { W_MACRO,          "w_macro"},
    { W_WIDE_INST,      "w_wide_inst"},
    { W_NO_TAIL_CALL,   "w_no_tail_call"},
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

struct s_wid wids[WIDS_MAX];

static struct s_wid wids_empty[] = { // wordlists
    {.head = &forth_heads[(sizeof(forth_heads) / sizeof(s_head)) - 1], .name = "`forth" },
    {.head = &only_heads[(sizeof(only_heads) / sizeof(s_head)) - 1],   .name = "`only" }
};

#define EMPTY_WIDS  2
static int wids_pointer = 2;

static void lfResetWids(void) { // initialize the wordlists
    memcpy(wids, wids_empty, sizeof(wids_empty));
    wids_pointer = EMPTY_WIDS;
}

// EMPTY  ( -- )  Resets the dictionary
int lfAPI_empty(void) {
    lfResetWids();
    return lfInitPointers();
}

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
    lf_puts(s); return lfSpace();
}

/* WORDS */
int lfAPI_words(void) {
    const struct s_head* link = wids[CONTEXT[0]].head;

    while (link != NULL) {
        if ((link->aux & A_SMUDGED) == 0) {
            if (link->aux & A_IMMEDIATE) {
                lfSetColor(COLOR_BRIGHT_YELLOW);
            }
            else if (link->w & W_PRIMITIVE) {
                lfSetColor(COLOR_GREEN);
            }
            else if (link->aux & A_CONSTANT) {
                lfSetColor(COLOR_BRIGHT_CYAN);
            }
            lf_puts(link->name);
            lfSetColor(COLOR_NORMAL);
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

// find a word in a given wordlist
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

// Index through the context to find a word
static const struct s_head* search_context(const char *target_name) {
    for (int i = 0; i < CONTEXT_MAX; i++) {
        int wid_idx = CONTEXT[i];
        if (wid_idx == -1) break; // end of context
        const struct s_head *found = search_wordlist(wid_idx, target_name);
        if (found != NULL) return found; // found it
    }
    return NULL; // Word not found in any active wordlist
}

// Traverse all wordlists in the context for the name of a value
char* lfFindLabel(uint32_t value, uint32_t mask, uint32_t must, uint32_t expected) {
    for (int i = 0; i < CONTEXT_MAX; i++) {
        int wid_idx = CONTEXT[i];
        if (wid_idx == -1) break; // end of context
        const struct s_head* link = wids[wid_idx].head;
        while (link != NULL) { // traverse the wordlist
            uint32_t xt = link->w;
            if ((xt & must) == expected) {
                xt &= mask;
                if (xt == value) {
                    if ((link->aux & A_SMUDGED) == 0) {
                        return link->name;
                    }
                }
            }
            link = link->link;
        }
    }
    return NULL;
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
        lf_puts("( ");
        if (depth > DOT_S_MAX) {
            lf_putc('[');
            lfDotB(depth, lfBASEfetch(), 0, 0);
            lf_puts("]... ");
            depth = DOT_S_MAX;
        }
        while (depth--) {
            lfDot(vmPeek(depth));
        }
        lf_puts(") ");
    }
    return 0;
}


/* =========================================================================
>IN and BLK are the top values of an 8-deep internal block stack. They are
used to manage the input buffer and block number for file-based input.
TIB is a fixed buffer in Forth data space for terminal input.
========================================================================= */

#define TERMINAL_OVERFLOWED 0x8000

uint32_t g_lf_sys_options = 0;
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
            g_lf_sys_options &= ~(SYS_OPTION_NO_OK | SYS_OPTION_NO_DOTESS);
            break;                  // treat error as a normal terminator
        }
        if (c == '\r') {
            if (g_lf_sys_options & SYS_OPTION_IGNORE_CR) continue;
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
    lf_puts("Line ");
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
                BLK = input_stack[input_stack_depth].blk;
                source = input_stack[input_stack_depth].str;
                source_len = input_stack[input_stack_depth].len;
                lfTOINstore(input_stack[input_stack_depth].toin);
                if (BLK != 0) {
                    int32_t f_addr = 0;
                    ior = lfAssignBlock(BLK, &f_addr);
                    source = (char*)&vm_memory[RAM_PAGE][f_addr & VM_PAGE_MASK];
                }
                continue;
            }
            lfAPI_emptyBuffers(); // so it will load your edits 
            
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
 * `bye`      ( ? -- ? )   ends QUIT
 */

int lfAPI_setFlags(void) {
    int32_t val = vmPop();
    if ((g_lf_sys_options & SYS_OPTIONS_LOCKED) == 0) {
        if (val) { // set more options
            g_lf_sys_options |= val;
        }
        else { // clear options
            g_lf_sys_options = 0;
        }
    }
    return 0;
}

// `serial_open` before you call QUIT
int QUIT(void) {
    lf_puts(u8"幸运狐 v");
    lfDotB(TF_VERSION, 10, 2, 3);
    lfCR();
    lfAPI_empty();
    lfAPI_only();
    lfAPI_forth();
    while (1) {
        lfSetColor(COLOR_NORMAL);
        LF_PACKEDSTATE[0] = 10;
        LF_PACKEDSTATE[1] = 0;
        linecount = 0;
        vmReset();
        // REPL until an error (or bye) occurs, starting with a clean stack.
        // The `ok>` prompt is at the beginning for compatibility with
        // cooked input. The terminal echoes newline locally.
        int32_t ior = 0;
        while (ior == 0) {
            if ((g_lf_sys_options & SYS_OPTION_NO_DOTESS) == 0) {
                lfDotS();
            }
            if ((g_lf_sys_options & SYS_OPTION_NO_OK) == 0) {
                ior = lf_puts("ok>");
                if (ior) break; // lost the output stream
            }
            linecount++;
            int len = loadTIB();
            if (g_lf_sys_options & SYS_OPTION_VERBOSE) {
                lfCR();
                lfDotLinecount();
                lf_puts(TIB);
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
        }
		// handle the ior here if needed (e.g., exit on BYE)
        LF_PACKEDSTATE[0] = 10; // base = decimal
        lfSetColor(COLOR_BRIGHT_RED);
        switch (ior) {
        case ERR_QUIT: return 0;
        case ERR_UNDEFINED_WORD:
            lf_puts(lastparsed);
            lf_puts(" ?\n");
            break;
        default:
            lf_puts("Error: ior=");
            lfDot(ior);
#if (FAT_FORTH & 1)
            const char* msg = get_error_message(ior);
            lf_puts(msg);
            lfCR();
#endif
            break;
		}
        if (g_lf_sys_options & SYS_OPTION_VALIDATION) {
            lfDotLinecount();
            return ior; // quit after the first error
        }
    }
}

/* Tick primitive */
const struct s_head* lfTickWord(void) {
    char token[32] = { 0 };
    int ior = lfParseWord(token, sizeof(token));
    if (ior) return NULL;
    const struct s_head* word = search_context(token);
    if (word == NULL) return NULL;
    return word;
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
    int32_t* textptr = &vm_memory[RAM_PAGE][F_PTRS_TP];
    int32_t  f_hp = *textptr;
    int32_t  f_hmax = textptr[1];
    int page = (f_hp & 0x3FFFFF) >> (22 - VM_LOG2_PAGES);
    int32_t ch_dest = lfSetSliceWidth(f_hp, 8); // LF byte address for name
    uint32_t dest = ch_dest & VM_PAGE_MASK; // cell index within page

    int32_t* cell_dest = &vm_memory[page][dest];

    // Name string starts here
    char* name_dest = ((char*)cell_dest) + ((ch_dest >> 25) & 3);
    if (name != NULL) {
        *name = name_dest;
    }
    char* src = token;
    char c = 0;

    // Use VM functions to respect the sandbox
    do {
        c = *src++;
        int ior = vmStore(ch_dest, c);
        if (ior) return ior;
        ch_dest = vmFieldPlus(ch_dest);
    } while (c);
    /*
    * vmStore did not give an error, assume struct s_head will not step on
    * anything critical. Pad with 0 bytes to cell-align.
    */
    while ((ch_dest >> 22) & 0x1F) {
        vmStore(ch_dest, 0);
        ch_dest = vmFieldPlus(ch_dest);
    }
    ch_dest &= VM_PAGE_MASK;
    cell_dest = &vm_memory[page][ch_dest];

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
    int32_t total_cells_used = (int32_t)(cell_dest - &vm_memory[page][dest]);
    int32_t new_f_hp = (page << (22 - VM_LOG2_PAGES)) | ((dest + total_cells_used) & 0x3FFFFF);

    // Bounds check before writing back to F_PTRS
    if (new_f_hp >= f_hmax) {
        return ERR_DICTIONARY_OVERFLOW;
    }

    *textptr = new_f_hp;
    return 0;
}

int lfParseInputString(putcfunc* echo, char terminator) {
    int escaped = 0;

    while (1) {
        char c = TOINchar();
        if (c == '\0') break;
        TOINbump();

        if (escaped) {
            escaped = 0;
            switch (c) {
            case 'a':  c = '\a'; break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case 'v':  c = '\v'; break;
            default: break; // \? = ?
            }
        }
        else if (c == '\\') {
            escaped = 1;
            continue;
        }
        else if (c == terminator) {
            break;
        }

        if (echo) {
            int ior = echo(c);
            if (ior) return ior;
        }
    }
    return 0;
}

// BLOCK  ( u -- addr )  Get addr of block u, reading from storage if needed.
int lfAPI_block(void) {
    int32_t f_addr = 0;
    int ior = lfAssignBlock((uint32_t)vmPop(), &f_addr);
    vmPush(f_addr);
    return ior;
}

/**
 * LOAD  ( blk -- )
 * Saves current input context and redirects interpreter input to block i.
 * lfAssignBlock
 */
int lfAPI_load(void) {
    int32_t blk = vmPop();
    if (blk == 0) return ERR_INVALID_BLOCK_NUMBER;
    int32_t forth_addr = 0;
    int ior = lfAssignBlock(blk, &forth_addr);

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
    source = (char*)&vm_memory[RAM_PAGE][forth_addr & VM_PAGE_MASK];;
    source_len = sizeof(int32_t) * BLOCK_SIZE_CELLS;
    lfTOINstore(0);
    BLK = blk;
    return ior;
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

        if (cur_blk) {
            lf_puts("Screen ");
            lfDot10(cur_blk);
        }
        else {
            lf_puts("Terminal");
        }
        lf_puts(" [");
        lfDot10(cur_row);
        lf_putc(':');
        lfDot10(cur_col);
        lf_puts("] ");

        for (int p = line_start; p < cur_toin; p++) {
            lf_putc(cur_str[p]);
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
