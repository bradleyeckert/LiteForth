#ifndef FORTH_H
#define FORTH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "options.h"

/**
vm_memory[RAM_PAGE] is the RAM page. Forth variables are placed at fixed
locations for accessibility by Forth or by C.
*/

#define VARIABLE(addr) ((RAM_PAGE << (22 - VM_LOG2_PAGES)) | (addr))
#define BITFIELD(size, pos, addr) ((size << 27) | (pos << 22) | VARIABLE(addr))
#define BFADDR(idx) (RAM_BASE << (22 - VM_LOG2_PAGES))

#define F_CURRENT    1
#define F_TIB        2
#define F_BLK        (F_TIB + TIBCELLS)
#define F_SCR        (F_BLK + 1)
#define F_PTRS       (F_SCR + 1)
#define F_CONTEXT    (F_PTRS + 9)
#define F_BLOCKBUFS  (F_CONTEXT + ((CONTEXT_MAX + 7) / 4))
#define F_HERE0      (F_BLOCKBUFS + (BLOCK_SIZE_CELLS * SYSTEM_BLOCKS))

#define F_PTRS_UDP   (F_PTRS)
#define F_PTRS_IDP   (F_PTRS + 2)
#define F_PTRS_CP    (F_PTRS + 4)
#define F_PTRS_TP    (F_PTRS + 6)
#define F_PTRS_IDP0  (F_PTRS + 8)

#define LF_PACKEDSTATE vm_memory[RAM_PAGE]  /* Packed Forth state          */
#define LF_BASE      BITFIELD(6, 0, 0)
#define LF_TIBSTATE  BITFIELD(2, 6, 0)
#define LF_STATE     BITFIELD(1, 8, 0)
#define LF_DPL       BITFIELD(6, 9, 0)
#define LF_TOIN      BITFIELD(13, 15, 0)
#define LF_MSPACE    BITFIELD(2, 28, 0)
#define LF_CURRENT   BITFIELD(8, 0, F_CURRENT)
#define LF_TIB       BITFIELD(8, 0, F_TIB) /* Terminal Input Buffer       */
#define LF_CONTEXT   BITFIELD(8, 0, F_CONTEXT)    /* context list          */
#define LF_BLOCKBUFS VARIABLE(F_BLOCKBUFS)
#define LF_HERE0     VARIABLE(F_HERE0)      /* first free RAM              */

#define CURRENT     ((int8_t *)&vm_memory[RAM_PAGE][F_CURRENT])
#define CONTEXT     ((int8_t *)&vm_memory[RAM_PAGE][F_CONTEXT])
#define TIB         ((char *)&vm_memory[RAM_PAGE][F_TIB])
#define TIBSIZE     (TIBCELLS * sizeof(int32_t)) // C only
#define BLK         vm_memory[RAM_PAGE][F_BLK]

// Flags in word->w[31:27]
#define W_PRIMITIVE     0x80000000 // the xt is a primitive in slot 1
#define W_MACRO         0x40000000 // the xt is all primitives except nops
#define W_WIDE_INST     0x20000000 // treat primitive as 16-bit instruction
#define W_NO_TAIL_CALL  0x10000000 // don't allow tail recursion

// Flags in word->aux[31:24]
#define A_SMUDGED       0x80000000 // this bit is set by `:`
#define A_IMMEDIATE     0x40000000 // this word is immediate
#define A_NO_EXECUTE    0x20000000 // only execute while compiling
#define A_CONSTANT      0x10000000 // w is a constant
#define A_IMMED_ONLY    (A_IMMEDIATE | A_NO_EXECUTE)


/* ======================================================================= */
/* STRUCTURE DEFINITIONS                                                   */
/* ======================================================================= */

/**
 * Structure representing an individual entry (word) in the dictionary.
 */
typedef struct s_head { 
    struct s_head *link; /* Pointer to the previous word in the list       */
    char *name;          /* Pointer to a C-string representing word name   */
    uint32_t w;          /* Execution token / Word identifier payload      */
    uint32_t aux;        /* Auxiliary storage parameter                    */
} s_head;

/**
 * Structure representing a Wordlist / Vocabulary Identifier (WID).
 */
typedef struct s_wid {   /* Uses WIDS_MAX sizeof(s_wid) of RAM             */
    const struct s_head* head; /* Pointer to the latest word in this list  */
    char* name;          /* Descriptive name of the vocabulary             */
} s_wid;

