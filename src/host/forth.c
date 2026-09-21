#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include <stdio.h> // remove for production code
// cd /mnt/c/Users/User/Documents/GitHub/LiteForth

#if (FAT_FORTH & 1)
#include "utils.h"
#endif

// some globals used in this file
static uint32_t system_flags = 0;
static uint32_t linecount = 0;
static int case_insensitive = CASE_INSENSITIVE;
static char token[32]; // token buffer for parsing
static char* source = NULL; // pointer to the current position in the input string
static int32_t value; // value returned by numeric input parsing
static int slot = SLOT0_POSITION;
static uint16_t instruction;

int lfBASEfetch(void) {
    int32_t result;
    vmFetch(LF_BASE, &result);
    return result;
}

int lfBASEstore(int base) {
    return vmStore(LF_BASE, base);
}

static int lfSTATEfetch(void) {
    int32_t result;
    vmFetch(LF_STATE, &result);
    return result;
}

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

int vmPush(int32_t x) {
    return vmPoke(-1, x);
}

int32_t vmPop(void) {
    return vmPeek(-1);
}

/* =========================================================================
* String output functions
========================================================================= */

int serial_puts(const char* s) {
    int ior = 0;
    while (*s) {
        ior = serial_putc(*s++);
        if (ior) return ior;
    }
    return 0;
}

int lfDotB(uint32_t val, int base, int dpl, int digits) {
    char buf[36] = { 0 }; // Enough for 32-bit integer
    char* p = &buf[sizeof(buf)];
    *--p = 0; // Null terminator
    if ((val & 0x80000000) && (base == 10)) {
        val = 0 - val;
        serial_putc('-');
    }
    do {
        if (p == buf) break; // no more room
        int digit = val % base;
        val /= base;
        if (digit < 10) {
            *--p = '0' + digit;
        }
        else {
            *--p = 'A' + (digit - 10);
        }
        digits--;
        if (dpl) {          // optional decimal
            if (--dpl == 0) {
                *--p = '.';
            }
        }
    } while (val | dpl | (digits > 0));
    int result = serial_puts(p);
    return result;
}

int lfCR(void) {
    return serial_puts("\r\n");
}

int lfSpace(void) {
    return serial_putc(' ');
}

int lfDot(int32_t val) {
    int base = lfBASEfetch();
    lfDotB(val, base, 0, 0);
    if (base == 16) {
        serial_putc('H');
    }
    return lfSpace();
}

/*=========================================================================
* Compiling
=========================================================================*/

static int cpFetch(uint32_t* cp) {
    int32_t* mem = vm_memory[RAM_PAGE];
    *cp = mem[F_PTRS + 1];
    return 0;
}

static int cpStore(uint32_t cp) {
    int32_t* mem = vm_memory[RAM_PAGE];
    uint32_t cp_max = mem[F_PTRS + 4];
    if ((cp & 0x3FFFFF) >= cp_max) return ERR_DICTIONARY_OVERFLOW;
    mem[F_PTRS + 1] = cp;
    return 0;
}

/*
 * Compile to code space. The data size is determined by the upper bits of cp.
 */
static int commaCode(uint32_t inst) {
    uint32_t cp = 0;
    int ior = cpFetch(&cp);
    if (ior) return ior;
    ior = vmStore(cp, inst);
    if (ior) return ior;
    printf("mem[%x]=%x ", cp & 0x3FFFFF, inst);
    cp = vmCharPlus(cp);
    return cpStore(cp);
    instruction = 0;
    slot = SLOT0_POSITION;
}

static void NewInst(void) {
    if (slot != SLOT0_POSITION) commaCode(VM_UOPS | instruction);
    instruction = 0;
}

static void InstCompile(uint32_t inst) {// compile instruction
    NewInst();                          // flush any uops
    commaCode(inst);
}

static void CompCall(uint32_t addr) {
    NewInst();
    if (addr & ~VM_LIMM_MASK) commaCode(VMI_PFX + (addr >> VM_LIMM_BITS));
    commaCode(VMI_CALL + (addr & VM_LIMM_MASK));
}

static int CompUop(uint32_t uop) {
    uop &= 0x1F; // slots = 9, 4, -1
    int ior = 0;
    if (slot < -4) commaCode(VM_UOPS | instruction);
    if (slot < 0) { // last slot
        slot = 0;
        if (uop >= (1 << LAST_SLOT_WIDTH)) {
            ior = commaCode(VM_UOPS | instruction);
        }
    }
    instruction |= uop << slot;
    slot -= 5;
    return ior;
}

