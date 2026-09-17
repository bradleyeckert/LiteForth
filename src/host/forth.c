#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include <stdio.h> // remove for production code
// cd /mnt/c/Users/User/Documents/GitHub/LiteForth
#define IS_PRIMITIVE 0x80000000
#define IS_MACRO     0xC0000000
#define IS_IMMEDIATE 0x40000000
//#define IS_IMM_ONLY  0x20000000
#define IS_CONSTANT  0x10000000
#define UOP(val) (IS_PRIMITIVE | VM_UOPS | ((val) << 9) )
#define MACRO(s0, s1, s2) (IS_MACRO | VM_UOPS | ((s0) << 9)| ((s1) << 4)| (s2) )
#define DATA(idx) ((RAM_PAGE << (22 - VM_LOG2_PAGES)) + (idx))
#define API0(idx) (IS_PRIMITIVE | VMI_API0 | (idx))

#if (FAT_FORTH & 1)
#include "utils.h"
#endif

static uint32_t system_flags = 0;
static uint32_t linecount = 0;

int lfBASEfetch(void) {
    int32_t result;
    vmFetch(LF_BASE, &result);
    return result;
}

int lfBASEstore(int base) {
    return vmStore(LF_BASE, base);
}

int lfSTATEfetch(void) {
    int32_t result;
    vmFetch(LF_STATE, &result);
    return result;
}

int lfSTATEstore(int compiling) {
    return vmStore(LF_STATE, compiling);
}

int lfTOINfetch(void) {
    int32_t result;
    vmFetch(LF_TOIN, &result);
    return result;
}

int lfTOINstore(int position) {
    return vmStore(LF_TOIN, position);
}

int lfTIBSTATEfetch(void) {
    int32_t result;
    vmFetch(LF_TIBSTATE, &result);
    return result;
}

int lfTIBSTATEstore(int state) {
    return vmStore(LF_TIBSTATE, state);
}

int lfPAGEfetch(void) {
    int32_t result;
    vmFetch(LF_PAGE, &result);
    return result;
}


static int vmPush(int32_t value) {
    return vmPoke(-1, value);;
}

