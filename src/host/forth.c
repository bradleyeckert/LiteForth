#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include <stdio.h> // remove for production code

#define DOT_S_MAX    8  // maximum depth to display in .s
#define IS_PRIMITIVE 0x80000000
#define IS_MACRO     0xC0000000
#define IS_CONSTANT  0x10000000
#define UOP(val) (IS_PRIMITIVE | VM_UOPS | ((val) << 9) )
#define MACRO(s0, s1, s2) (IS_MACRO | VM_UOPS | ((s0) << 9)| ((s1) << 4)| (s2) )
#define DATA(idx) ((RAM_PAGE << (22 - VM_SEGMENT_BITS)) + (idx))
#define API0(idx) (IS_PRIMITIVE | VMI_API0 | (idx))

static uint32_t system_flags = 0;

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
    { LINK(13), "r@",       UOP(VMU_R),                             0x20E},
    { LINK(14), "r>",       UOP(VMU_POP),                           0x20F},
    { LINK(15), "2/c",      UOP(VMU_TWODIVC),                       0x210},
    { LINK(16), "2/",       UOP(VMU_TWODIV),                        0x211},
    { LINK(17), "!a",       UOP(VMU_STOREA),                        0x212},
    { LINK(18), "!a+",      UOP(VMU_STOREAPLUS),                    0x213},
    { LINK(19), "!b",       UOP(VMU_STOREB),                        0x214},
    { LINK(20), "!b+",      UOP(VMU_STOREBPLUS),                    0x215},
    { LINK(21), "swap",     UOP(VMU_SWAP),                          0x216},
    { LINK(22), "+*",       UOP(VMU_PLUSSTAR),                      0x217},
    { LINK(23), "b",        UOP(VMU_B),                             0x218},
    { LINK(24), "b!",       UOP(VMU_BSTORE),                        0x219},
    { LINK(25), "@b",       UOP(VMU_FETCHB),                        0x21A},
    { LINK(26), "@b+",      UOP(VMU_FETCHBPLUS),                    0x21B},
    { LINK(27), "a",        UOP(VMU_A),                             0x21C},
    { LINK(28), "cy",       UOP(VMU_CY),                            0x21D},
    { LINK(29), "base",     DATA(F_BASE),           IS_CONSTANT |   0x21E},
    { LINK(30), "state",    DATA(F_STATE),          IS_CONSTANT |   0x21F},
    { LINK(31), "dpl",      DATA(F_DPL),            IS_CONSTANT |   0x220},
    { LINK(32), ">in",      DATA(F_TOIN),           IS_CONSTANT |   0x221},
    { LINK(33), "blk",      DATA(F_BLK),            IS_CONSTANT |   0x222},
    { LINK(34), "tib",      DATA(F_BASE),           IS_CONSTANT |   0x223},
    { LINK(35), "sectors",  VM_SEGMENTS,            IS_CONSTANT |   0x224},
    { LINK(36), "2dup",     MACRO(VMU_OVER,VMU_OVER,VMU_NOP),       0x225},
    { LINK(37), "!",        MACRO(VMU_BSTORE,VMU_STOREB,VMU_NOP),   0x226},
    { LINK(38), "@",        MACRO(VMU_BSTORE,VMU_FETCHB,VMU_NOP),   0x227},
    { LINK(39), "nip",      MACRO(VMU_SWAP,VMU_DROP,VMU_NOP),       0x228},
    { LINK(40), "emit",     API0(1),                                0x229},
    { LINK(41), ".",        API0(2),                                0x22A},
    { LINK(42), "sector",   API0(3),                                0x22B},
    { LINK(43), "um*",      API0(4),                                0x22C},
    { LINK(44), "m*",       API0(5),                                0x22D},
    { LINK(45), "}t",       API0(6),                                0x230},
    { LINK(46), "->",       API0(7),                                0x231},
    { LINK(47), "t{",       API0(8),                                0x232},
    { LINK(48), ">options", API0(9),                                0x233},
    { LINK(49), "options>", API0(10),                               0x234},
    { LINK(50), "(",        API0(11),                               0x235},
    { LINK(51), ".(",       API0(12),                               0x236},
    { LINK(52), "cr",       API0(13),                               0x237},
};