static int CompUlit(uint32_t x) {       // unsigned literal
    int ior = 0;
    vmPush(-1);
    vmPush(VMI_LIT | (x & VM_LIMM_MASK));
    x = x >> VM_LIMM_BITS;
    while (x) {
        vmPush(VMI_PFX | (x & VM_IMM_MASK));
        x = x >> VM_IMM_BITS;
    }
    while (1) {
        int32_t n = vmPop();
        if (n < 0) break;
        ior = commaCode(n);
        if (ior) return ior;
    }
    return 0;
}

int lfCompileLit(int32_t x) {
    if (x < 0) {
        CompUlit(~x);
        return CompUop(VMU_INV);
    }
    return CompUlit(x);
}

static int compile_word(const struct s_head* word) {
    printf("Compiling word: %s (w=%x, aux=%x)\n", word->name, word->w, word->aux);
    return 0;
}


/*=========================================================================
* Define a Forth
=========================================================================*/

// Flags in word->w[31:27]
#define IS_PRIMITIVE 0x80000000 // the xt is a primitive in slot 1
#define IS_MACRO     0xC0000000 // the xt is all primitives except nops

// Flags in word->aux[31:24]
#define IS_SMUDGED   0x80000000 // this bit is set by `:`
#define IS_IMMEDIATE 0x40000000 // this word is immediate
#define IS_CONSTANT  0x20000000 // w is a constant
#define IS_NOTHING   0x10000000 // do nothing
//#define IS_IMM_ONLY  0x08000000

#define UOP(val) (IS_PRIMITIVE | VM_UOPS | ((val) << 9) )
#define MACRO(s0, s1, s2) (IS_MACRO | VM_UOPS | ((s0) << 9)| ((s1) << 4)| (s2) )
#define DATA(idx) ((RAM_PAGE << (22 - VM_LOG2_PAGES)) + (idx))
#define API0(idx) (IS_PRIMITIVE | VMI_API0 | (idx))
#define SYS(idx) (IS_PRIMITIVE | VMI_SYS | (idx))
#define SYSTO(idx) (IS_PRIMITIVE | VMI_TOSYS | (idx))
#define SYSFM(idx) (IS_PRIMITIVE | VMI_FROMSYS | (idx))