static int execute_word(const struct s_head* word) {
    if (word->aux & IS_CONSTANT) {
		return vmPush(word->w);
	}
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

static int compile_word(const struct s_head* word) {
    printf("Compiling word: %s (w=%x, aux=%x)\n", word->name, word->w, word->aux);
    return 0;
}


#define LINK(val) (struct s_head*)&lf_heads[(val)]

// Flash cost per entry: 16 bytes plus name string (length+1 bytes).
// 64 entries is about 1.4 KB
static const struct s_head lf_heads[] = {  
    { NULL,     "bye",      API0(0),                                0x200},
    { LINK( 0), "inv",      UOP(VMU_INV),                           0x201},
    { LINK( 1), "over",     UOP(VMU_OVER),                          0x202},
    { LINK( 2), "a!",       UOP(VMU_ASTORE),                        0x203},
    { LINK( 3), "xor",      UOP(VMU_XOR),                           0x204},
    { LINK( 4), "+",        UOP(VMU_PLUS),                          0x205},
    { LINK( 5), "and",      UOP(VMU_AND),                           0x206},
    { LINK( 6), ">r",       UOP(VMU_PUSH),                          0x207},
    { LINK( 7), "unext",    UOP(VMU_UNEXT),                         0x208},
    { LINK( 8), "2*",       UOP(VMU_TWOSTAR),                       0x209},
    { LINK( 9), "dup",      UOP(VMU_DUP),                           0x20A},
    { LINK(10), "drop",     UOP(VMU_DROP),                          0x20B},
    { LINK(11), "@a",       UOP(VMU_FETCHA),                        0x20C},
    { LINK(12), "@a+",      UOP(VMU_FETCHAPLUS),                    0x20D},
    { LINK(13), "@as",      UOP(VMU_FETCHASIGN),                    0x20D},
    { LINK(14), "r@",       UOP(VMU_R),                             0x20E},
    { LINK(15), "r>",       UOP(VMU_POP),                           0x20F},
    { LINK(16), "2/c",      UOP(VMU_TWODIVC),                       0x210},
    { LINK(17), "2/",       UOP(VMU_TWODIV),                        0x211},
    { LINK(18), "!a",       UOP(VMU_STOREA),                        0x212},
    { LINK(19), "!a+",      UOP(VMU_STOREAPLUS),                    0x213},
    { LINK(20), "!b",       UOP(VMU_STOREB),                        0x214},
    { LINK(21), "!b+",      UOP(VMU_STOREBPLUS),                    0x215},
    { LINK(22), "swap",     UOP(VMU_SWAP),                          0x216},
    { LINK(23), "+*",       UOP(VMU_PLUSSTAR),                      0x217},
    { LINK(24), "b",        UOP(VMU_B),                             0x218},
    { LINK(25), "b!",       UOP(VMU_BSTORE),                        0x219},
    { LINK(26), "@b",       UOP(VMU_FETCHB),                        0x21A},
    { LINK(27), "@b+",      UOP(VMU_FETCHBPLUS),                    0x21B},
    { LINK(28), "a",        UOP(VMU_A),                             0x21C},
    { LINK(29), "cy",       UOP(VMU_CY),                            0x21D},
    { LINK(30), "base",     LF_BASE,                IS_CONSTANT |   0x21E},
    { LINK(31), "state",    LF_STATE,               IS_CONSTANT |   0x21F},
    { LINK(32), "dpl",      LF_DPL,                 IS_CONSTANT |   0x220},
    { LINK(33), ">in",      LF_TOIN,                IS_CONSTANT |   0x221},
    { LINK(34), "blk",      LF_BLK,                 IS_CONSTANT |   0x222},
    { LINK(35), "tib",      LF_TIB,                 IS_CONSTANT |   0x223},
    { LINK(36), "page",     LF_PAGE,                IS_CONSTANT |   0x224},
    { LINK(37), "pages",    VM_MEM_PAGES,           IS_CONSTANT |   0x224},
    { LINK(38), "ram-base", F_HERE0,                IS_CONSTANT |   0x224},
    { LINK(39), "ram-page", RAM_PAGE,               IS_CONSTANT |   0x224},
    { LINK(40), "nope",     0,                      IS_CONSTANT |   0x224},
    { LINK(41), "nope",     0,                      IS_CONSTANT |   0x224},
    { LINK(42), "nope",     0,                      IS_CONSTANT |   0x224},
    { LINK(43), "2dup",     MACRO(VMU_OVER,VMU_OVER,VMU_NOP),       0x225},
    { LINK(44), "!",        MACRO(VMU_ASTORE,VMU_STOREA,VMU_NOP),   0x226},
    { LINK(45), "@",        MACRO(VMU_ASTORE,VMU_FETCHA,VMU_NOP),   0x227},
    { LINK(46), "s@",       MACRO(VMU_ASTORE,VMU_FETCHASIGN,VMU_NOP), 0x227},
    { LINK(47), "nip",      MACRO(VMU_SWAP,VMU_DROP,VMU_NOP),       0x228},
    { LINK(48), "]",        API0(1),                                0x229},
    { LINK(49), "[",        API0(2),                 IS_IMMEDIATE | 0x229},
    { LINK(50), "emit",     API0(3),                                0x229},
    { LINK(51), ".",        API0(4),                                0x22A},
    { LINK(52), "'page",    API0(5),                                0x22B},
    { LINK(53), "um*",      API0(6),                                0x22C},
    { LINK(54), "m*",       API0(7),                                0x22D},
    { LINK(55), "}t",       API0(8),                                0x230},
    { LINK(56), "->",       API0(9),                                0x231},
    { LINK(57), "t{",       API0(10),                               0x232},
    { LINK(58), ">options", API0(11),                               0x233},
    { LINK(59), "options>", API0(12),                               0x234},
    { LINK(60), "(",        API0(13),                IS_IMMEDIATE | 0x235},
    { LINK(61), ".(",       API0(14),                               0x236},
    { LINK(62), "cr",       API0(15),                               0x237},
    { LINK(63), "words",    API0(16),                               0x238},
    { LINK(64), "'",        API0(17),                               0x239},
    { LINK(65), "here",     API0(18),                               0x239},
    { LINK(66), "init-here",API0(19),                               0x239},
    { LINK(67), "heads",    API0(20),                               0x239},
    { LINK(68), "udata",    API0(21),                               0x239},
    { LINK(69), "ccode",    API0(22),                               0x239},
    { LINK(70), "page-end", API0(23),                               0x23A},
    { LINK(71), "page-base",API0(24),                               0x23B},
    { LINK(72), ">bits",    API0(25),                               0x23B},
    { LINK(73), "char+",    API0(26),                               0x23B},
    { LINK(74), ",",        API0(27),                               0x23B},
#if (FAT_FORTH & 1)
    { LINK(75), "hex",      API0(28),                               0x23A},
    { LINK(76), "decimal",  API0(29),                               0x23B},
    { LINK(77), ".page",    API0(30),                               0x23B},
    { LINK(78), ".pages",   API0(31),                               0x23B},
    { LINK(79), "dump",     API0(32),                               0x23B},
    { LINK(80), "dumpi",    API0(33),                               0x23B},
    { LINK(81), "dasm",     API0(34),                               0x23B},
#endif
};

struct s_wid wids[WIDS_MAX] = { // wordlists
    [0] = { .head = &lf_heads[(sizeof(lf_heads) / sizeof(s_head)) - 1],
    .name = "root" }
};

/* ========================================================================= */
/* FORTH SEARCH CONTEXT STRUCTURES                                           */
/* ========================================================================= */

/* CONTEXT array holds indices into the wids array, ordered by search priority.
   Terminated with -1 to indicate the end of the search order. */
int8_t context[CONTEXT_MAX] = { 0, -1 };

/* ========================================================================= */
/* LOOKUP FUNCTION                                                           */
/* ========================================================================= */

/* String comparison does not use strings.h. It terminates when encountering
   a \0 in either string. target_name may end in a blank. */

const struct s_head* search_wordlist(int wid_index, const char *target_name, int case_insensitive) {
    if (wid_index < 0 || wid_index >= WIDS_MAX) {
        return NULL;
    }

    const struct s_head *link = wids[wid_index].head;

    while (link != NULL) {
        const char *s1 = link->name;
        const char *s2 = target_name;
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

        if ((c1 | c2) == 0) {
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
const struct s_head* search_context(const char *target_name, int case_insensitive) {
    for (int i = 0; i < CONTEXT_MAX; i++) {
        int wid_idx = context[i];

        // Stop searching if we hit the end of the defined context order (-1)
        if (wid_idx == -1) {
            break;
        }

        const struct s_head *found = search_wordlist(wid_idx, target_name, case_insensitive);
        if (found != NULL) {
            return found; // Return immediately upon first match in priority order
        }
    }
    return NULL; // Word not found in any active wordlist
}

#include <ctype.h>
#include <string.h>

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
        if (dpl) {
            if (--dpl == 0) {
                *--p = '.';
            }
        }
    } while (val | dpl | (digits > 0));
	int result = serial_puts(p);
    return result;
}

int lfCR(void) {
    if (CR_IS_CRLF) {
        serial_putc('\r');
    }
    return serial_putc('\n');
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

static void lfDotLinecount(void) {
    lfCR();
    serial_puts("Line ");
    lfDot(linecount);
}

static int lfDotS(void) {
    uint8_t depth = (uint8_t)vmPeek(VM_REG_depth);
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
#define TERMINAL_CLOSED 0x8000

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
            if (serial_ready() == 0) {
#ifdef yield2c
                yield2c(); // Yield to other tasks (ans step VM) while waiting...
#endif
                continue;
            }
            int c = serial_getc();
            if (c == EOF) { aux_result = TERMINAL_CLOSED; break; }
			if (c == '\r') { continue; }
            if (c == '\n') { break; }
			*tib++ = (char)c;
            remaining--;
			if (remaining <= 1) { break; } // Leave space for null terminator
        }
        if (remaining--) {
            *tib++ = 0; // Null-terminate
        }
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

static char token[32]; // token buffer for parsing
static char* source = NULL; // pointer to the current position in the input string
static int32_t value; // value returned by numeric input parsing

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
            ior = ERR_DEFINITION_TOO_LONG;
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
int interpret(char* str, size_t len) {
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
        const struct s_head* word = search_context(token, CASE_SENSITIVE);
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
            ior = parseNumber(lfBASEfetch());    
            if (ior) return ior;        // Fall back to numeric evaluation
            if (state) {
                printf("Compiling number %d\n", value);
            }
            else {
                vmPush(value);
            }
        }
    }
    return ior;
}

// `serial_open` before you call QUIT
int QUIT(void) {
    serial_puts(u8"幸运狐 v");
    lfDotB(TF_VERSION, 10, 2, 3);
    lfCR();
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
            if ((system_flags & SYS_FLAG_VALIDATION) == 0) {
                lfDotS();
                ior = serial_puts("ok>");
                if (ior) break; // lost output channel
            }
            linecount++;
            int len = loadTIB();
            if (system_flags & SYS_FLAG_VERBOSE) {
                lfDotLinecount();
                serial_puts(TIB);
            }
            ior = interpret((char*)TIB, len & 0x7FFF);
            int depth = vmPeek(VM_REG_depth);
            if (depth >= STACK_MASK) ior = ERR_STACK_OVERFLOW;
            else if (depth < 0) ior = ERR_STACK_UNDERFLOW;
            if (len & TERMINAL_CLOSED) ior = ERR_QUIT;
        }
		// handle the ior here if needed (e.g., exit on BYE)
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

/*==========================================================================
* API 0 (internal)
* Functions that access the stack must be in this file.
*=========================================================================*/

char* vm_memory_name[VM_MEM_PAGES] = { NULL };

struct s_dictptr dictptr[VM_MEM_PAGES];
static int memory_space = 0; // code, data, header

static int APIcodeSpace(void) {
    memory_space = 0;
    return 0;
}

static int APIdataSpace(void) {
    memory_space = 1;
    return 0;
}

static int APIheadSpace(void) {
    memory_space = 2;
    return 0;
}

int lfUnused(int type, int page) {
    if (page >= VM_MEM_PAGES) return 0;
    uint32_t base = page << (22 - VM_LOG2_PAGES);
    s_dictptr* dict = &dictptr[page];
    uint32_t a0 = dict->cp;
    uint32_t a1 = dict->hp0 << 4;
    switch (type) {
    case 1: a1 = dict->dp0 << 4;  a0 = dict->hp;  break;
    case 2: a1 = vm_memory_rd_limit[page];  a0 = dict->dp;  break;
    default: break;
    }
    if (a0 & 0x07C00000) a0++; // bitfields used?
    return (base + a1) - (0x3FFFFF & a0);
}

/* PAGE-BASE  ( -- bottom_addr )
* NOTE: PAGE-BASE and PAGE-END are likely going away.
* PAGE @ 'PAGE does the same thing as PAGE-BASE,
* and PAGE-END is not useful. 
*/
static int APIpageBase(void) { // base address of the current page
    uint32_t current_page = lfPAGEfetch();
    int32_t res = current_page << (22 - VM_LOG2_PAGES);
    return vmPoke(-1, res);
}

/* UNUSED */
static int APIunused(void) {
    uint32_t current_page = lfPAGEfetch();
    int32_t res = lfUnused(memory_space, current_page);
    return vmPoke(-1, res);
}

/*
 * Sets up the current page using `offset codesize headsize init_here`, where
 * - offset is the amount of data before code space
 * - codesize is the amount of code space below header space
 * - headsize is the amount of header space below data space
 */
static int APIinitHere(void) { // the `init-here` ( offset code head -- ) function
    int32_t headsize = vmPeek(-1);
    int32_t codesize = vmPeek(-1);
    int32_t offset = vmPeek(-1);
    uint32_t page = lfPAGEfetch();
    if (page >= VM_MEM_PAGES) return ERR_INVALID_MEMORY_PAGE;
    int32_t base = page << (22 - VM_LOG2_PAGES);
    s_dictptr* dict = &dictptr[page];
    dict->cp = base + offset + (16 << 27);   // cp is array of 16-bit values at 1
    offset = (offset + codesize + 15) & 0xFFFFFFF0; // 16-cell-align head space
    dict->hp = base + offset;
    dict->hp0 = offset >> 4;
    offset = (offset + headsize + 15) & 0xFFFFFFF0; // 16-cell-align data space
    dict->dp = base + offset;
    dict->dp0 = offset >> 4;
    return 0;
}

/*
 * Gets the HERE for a given memory space
 */
static uint32_t lfHereC(int space) {
    uint32_t current_page = lfPAGEfetch();
    if (current_page >= VM_MEM_PAGES) return 0;
    s_dictptr* dict = &dictptr[current_page];
    int32_t  res = dict->cp;
    switch (space) {
    case 1:  res = dict->hp;  break;
    case 2:  res = dict->dp;  break;
    default: break;
    }
    return res;
}

/* HERE */
static int APIhere(void) {
    vmPoke(-1, lfHereC(memory_space));
    return 0;
}

/*
 * Sets the HERE for a given memory space
 */
static int lfOrgC(int space, uint32_t addr) {
    uint32_t current_page = lfPAGEfetch();
    if (current_page >= VM_MEM_PAGES) return 0;
    if ((addr & 0x3FFFFF) >= vm_memory_rd_limit[current_page]) { 
        return ERR_DICTIONARY_OVERFLOW;
    }
    s_dictptr* dict = &dictptr[current_page];
    switch (space) {
    case 0:  dict->cp = addr;  break;
    case 1:  dict->hp = addr;  break;
    case 2:  dict->dp = addr;  break;
    default: break;
    }
    return 0;
}

/*
 * Bit field `char+`
 */
static int APIcharPlus(void) {
    int32_t addr = vmPeek(0);
    return vmPoke(0, vmCharPlus(addr));
}

/*
 * `>bits` ( n -- )
 * Modify the current HERE to address bit fields with a width
 * between 1 and 32 bits.
 */
static int APItoBits(void) {
    int bits = (int)vmPeek(-1);
    uint32_t addr = lfHereC(memory_space);
    int bshift = (addr >> 22) & 0x1F;
    // check if the next char+ would cross a cell boundary
    if ((bshift + bits) > 32) { 
        addr = (addr & ~(0x1F << 22)) + 1;
    }
    // change the bitfield size
    addr = (addr & ~(0x1F << 27)) | (bits << 27);
    return lfOrgC(memory_space, addr);
}

static int vmComma(int space, int32_t data) {
    int32_t addr = lfHereC(space);
    vmStore(addr, data);
    addr = vmCharPlus(addr);
    return lfOrgC(space, addr);
}

/* `,` */
static int APIcomma(void) {
    return vmComma(memory_space, vmPeek(-1));
}

/* `[` */
static int APIbracketOpen(void) {
    return lfSTATEstore(0);
}

/* `]` */
static int APIbracketClose(void) {
    return lfSTATEstore(1);
}

/* `'` */
static int APItick(void) {
    int ior = parseWord();
    if (ior) return ior;
    const struct s_head* word = search_context(token, CASE_SENSITIVE);
    if (word == NULL) return ERR_UNDEFINED_WORD;
    return vmPoke(-1, word->w);
}

/* BYE */
static int APIbye(void) {
    return ERR_QUIT;
}

/* EMIT */
static int APIemit(void) {
    return serial_putc((char)vmPeek(-1));
}

/* `.` */
static int APIdot(void) {
    return lfDot(vmPeek(-1));
}

/* WORDS */
static int APIwords(void) {
    const struct s_head* link = wids[0].head;
    while (link != NULL) {
        serial_puts(link->name);
        lfSpace();
        link = link->link;
    }
    return 0;
}

/* `'PAGE` */
static int APIpage(void) {
    int32_t val = vmPeek(0);
    val = val << (22 - VM_LOG2_PAGES);
    return vmPoke(0, val);
}

static int API_umstar_x(int sign) {
    uint32_t a = (uint32_t)vmPeek(0);
    uint32_t b = (uint32_t)vmPeek(1);
    int invert = 0;
    if (sign) {
        invert = (a ^ b) & 0x80000000;
        if (a & 0x80000000) a = 0 - a;
        if (b & 0x80000000) b = 0 - b;
    }
    uint64_t p = (uint64_t)a * (uint64_t)b;
    if (invert) {
        p = 0 - p;
    }
    vmPoke(1, (int32_t)(p & 0xFFFFFFFF));
    vmPoke(0, (int32_t)(p >> 32));
    return 0;
}

/* UM* */
static int API_umstar(void) {
    return API_umstar_x(0);
}

/* M* */
static int API_mstar(void) {
    return API_umstar_x(1);
}

/*
 * Assertion tests for Forth words
 * 
 * From the John Hayes test suite, for example:
 * T{ 0 0 AND -> 0 }T
 */

static uint8_t sp0;
static uint8_t actual_sp;
static int32_t expected_results[STACK_CAPACITY];

static int APIbeginTest(void) { // t{
    sp0 = vmPeek(VM_REG_sp);
    return 0;
}

static int APIdoTest(void) { // ->
    actual_sp = vmPeek(VM_REG_sp);
    int i = actual_sp;
    if (i >= STACK_MASK) return ERR_STACK_OVERFLOW;
    while (i--) {
        expected_results[i] = vmPeek(i);
    }
    vmPoke(VM_REG_sp, sp0);
    return 0;
}

static int APIendTest(void) { // }t
    if (actual_sp != vmPeek(VM_REG_sp)) {
        return ERR_WRONG_NUM_RESULTS;
    }
    int i = actual_sp;
    if (i >= STACK_MASK) return ERR_STACK_OVERFLOW;
    while (i--) {
        if (expected_results[i] != vmPeek(i)) {
            return ERR_WRONG_RESULTS;
        }
    }
    vmPoke(VM_REG_sp, sp0);
    vmPoke(VM_REG_depth, sp0);
    return 0;
}

/*
 * Option flags: 
 *
 * `>options` ( flags -- )
 * `options>` ( -- flags )
 * 
 * Bit 0 = Verification: Quit the app upon the first error
 * Bit 1 = Verbose: Echo the input line
 */
static int APIgetFlags(void) {
    return vmPoke(-1, system_flags);
}

static int APIsetFlags(void) {
    int32_t val = vmPeek(-1);
    if ((system_flags & SYS_FLAGS_LOCKED) == 0) {
        system_flags = val;
    }
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
static int APIparen(void) {
    return lfParenthesis(0);
}

/* .( */
static int APIdotParen(void) {
    return lfParenthesis(1);
}

typedef int(*APIfn) (void);

static const APIfn API0fns[] = {
    APIbye, APIbracketClose, APIbracketOpen, APIemit, APIdot, 
    APIpage, API_umstar, API_mstar, APIendTest, APIdoTest, 
    APIbeginTest, APIsetFlags, APIgetFlags, APIparen, APIdotParen, 
    lfCR, APIwords, APItick, APIhere, APIinitHere, 
    APIheadSpace, APIdataSpace, APIcodeSpace, APIunused, APIpageBase,
    APItoBits, APIcharPlus, APIcomma
#if (FAT_FORTH & 1)
    , lfAPIhex, lfAPIdecimal, lfAPIdotPage, lfAPIdotPages, lfAPIdump
    , lfAPIdumpIns, lfAPIdasm
#endif

    /*, API_NVMbeginWrite, API_NVMread, API_NVMwrite, // 0
    API_NVMendRW, API_Emit, API_umstar, API_mudivmod,               // 4
    API_LCDraw, API_LCDparmSet, API_LCDparm, API_LCDchar,           // 8
    API_LCDcharWidth, API_LCDfill, API_Milliseconds, API_Buttons,   // C
    API_CRC32, API_NVMID         // 10
    */
};

#define API0fs ((int)(sizeof(API0fns)/sizeof(API0fns[0])))

int VMapi0Call(int fn) {
    if (fn < API0fs) {
        return API0fns[fn]();
    }
    return ERR_INVALID_API_CALL;
}


/*
* API 1 (external)
* This would use an execution table at a known address.
*/

int VMapi1Call(int fn) {
    (void)fn;
    return ERR_INVALID_API_CALL;
}

//
//VMcell_t API_mudivmod(vm_ctx* ctx) {
//    /* MU/MOD ( dividendL dividendH divisor -- rem ql qh ) */
//    VMdblcell_t dividend = ((VMdblcell_t)(ctx->n & VM_MASK) << VM_CELLBITS) | (THIRD & VM_MASK);
//    VMdblcell_t divisor = (VMdblcell_t)(ctx->t & VM_MASK);
//    VMdblcell_t q = dividend / divisor;
//    THIRD = (VMcell_t)(dividend % divisor);
//    ctx->n = (VMcell_t)(q & VM_MASK);
//    return (VMcell_t)(q >> VM_CELLBITS) & VM_MASK;
//}