struct s_wid wids[WIDS_MAX] = {// wordlists
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

static int serial_puts(const char* s) {
    int ior = 0;
    while (*s) {
        ior = serial_putc(*s++);
        if (ior) return ior;
    }
    return 0;
}


int lfDotB(int32_t val, int base, int dpl) {
    char buf[36] = { 0 }; // Enough for 32-bit integer
    char* p = &buf[sizeof(buf)];
    *--p = 0; // Null terminator
    if (val < 0) {
        val = -val;
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
        if (dpl) {
            if (--dpl == 0) {
                *--p = '.';
            }
        }
    } while (val | dpl);
	int result = serial_puts(p);
    if (BASE == 16) {
        result = serial_putc('H');
	}
    return result;
}

static int lfCR(void) {
    if (CR_IS_CRLF) {
        serial_putc('\r');
    }
    return serial_putc('\n');
}

static int lfDot(int32_t val) {
    lfDotB(val, BASE, 0);
    return serial_putc(' ');
}

static void lfDotLinecount(void) {
    lfCR();
    serial_puts("Line ");
    lfDot(LINECOUNT);
}

static int lfDotS(void) {
    uint8_t depth = (uint8_t)vmPeek(VM_REG_depth);
    if (depth) {
        serial_puts("( ");
        if (depth > DOT_S_MAX) {
            serial_putc('[');
            lfDotB(depth, BASE, 0);
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
    return 100; // Invalid character for a digit
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

    if (TIBSTATE) {
        // Announce to Forth that the terminal is waiting for TIBSTATE = 2
		TIBSTATE = 1;
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
        if (TIBSTATE) {
            TIBSTATE = 2; // Indicate that TIB is ready for processing
#ifdef yield2c
            while (TIBSTATE != 3) {
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
    return source[TOIN];
}

static void TOINbump(void) {
    TOIN++;
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
    DPL = -1; // -1 indicates no decimal point was encountered
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
            DPL = 0;
            continue;
        }

        int digit = char2digit(c);

        // Guard against invalid characters or digits >= base
        if (digit < 0 || digit >= base) {
            return ERR_UNDEFINED_WORD; // -13[cite: 1]
        }

        has_digits = 1;
        value = (value * base) + digit;

        // Increment DPL for every valid digit parsed after '.'
        if (has_dpl) {
            DPL++;
        }
    }

    // Must have contained at least one actual digit
    if (!has_digits) {
        return ERR_UNDEFINED_WORD; // -13[cite: 1]
    }

    if (sign) {
        value = -value;
    }

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
    TOIN = 0;
    int ior = 0;
    while (TOINchar() != 0) {
        if (ior) return ior;
        ior = parseWord();              // Parse the next token delimited by space
		if (ior) return ior;
        // A. Check the current active vocabulary lists
        const struct s_head* word = search_context(token, CASE_SENSITIVE);
        if (token[0] != '\0') {         // ignore empty strings
            if (word != NULL) {         // word was found in the dictionary
                if (STATE) {
                    ior = execute_word(word);
                }
                else {
                    ior = execute_word(word);
                }
                int base = BASE;        // safety-check the base
                if (base < 2 || base > 36) {
                    ior = ERR_INVALID_BASE;
                    BASE = 10;
                }
                continue;
            }
            ior = parseNumber(BASE);    // Fall back to numeric evaluation
            if (ior) return ior;
            if (STATE) {                // valid number
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
    serial_puts(u8"幸运狐 v0");
    lfDotB(TF_VERSION, 10, 2);
    lfCR();
    while (1) {
        BASE = 10;
        STATE = 0;
        DPL = 0;
        TOIN = 0;
        BLK = 0;
        LINECOUNT = 0;
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
            LINECOUNT++;
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

static int APIbye(void) {
    return ERR_QUIT;
}

static int APIemit(void) {
    return serial_putc((char)vmPeek(-1));
}

static int APIdot(void) {
    return lfDot(vmPeek(-1));
}

static int APIsegment(void) {
    int32_t val = vmPeek(0);
    val = val << (22 - VM_SEGMENT_BITS);
    return vmPoke(0, val);
}

static int API_umstar_x(int sign) {
    uint64_t a = (uint64_t)vmPeek(0);
    uint64_t b = (uint64_t)vmPeek(1);
    int invert = 0;
    if (sign) {
        invert = (a ^ b) & 0x80000000;
        if (a & 0x80000000) a++;
        if (b & 0x80000000) b++;
    }
    uint64_t p = a * b;
    if (invert) {
        p = -(signed)p;
    }
    vmPoke(1, (int32_t)p);
    vmPoke(0, (int32_t)(p >> 32));
    return 0;
}

static int API_umstar(void) {
    return API_umstar_x(0);
}

static int API_mstar(void) {
    return API_umstar_x(1);
}

// Assertion tests for Forth words and primitives
// T{ 0 0 AND -> 0 }T

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

static int APIparenX(int echo) {
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

static int APIparen(void) {
    return APIparenX(0);
}

static int APIdotParen(void) {
    return APIparenX(1);
}

typedef int(*APIfn) (void);

static const APIfn API0fns[] = {
    APIbye, APIemit, APIdot, APIsegment, API_umstar, API_mstar,
    APIendTest, APIdoTest, APIbeginTest, APIsetFlags, APIgetFlags,
    APIparen, APIdotParen, lfCR

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