static int execute_word(const struct s_head* word) {
    if (word->aux & IS_NOTHING)  return 0;
    if (word->aux & IS_CONSTANT) return vmPush(word->w);

    if (word->w & IS_PRIMITIVE) {
        // Handle primitive word execution
        int32_t ior = vmRun(1, word->w & 0xFFFF, 0);
        if (ior) return ior;
    } else {
        // Handle non-primitive word execution (e.g., user-defined)
        // For now, we just print a message
        printf("Executing word: %s (w=%x, aux=%x)\n", word->name, word->w, word->aux);
	}
    return 0;
}

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
    { LINKO(3), "invert",       UOP(VMU_INV),                          0x0},
    { LINK( 0), "inv",          UOP(VMU_INV),                          0x0},
    { LINK( 1), "over",         UOP(VMU_OVER),                         0x0},
    { LINK( 2), "a!",           UOP(VMU_ASTORE),                       0x0},
    { LINK( 3), "xor",          UOP(VMU_XOR),                          0x0},
    { LINK( 4), "+",            UOP(VMU_PLUS),                         0x0},
    { LINK( 5), "and",          UOP(VMU_AND),                          0x0},
    { LINK( 6), ">r",           UOP(VMU_PUSH),                         0x0},
    { LINK( 7), "unext",        UOP(VMU_UNEXT),                        0x0},
    { LINK( 8), "2*",           UOP(VMU_TWOSTAR),                      0x0},
    { LINK( 9), "dup",          UOP(VMU_DUP),                          0x0},
    { LINK(10), "drop",         UOP(VMU_DROP),                         0x0},
    { LINK(11), "@a",           UOP(VMU_FETCHA),                       0x0},
    { LINK(12), "@a+",          UOP(VMU_FETCHAPLUS),                   0x0},
    { LINK(13), "@as",          UOP(VMU_FETCHASIGN),                   0x0},
    { LINK(14), "r@",           UOP(VMU_R),                            0x0},
    { LINK(15), "r>",           UOP(VMU_POP),                          0x0},
    { LINK(16), "2/c",          UOP(VMU_TWODIVC),                      0x0},
    { LINK(17), "2/",           UOP(VMU_TWODIV),                       0x0},
    { LINK(18), "!a",           UOP(VMU_STOREA),                       0x0},
    { LINK(19), "!a+",          UOP(VMU_STOREAPLUS),                   0x0},
    { LINK(20), "!b",           UOP(VMU_STOREB),                       0x0},
    { LINK(21), "!b+",          UOP(VMU_STOREBPLUS),                   0x0},
    { LINK(22), "swap",         UOP(VMU_SWAP),                         0x0},
    { LINK(23), "+*",           UOP(VMU_PLUSSTAR),                     0x0},
    { LINK(24), "b",            UOP(VMU_B),                            0x0},
    { LINK(25), "b!",           UOP(VMU_BSTORE),                       0x0},
    { LINK(26), "@b",           UOP(VMU_FETCHB),                       0x0},
    { LINK(27), "@b+",          UOP(VMU_FETCHBPLUS),                   0x0},
    { LINK(28), "a",            UOP(VMU_A),                            0x0},
    { LINK(29), "cy",           UOP(VMU_CY),                           0x0},
    { LINK(30), "cells",        0,                        IS_NOTHING | 0x0},
    { LINK(31), "2dup",         MACRO(VMU_OVER,VMU_OVER,VMU_NOP),      0x0},
    { LINK(32), "!",            MACRO(VMU_ASTORE,VMU_STOREA,VMU_NOP),  0x0},
    { LINK(33), "@",            MACRO(VMU_ASTORE,VMU_FETCHA,VMU_NOP),  0x0},
    { LINK(34), "s@",         MACRO(VMU_ASTORE,VMU_FETCHASIGN,VMU_NOP),0x0},
    { LINK(35), "nip",          MACRO(VMU_SWAP,VMU_DROP,VMU_NOP),      0x0},
    { LINK(36), "tuck",         MACRO(VMU_SWAP,VMU_OVER,VMU_NOP),      0x0},
    { LINK(37), "char+",        SYS(VMS_CHARPLUS),   /* a1 -- a2    */ 0x0},
    { LINK(38), "]task",        SYSTO(VMS_CHARPLUS), /* tstate --   */ 0x0},
    { LINK(39), "barf",         SYSTO(VMS_CHARPLUS), /* ior --      */ 0x0},
    { LINK(40), "task[",        SYSFM(VMS_CHARPLUS), /* -- tstate   */ 0x0},
    { LINK(41), "um*",          API0( 4), /* u1 u2 -- ud            */ 0x0},
    { LINK(42), "m*",           API0( 5), /* n1 n2 -- d             */ 0x0},
    { LINK(43), "mu/mod",       API0( 6), /* ud u -- rem dquot      */ 0x0},
    { LINK(44), "*/mod",        API0( 7), /* n1 n2 -- rem quot      */ 0x0},
    { LINK(45), "key?",         API0( 8), /* -- flag                */ 0x0},
    { LINK(46), "key",          API0( 9), /* -- c                   */ 0x0},
    { LINK(47), "emit",         API0(10), /* c --                   */ 0x0},
    { LINK(48), "header",       API0(11), /* w aux <name> --        */ 0x0},
    { LINK(49), ">options",     API0(12), /* n --                   */ 0x0},
    { LINK(50), "options>",     API0(13), /* -- n                   */ 0x0},
    { LINK(51), "(",            API0(14), /* -- */      IS_IMMEDIATE | 0x0},
    { LINK(52), ".(",           API0(15), /* --                     */ 0x0},
    { LINK(53), ".s",           API0(16), /* --                     */ 0x0},
    { LINK(54), ".",            API0(17), /* n --                   */ 0x0},
    { LINK(55), "cr",           API0(18), /* --                     */ 0x0},
    { LINK(56), "space",        API0(19), /* --                     */ 0x0},
    { LINK(57), ".wid",         API0(20), /* wid --                 */ 0x0},
    { LINK(58), "p'",           API0(21), /* <name> -- w aux        */ 0x0},
    { LINK(59), "page",         API0(22), /* page -- a              */ 0x0},
    { LINK(60), ",lit",         API0(23), /* u --                   */ 0x0},
    { LINK(61), "flash-open",   API0(24), /*                        */ 0x0},
    { LINK(62), "flash-close",  API0(25), /*                        */ 0x0},
