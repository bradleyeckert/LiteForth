#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include <stdio.h>

#define INST(val) (0x8000 | val) 

static const struct s_head lf_heads[] = { 
    { NULL,                         "BYE",  INST(VMU_DUP), 0x200},
    { (struct s_head*)&lf_heads[ 0], "inv",  INST(VMU_INV       ), 0x200},
    { (struct s_head*)&lf_heads[ 1], "over", INST(VMU_OVER      ), 0x201},
    { (struct s_head*)&lf_heads[ 2], "a!",   INST(VMU_ASTORE    ), 0x202},
    { (struct s_head*)&lf_heads[ 3], "xor",  INST(VMU_XOR       ), 0x203},
    { (struct s_head*)&lf_heads[ 4], "+",    INST(VMU_PLUS      ), 0x204},
    { (struct s_head*)&lf_heads[ 5], "and",  INST(VMU_AND       ), 0x205},
    { (struct s_head*)&lf_heads[ 6], ">r",   INST(VMU_PUSH      ), 0x206},
    { (struct s_head*)&lf_heads[ 7], "unext",INST(VMU_UNEXT     ), 0x207},
    { (struct s_head*)&lf_heads[ 8], "2*",   INST(VMU_TWOSTAR   ), 0x208},
    { (struct s_head*)&lf_heads[ 9], "dup",  INST(VMU_DUP       ), 0x209},
    { (struct s_head*)&lf_heads[10], "drop", INST(VMU_DROP      ), 0x20A},
    { (struct s_head*)&lf_heads[11], "@a",   INST(VMU_FETCHA    ), 0x20B},
    { (struct s_head*)&lf_heads[12], "@a+",  INST(VMU_FETCHAPLUS), 0x20C},
    { (struct s_head*)&lf_heads[13], "r@",   INST(VMU_R         ), 0x20D},
    { (struct s_head*)&lf_heads[14], "r>",   INST(VMU_POP       ), 0x20E},
    { (struct s_head*)&lf_heads[15], "2/c",  INST(VMU_TWODIVC   ), 0x20F},
    { (struct s_head*)&lf_heads[16], "2/",   INST(VMU_TWODIV    ), 0x210},
    { (struct s_head*)&lf_heads[17], "!a",   INST(VMU_STOREA    ), 0x211},
    { (struct s_head*)&lf_heads[18], "!a+",  INST(VMU_STOREAPLUS), 0x212},
    { (struct s_head*)&lf_heads[19], "!b",   INST(VMU_STOREB    ), 0x213},
    { (struct s_head*)&lf_heads[20], "!b+",  INST(VMU_STOREBPLUS), 0x214},
    { (struct s_head*)&lf_heads[21], "swap", INST(VMU_SWAP      ), 0x215},
    { (struct s_head*)&lf_heads[22], "+*",   INST(VMU_PLUSSTAR  ), 0x216},
    { (struct s_head*)&lf_heads[23], "b",    INST(VMU_B         ), 0x217},
    { (struct s_head*)&lf_heads[24], "b!",   INST(VMU_BSTORE    ), 0x218},
    { (struct s_head*)&lf_heads[25], "@b",   INST(VMU_FETCHB    ), 0x219},
    { (struct s_head*)&lf_heads[26], "@b+",  INST(VMU_FETCHBPLUS), 0x21A},
    { (struct s_head*)&lf_heads[27], "a",    INST(VMU_A         ), 0x21B},
    { (struct s_head*)&lf_heads[28], "cy",   INST(VMU_CY        ), 0x21C},
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

    printf("List last name = '%s'\n", link->name);

    while (link != NULL) {
        const char *s1 = link->name;
        const char *s2 = target_name;
        int match = 1;

		while (1) {
            char c1 = *s1;
            char c2 = *s2;
            if (c1 == '\0') break;
            if (c2 == '\0') break;
            if (case_insensitive) {
                if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
                if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
            }

            if (c1 != c2) {
                match = 0;
                break;
            }

            s1++;
            s2++;
        }

        if (match) {
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

#include <stdio.h>
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

static int dot(int64_t val, int base) {
    char buf[68]; // Enough for 32-bit integer
    char* p = &buf[sizeof(buf)];
    *--p = 0; // Null terminator
    if (val < 0) {
        val = -val;
        serial_putc('-');
    }
    *--p = ' ';
    while (val > 0) {
        int digit = val % base;
        val /= base;
        if (digit < 10) {
            *--p = '0' + digit;
        }
        else {
            *--p = 'A' + (digit - 10);
        }
        if (p == buf) break;
    }
    return serial_puts(p);
}

static int vmDotS(void) {
    int32_t val;
    int8_t depth = (int8_t)vmRun(3, 8, 0);
    if (depth) {
        while (--depth) {
            val = vmRun(3, 0x100 + depth, 0);
            dot((int64_t)val, BASE);
        }
        val = (int)vmRun(3, 0, 0); // T
        dot((int64_t)val, BASE);
    }
	return 0;
}

static uint32_t char2digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return 100; // Invalid character for a digit
}

int execute_word(const struct s_head* word) {
    printf(" Executing word: %s (w=%u, aux=%u)\n", word->name, word->w, word->aux);
    return 0;
}



/* ========================================================================= 
>IN and BLK are the top values of an 8-deep internal block stack. They are
used to manage the input buffer and block number for file-based input.
TIB is a fixed buffer in Forth data space for terminal input. 
========================================================================= */

int loadTIB(void) {
    char *tib = (char*)TIB; // reset the TIB pointer
	int remaining = TIBSIZE; // remaining space in TIB

	if (BLK == 0) { // Load the TIB with keyboard input
        while (1) {
            if (serial_ready() == 0) {
#ifdef yield2c
                yield2c(); // Yield to other tasks while waiting...
#endif
                continue;
            }
            int c = serial_getc();
            if (c == EOF) { break; }
			if (c == '\r') { continue; }
            if (c == '\n') { break; }
			*tib++ = (char)c;
            remaining--;
			if (remaining <= 1) { break; } // Leave space for null terminator
        }
        while (remaining--) {
            *tib++ = 0; // Null-terminate and wipe the remaining TIB
        }
        return TIBSIZE - remaining; // Return the number of bytes loaded into TIB
    }
	return 0; // For now, we only handle keyboard input (BLK == 0)
}

static char token[32]; // token buffer for parsing
static char* source = NULL; // pointer to the current position in the input string
static uint32_t value; // value returned by numeric input parsing

/* 
parseStr assumes `str` is always correctly terminated with a null character. 
It extracts the next terminator-delimited token from `str` and returns a pointer
to the next character after the token. If no token is found, it returns NULL.
The extracted token is stored in a static buffer for later use.
*/

static char thisChar(void) {
    return source[TOIN];
}

int parseStr(char terminator) {
    int ior = 0;
    // Skip leading whitespace
    while(1) {
        char c = thisChar();
        if (c == '\0') break;
        if (c != ' ') break;
        TOIN++;
    }
	// Copy characters into the token buffer
    int i = 0;
    while(1) {
		char c = thisChar();
        if (c == '\0') break;
        if (c == terminator) break;
		token[i++] = c;
        if (i >= sizeof(token) - 1) {
            ior = ERR_DEFINITION_TOO_LONG;
            break; // Prevent buffer overflow
        }
        TOIN++;
    }
	// Terminate the token string
    token[i] = 0;
    return 0;
}

/**
 * Attempts to interpret a raw token text as a numeric literal (base 10).
 * Returns 1 if successful and pushes to stack, 0 if it's not a number.
 */
int parseNumber(int base) {
    DPL = 0;
    value = 0;
    int i = 0;
    while (1) {
        int8_t c = (int8_t)token[i++];
        if (c == '\0') break;
        uint32_t digit = char2digit(c);
		if (digit >= base) return ERR_UNDEFINED_WORD;
        value = (value * base) + digit;
	}
    return 0;
}

/**
 * Core Forth Text Interpreter (EVALUATE).
 * Processes a specific raw text memory buffer slice of a given length.
 *
 * @param str Ptr to the character stream data to interpret.
 * @param len The exact byte boundary size of the string string.
 * @return    0 on normal execution, -1 if a termination word (like BYE) is trapped.
 */
int interpret(char* str, size_t len) {
    if (str == NULL || len == 0) {
        return 0;
    }
    // Safety implementation boundary: Ensure the working block slice is null-terminated
    // so standard string tokenizers do not run past the parameter boundary.
	// After this, parsing does not require additional length checks (len is not used).

    if (str[len] != '\0') {
        str[len] = '\0';
    }
    source = str;
    TOIN = 0;
    int ior = 0;
    while (thisChar() != 0) {
        if (ior) return ior;
        ior = parseStr(' '); // Parse the next token delimited by space
		if (ior) return ior;
        // A. Check the current active vocabulary lists
        const struct s_head* word = search_context(token, 1); // 1 = Case Insensitive

		if (word != NULL) {             // word was found in the dictionary
            if (STATE) {
                ior = execute_word(word);
            } else {
                ior = execute_word(word);
			}
            int base = BASE;            // safety-check the base
            if (base < 2 || base > 36) {
                ior = ERR_INVALID_BASE;
                BASE = 10;
            }
            continue;
        }
		printf(" Token %s not found in dictionary. Attempting numeric parse.\n", token);
        ior = parseNumber(BASE);        // Fall back to numeric evaluation
        if (ior) return ior;
        if (STATE) {                    // valid number
        }
        else {
            vmRun(1, INST(VMU_DUP), 0);
            vmRun(2, 0, value); // Push the number onto the data stack
        }
    }
    return ior;
}

/* ========================================================================= */
/* OUTER 'QUIT' MAIN TERMINAL REPL LOOP                                      */
/* ========================================================================= */

void QUIT(void) {
    serial_puts("May the Forth be with you. Type 'BYE' to exit.\n");

    while (1) {
        BASE = 10;
        STATE = 0;
        DPL = 0;
        TOIN = 0;
        BLK = 0;
        vmRun(4, 0, 0); // reset the VM
        // REPL until an error (or bye) occurs
        int32_t ior = 0;
        while (ior == 0) {
            vmDotS();
            serial_puts("ok>");
            int len = loadTIB();
            ior = interpret((char*)TIB, len);
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
            dot((int64_t)ior, BASE);
            break;
		}
    }
}

int32_t VMapi0Call(int32_t tos, int32_t nos, int fn) {
    (void)tos; (void)nos;
    fn &= 0x7F;
    return -1;
}

int32_t VMapi1Call(int32_t tos, int32_t nos, int fn) {
    (void)tos; (void)nos;
    fn &= 0x7F;
    return -1;
}