/**
 * Structure representing a CONSTANT.
 * 
 * An array of constants simplifies ad-hoc additions to the constants list.
 */
typedef struct {
    int32_t value;
    const char* name;
} ConstantMapping;

typedef int (putcfunc)(char c);


/* ======================================================================= */
/* CONSTANTS                                                               */
/* ======================================================================= */

/* Reset / Normal Formatting */
#define COLOR_NORMAL         -1

/* Standard ANSI Colors (0–7) */
#define COLOR_BLACK           0
#define COLOR_RED             1
#define COLOR_GREEN           2
#define COLOR_YELLOW          3
#define COLOR_BLUE            4
#define COLOR_MAGENTA         5
#define COLOR_CYAN            6
#define COLOR_WHITE           7

/* High-Intensity / Bright Colors (8–15) */
#define COLOR_BRIGHT_BLACK    8   /* Dark Gray */
#define COLOR_BRIGHT_RED      9
#define COLOR_BRIGHT_GREEN    10
#define COLOR_BRIGHT_YELLOW   11
#define COLOR_BRIGHT_BLUE     12
#define COLOR_BRIGHT_MAGENTA  13
#define COLOR_BRIGHT_CYAN     14
#define COLOR_BRIGHT_WHITE    15

#define API_COMPILE           48

/**
 * The standard Forth outer interpreter (terminal loop).
 * Prints the version banner, resets the dictionary and search order, then
 * reads lines from the terminal and interprets them. After each line it
 * checks the data stack: a line may leave at most STACK_CAPACITY / 2 - 1
 * items. A line too long for TIB is not interpreted. After an error it
 * reports the error, resets the stacks and base, and continues.
 * Open the terminal with `serial_open` before calling.
 * @return 0 when `bye` is executed, the error code of the first error if
 *         SYS_OPTION_VALIDATION is set, or an output error if the prompt
 *         can't be written.
 */
int QUIT(void);

/**
 * The Forth text interpreter.
 * Interprets (or, while STATE is set, compiles) the words and numbers in a
 * character stream, starting a new input source with >IN at 0 and BLK at 0.
 * `load` interprets a block before it returns, so every block loaded
 * from this stream is finished when lfInterpret returns.
 * @param str Character stream to interpret. A NUL ends it early.
 * @param len Stream length in bytes.
 * @return 0 on normal execution, else Forth error code from errcodes.h
 *         (ERR_QUIT for `bye`). If any other error happens inside a block,
 *         prints the input trace: one line per nesting level, innermost
 *         first, ending with the terminal line.
 */
int lfInterpret(char* str, int len);

/**
 * Prints the data stack, bottom to top, as `( n1 n2 ... ) `.
 * If the stack is deeper than DOT_S_MAX, prints `[depth]... ` followed by the
 * top DOT_S_MAX items. Prints nothing when the stack is empty.
 * @return 0.
 */
int lfDotS(void);

/**
 * Parses the next whitespace-delimited word from the input source at >IN.
 * Skips leading whitespace, copies the word, and moves >IN past one
 * trailing delimiter. A word too long for dest is truncated.
 * @param dest Buffer that receives the null-terminated word (empty at end of input).
 * @param destSize Size of dest in bytes, including the terminator.
 * @return 0 on success, or ERR_PARSED_STRING_OVERFLOW if the word was truncated.
 */
int lfParseWord(char* dest, int destSize);

/**
 * Creates a dictionary header for the next word in the input stream.
 * Stores the name and header in text space, links the header into the
 * CURRENT wordlist, and makes it the latest header (see lfToHeader).
 * @param w Execution token or value for the header's w field.
 * @param aux Initial flags for the header's aux field (e.g., A_SMUDGED, A_CONSTANT).
 * @param name If not NULL, receives a C pointer to the stored name string.
 * @return 0 on success, or a negative error code (e.g., ERR_DICTIONARY_OVERFLOW).
 */
int lfHeader(uint32_t w, uint32_t aux, char** name);

/**
 * Modifies the latest header created by lfHeader.
 * @param w Bits to OR into the header's w field.
 * @param aux Bits to toggle (XOR) in the header's aux field, e.g. A_SMUDGED
 *        to reveal a word or A_IMMEDIATE to make it immediate.
 * @return 0 on success, or ERR_UNSUPPORTED_OPERATION if no header has been created.
 */
int lfToHeader(uint32_t w, uint32_t aux);