#if (FAT_FORTH & 1)                                                  
    { LINK(63), "}t",           API0(26), /* ? --                   */ 0x0},
    { LINK(64), "->",           API0(27), /* ? --                   */ 0x0},
    { LINK(65), "t{",           API0(28), /* --                     */ 0x0},
    { LINK(66), "hex",          API0(29), /* --                     */ 0x0},
    { LINK(67), "decimal",      API0(30), /* --                     */ 0x0},
    { LINK(68), ".page",        API0(31), /* n --                   */ 0x0},
    { LINK(69), ".pages",       API0(32), /* --                     */ 0x0},
    { LINK(70), "dump",         API0(33), /* addr length --         */ 0x0},
    { LINK(71), "dumpi",        API0(34), /* inst --                */ 0x0},
    { LINK(72), "dasm",         API0(35), /* addr length --         */ 0x0},
#endif
};

static const ConstantMapping constant_table[] = {
    { -1,           "true"},
    { 0,            "false"},
    { LF_STATE,     "state"},
    { LF_BASE,      "base"},
    { LF_CURRENT,   "current"},
    { LF_CONTEXT,   "context"},
    { CONTEXT_MAX,  "|context|"},
    { LF_DPL,       "dpl"},
    { LF_TOIN,      ">in"},
    { LF_BLK,       "blk"},
    { LF_TIB,       "tib"},
    { LF_PTRS,      "dp[]"},
    { LF_MSPACE,    "dp^" },
    { VM_MEM_PAGES, "pages"},
    { LF_HERE0,     "ram-base"},
    { VMI_CALL    , "_call"},
    { VMI_JUMP    , "_jump"},
    { VMI_LIT     , "_lit"},
    { VMI_ZBRAN   , "_0bran"},
    { VMI_BRAN    , "_bran"},
    { VMI_PBRAN   , "_pbran"},
    { VMI_RCALL   , "_rcall"},
    { VMI_NEXT    , "_next"},
    { VMI_PFX     , "_pfx"},
    { VMI_USER    , "_user"},
    { VMI_QLIT    , "_qlit"},
    { VMI_API0    , "_api0"},
    { VMI_API1    , "_api1"},
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
/* FORTH SEARCH CONTEXT STRUCTURES                                           */
/* ========================================================================= */

struct s_wid wids[WIDS_MAX] = { // wordlists
    [0] = {.head = &forth_heads[(sizeof(forth_heads) / sizeof(s_head)) - 1],
           .name = "`forth" },
    [1] = {.head = &only_heads[(sizeof(only_heads) / sizeof(s_head)) - 1],
           .name = "`only" }
};

static int wids_pointer = 2;

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
        serial_puts(link->name);
        lfSpace();
        link = link->link;
    }
    return 0;
}

/* ========================================================================= */
/* LOOKUP FUNCTION                                                           */
/* ========================================================================= */

static const struct s_head* search_wordlist(int wid_index, const char *target_name) {
    if (wid_index < 0 || wid_index >= wids_pointer) {
        return NULL;
    }
    const struct s_head *link = wids[wid_index].head;
    while (link != NULL) {
        if (TheStringsMatch(link->name, (char *)target_name)) {
            return link;
        }
        link = link->link;
    }
    return NULL;
}

/* ========================================================================= */
/* FORTH CONTEXT SEARCH WRAPPER                                              */
/* ========================================================================= */

/**
 * Iterates through the active wordlists defined in the 'context' array.
 * Replicates the behavior of traditional Forth text interpreters.
 */
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

static void lfDotLinecount(void) {
    lfCR();
    serial_puts("Line ");
    lfDot(linecount);
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

static int char2digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return -1; // Invalid character for a digit
}


/* =========================================================================
>IN and BLK are the top values of an 8-deep internal block stack. They are
used to manage the input buffer and block number for file-based input.
TIB is a fixed buffer in Forth data space for terminal input.
========================================================================= */

#define TERMINAL_OVERFLOWED 0x8000

static int loadTIB(void) {
    char *tib = (char*)TIB; // reset the TIB pointer
	int remaining = TIBSIZE; // remaining space in TIB
    int aux_result = 0;

    if (lfTIBSTATEfetch()) {
        // Announce to Forth that the terminal is waiting for TIBSTATE = 2
        lfTIBSTATEstore(1);
    }
    if (BLK == 0) { // Load the TIB with keyboard input
        while (1) {
            if (serial_ready() < 1) {
#ifdef yield2c
                yield2c(); // Yield to other tasks (ans step VM) while waiting...
#endif
                continue;
            }
            int c = serial_getc();
            if (c == EOF) break;        // treat EOF as a normal terminator
            if (c == '\r') {
                if (system_flags & SYS_FLAG_IGNORE_CR) continue;
                break;
            }
            if (c == '\n') break;       // CRLF inserts lines
            if (remaining > 1) {
                *tib++ = (char)c;
                remaining--;
            }
            else {                      // ignore input remaining until EOL
                aux_result = TERMINAL_OVERFLOWED;
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
	return 0; // For now, we only handle keyboard input (BLK == 0)
}

/*
parseStr assumes `str` is always correctly terminated with a null character.
It extracts the next terminator-delimited token from `str` and returns a pointer
to the next character after the token. If no token is found, it returns NULL.
The extracted token is stored in a static buffer for later use.
*/

static char TOINchar(void) {
    return source[lfTOINfetch()];
}

static void TOINbump(void) {
    lfTOINstore(1 + lfTOINfetch());
}

static int parseWord(void) {
    int ior = 0;
    // Skip leading whitespace
    while(1) {
        char c = TOINchar();
        if (c == '\0') break;
        if (c != ' ') break;
        TOINbump();
    }
	// Copy characters into the token buffer
    int i = 0;
    char c;
    while(1) {
		c = TOINchar();
        if (c == '\0') break;
        if (c == ' ') break;
		token[i++] = c;
        if (i >= (int)sizeof(token) - 1) {
            ior = ERR_PARSED_STRING_OVERFLOW;
            break; // Prevent buffer overflow
        }
        TOINbump();
    }
	// Terminate the token string
    token[i] = 0;
    // skip the blank delimiter
    if (c != '\0') TOINbump();
    return ior;
}

/* HEADER  ( w aux <name> -- ) */
int lfAPI_header(void) {
    int ior = parseWord();
    if (ior) return ior;

    // 1. Pop aux and w off the Forth stack (TOS = aux, NOS = w)
    uint32_t aux = (uint32_t)vmPop();
    uint32_t w = (uint32_t)vmPop();

    // 2. Resolve destination memory location in 32-bit cells
    int32_t* headptr = &vm_memory[RAM_PAGE][F_PTRS];
    int32_t  f_hp = headptr[2];
    int32_t  f_hmax = headptr[5];
    int page = f_hp >> (22 - VM_LOG2_PAGES);
    int32_t  start_f_hp = f_hp & 0x3FFFFF;

    int32_t* cell_dest = &vm_memory[page][start_f_hp];

    // 3. Pack string name into 32-bit cells
    char* name_dest = (char*)cell_dest;
    int32_t ch_dest = start_f_hp | 0x40000000; // bytes
    char* src = token;
    char c = 0;
    uint8_t length = 0;

    do {
        int ior = vmStore(ch_dest, *src++);
        if (ior) return ior;
        ch_dest = vmCharPlus(ch_dest);
        length++;
    } while (c);
    /*
    * vmStore did not give an error, assume struct s_head will not step on
    * anything critical.
    */
    while (length & 3) {
        vmStore(ch_dest, *src++);
        ch_dest = vmCharPlus(ch_dest);
        length++;
    }

    cell_dest += (length / sizeof(int32_t));

    // 4. Construct struct s_head header directly in the next cell boundary
    struct s_head* target_head = (struct s_head*)cell_dest;
    target_head->name = name_dest;
    target_head->w = w;
    target_head->aux = aux;

    // 5. Insert header at top of current wordlist (linked list)
    s_wid* current = &wids[*CURRENT];
    target_head->link = (struct s_head*)current->head; // Point new node to current head

    // 6. Update current wordlist head pointer
    current->head = target_head;

    // Advance cell_dest past the struct s_head
    int32_t head_cells = (sizeof(struct s_head) + sizeof(int32_t) - 1) / sizeof(int32_t);
    cell_dest += head_cells;

    // 7. Calculate new f_hp address (start_f_hp + total cell delta)
    int32_t total_cells_used = (int32_t)(cell_dest - &vm_memory[page][start_f_hp]);
    int32_t new_f_hp = (page << (22 - VM_LOG2_PAGES)) | ((start_f_hp + total_cells_used) & 0x3FFFFF);

    // 8. Bounds check before writing back to F_PTRS
    if (new_f_hp >= f_hmax) {
        return ERR_DICTIONARY_OVERFLOW;
    }

    headptr[2] = new_f_hp;
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
 * Attempts to interpret a raw token text as a numeric literal.
 * Returns ior and sets the global `value` to the parsed number.
 * It sets DPL to the number of digits after the decimal point if a decimal
 * point is present, leaves it at -1 otherwise. Blame: Gemini
 */
static int parseNumber(int base) {
    int dpl = -1; // -1 indicates no decimal point was encountered
    value = 0;
    int i = 0;
    int sign = 0;
    int has_digits = 0;
    int has_dpl = 0;

    // Fast-fail empty tokens
    if (token[0] == '\0') return ERR_UNDEFINED_WORD; // -13

    while (1) {
        char c = token[i++];
        if (c == '\0') break;

        // Handle leading minus sign
        if (i == 1 && c == '-') {
            sign = 1;
            continue;
        }

        // Handle decimal point '.'
        if (c == '.') {
            has_dpl = 1;
            dpl = 0;
            continue;
        }

        int digit = char2digit(c);

        // Guard against invalid characters or digits >= base
        if (digit < 0 || digit >= base) {
            return ERR_UNDEFINED_WORD;
        }

        has_digits = 1;
        value = (value * base) + digit;

        // Increment DPL for every valid digit parsed after '.'
        if (has_dpl) {
            dpl++;
        }
    }

    // Must have contained at least one actual digit
    if (!has_digits) {
        return ERR_UNDEFINED_WORD;
    }

    if (sign) {
        value = -value;
    }

    vmStore(LF_DPL, dpl);
    return 0; // Success
}

/**
 * Forth Text Interpreter for a buffer anywhere in memory
 * The character just after the end of the buffer is set to \0 as a backstop.
 * Make sure the buffer has room for that.
 *
 * @param str Character stream to interpret.
 * @param len Stream length.
 * @return    0 on normal execution, else Forth error code from errcodes.h
 */
static int interpret(char* str, size_t len) {
    if (str == NULL || len == 0) {
        return 0;
    }
    str[len] = '\0';                    // backstop the buffer 
    source = str;                       // use str for TOINchar
    lfTOINstore(0);
    int ior = 0;
    while (TOINchar() != 0) {
        if (ior) return ior;
        ior = parseWord();              // Parse the next token delimited by space
		if (ior) return ior;
        // A. Check the current active vocabulary lists
        const struct s_head* word = search_context(token);
        if (token[0] != '\0') {         // ignore empty strings
            int state = lfSTATEfetch();
            if (word != NULL) {         // word was found in the dictionary
                if (word->aux & IS_IMMEDIATE) {
                    state = 0;
                }
                if (state) {
                    ior = compile_word(word);
                }
                else {
                    ior = execute_word(word);
                }
                int base = lfBASEfetch();
                if (base < 2) {         // safety-check the base
                    ior = ERR_INVALID_BASE;
                    lfBASEstore(10);
                }
                continue;
            }
            ior = findConstant(token, &value);
            if (ior) {                  // Fall back to numeric evaluation
                ior = parseNumber(lfBASEfetch());
            }
            if (ior) return ior;
            if (state) {
                lfCompileLit(value);
            }
            else {
                vmPush(value);
            }
        }
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
        BLK = 0;
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
        }
		// handle the ior here if needed (e.g., exit on BYE)
        LF_PACKEDSTATE[0] = 10;
        switch (ior) {
        case ERR_QUIT: return 0;
        case ERR_UNDEFINED_WORD:
            serial_puts(token);
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

// API calls that need variables in this file, which we'd rather not export.

/* `D'` */
int lfAPI_tickx(void) {
    int ior = parseWord();
    if (ior) return ior;
    const struct s_head* word = search_context(token);
    if (word == NULL) return ERR_UNDEFINED_WORD;
    vmPush(word->w);
    return vmPush(word->aux);
}