/**
 * Allocates a new, empty wordlist.
 * @param name Descriptive name shown by `.wid`, or NULL.
 * @return The new wordlist identifier (wid), or ERR_WID_OVERFLOW if all
 *         WIDS_MAX wordlists are in use.
 */
int lfAddWordlist(char* name);

/**
 * Parses characters from the input source at >IN up to a terminator.
 * Handles the C escapes \\a \\b \\f \\n \\r \\t \\v. A backslash before any
 * other character, such as another backslash or the terminator, passes that
 * character through.
 * Stops at the terminator, which is consumed, or at the end of input.
 * @param echo Function called with each resulting character, or NULL to skip it.
 * @param terminator Character that ends the string, e.g. '"' or ')'.
 * @return 0, or the result of the last echo call.
 */
int lfParseInputString(putcfunc* echo, char terminator);

/**
 * Parses the next word from the input and looks it up in the search order.
 * Hidden (smudged) words are skipped.
 * @return Pointer to the word's header, or NULL if not found or not parsed.
 */
const struct s_head* lfTickWord(void);

/**
 * Finds the name of a word by value, searching the wordlists in the search order.
 * A word matches when (w & must) == expected and (w & mask) == value.
 * Used by the disassembler to label addresses.
 * @param value Value to look for, after masking.
 * @param mask Bits of the header's w field to compare with value.
 * @param must Bits of the header's w field to check against expected.
 * @param expected Required value of (w & must), e.g. 0 to exclude primitives.
 * @return The name of the first visible matching word, or NULL if none matches.
 */
char* lfFindLabel(uint32_t value, uint32_t mask, uint32_t must, uint32_t expected);

/**
 * @brief Forth word `.wid`  ( wid -- )
 * Prints the name of a wordlist, or its number if it has no name.
 *
 * @return 0 on success, or ERR_SEARCH_ORDER_OVERFLOW if wid is not allocated.
 */
int lfAPI_dotWid(void);

/**
 * @brief Forth word `words`  ( -- )
 * Lists the visible words in the first wordlist of the search order.
 * With colors enabled, immediate words, primitives and constants are
 * shown in different colors.
 *
 * @return 0.
 */
int lfAPI_words(void);

/**
 * @brief Forth word `>options`  ( n -- )
 * Sets system option flags (SYS_OPTION_* in vm.h). A nonzero n ORs its
 * bits into the options; zero clears all options. Has no effect once
 * SYS_OPTIONS_LOCKED is set.
 *
 * @return 0.
 */
int lfAPI_setFlags(void);

/**
 * @brief Forth word `only`  ( -- )
 * Sets the search order to the minimal `only` wordlist.
 *
 * @return 0.
 */
int lfAPI_only(void);

/**
 * @brief Forth word `forth`  ( -- )
 * Replaces the first wordlist in the search order with the `forth` wordlist.
 *
 * @return 0.
 */
int lfAPI_forth(void);

/**
 * @brief Forth word `-->`  ( -- )
 * Stops interpreting the current block and continues with block BLK+1.
 * The next block replaces the current one, so chains of any length do not
 * use up `load` nesting.
 *
 * @return 0 on success, ERR_INVALID_BLOCK_NUMBER if not interpreting a
 *         block, or an error from `load`.
 */
int lfAPI_nextBlock(void);

/**
 * @brief Forth word `load`  ( u -- )
 * Interprets block u, then returns. The current input source, BLK and >IN
 * are saved and restored, so interpretation resumes right after `load`,
 * whether it was typed or called from a colon definition. Loads can nest up
 * to MAX_LOAD_NESTING deep; each level recurses on the C stack.
 *
 * @return 0 on success, ERR_INVALID_BLOCK_NUMBER if u is 0,
 *         ERR_STACK_OVERFLOW if nested too deeply, or a block read error.
 */
int lfAPI_load(void);

/**
 * @brief Forth word `empty`  ( -- )
 * Resets the dictionary: removes all user-defined words and wordlists and
 * resets the dictionary pointers (see lfInitPointers).
 *
 * @return 0 on success, or the error from lfInitPointers.
 */
int lfAPI_empty(void);

/**
 * @brief Forth word `block`  ( u -- addr )
 * Returns the address of a RAM buffer holding block u, reading the block
 * from storage if it is not already in a buffer.
 *
 * @return 0 on success, or non-zero VM error code.
 */
int lfAPI_block(void);

#ifdef __cplusplus
}
#endif

#endif /* FORTH_H */
