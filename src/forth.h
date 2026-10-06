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
#define F_CONTEXT    (F_SCR + 1)
#define F_BLOCKBUFS  (F_CONTEXT + ((CONTEXT_MAX + 7) / 4))
#define F_PTRS       (F_BLOCKBUFS + (BLOCK_SIZE_CELLS * SYSTEM_BLOCKS))
#define F_HERE0      (F_PTRS + 8)

#define F_PTRS_UDP   (F_PTRS)
#define F_PTRS_IDP   (F_PTRS + 2)
#define F_PTRS_CP    (F_PTRS + 4)
#define F_PTRS_TP    (F_PTRS + 6)
#define F_PTRS_IDP0  (F_PTRS + 8)

#define LF_PACKEDSTATE vm_memory[RAM_PAGE]  /* Packed Forth state          */
#define LF_BASE      BITFIELD(6, 0, 0)
#define LF_STATE     BITFIELD(1, 6, 0)
#define LF_DPL       BITFIELD(6, 7, 0)
#define LF_TOIN      BITFIELD(13, 13, 0)
#define LF_MSPACE    BITFIELD(2, 26, 0)
#define LF_DEADTIB   BITFIELD(1, 28, 0)
#define LF_CURRENT   BITFIELD(8, 0, F_CURRENT)
#define LF_COLUMNS   BITFIELD(7, 8, F_CURRENT)
// there is spare room for 17 more bits of state in F_CURRENT
#define LF_TIB       BITFIELD(8, 0, F_TIB) /* Terminal Input Buffer        */
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
#define A_UNRESOLVED    0x08000000 // label has not been resolved
#define A_IMMED_ONLY    (A_IMMEDIATE | A_NO_EXECUTE)


/* ======================================================================= */
/* STRUCTURE DEFINITIONS                                                   */
/* ======================================================================= */

/**
 * Structure representing an individual entry (word) in the dictionary.
 *
 * Built-in headers (the C tables in forth.c) hold C pointers. Headers that
 * `lfHeader` makes in VM memory hold VM values instead, so that flash holds
 * no C addresses and needs no relocating when it's programmed or booted:
 *   - link (and s_wid.head) is a tagged value:
 *       0             end of the list;
 *       (a << 1) | 1  a header in VM memory at cell address a;
 *       (n << 2) | 2  the built-in headers of wordlist n (forth, only);
 *       otherwise     a C pointer to a built-in header.
 *   - name is the VM byte address of the name, when the header is in VM
 *     memory; a C string pointer in the built-in tables.
 * Follow links with lfFollow in forth.c, which also resolves the name.
 */
typedef struct s_head { 
    struct s_head *link; /* Previous word in the list (tagged, see above)  */
    char *name;          /* Name: C string, or VM byte address (see above) */
    uint32_t w;          /* Execution token / Word identifier payload      */
    uint32_t aux;        /* Auxiliary storage parameter                    */
} s_head;

/**
 * Structure representing a Wordlist / Vocabulary Identifier (WID).
 */
typedef struct s_wid {   /* Uses WIDS_MAX sizeof(s_wid) of RAM             */
    const struct s_head* head; /* Pointer to the latest word in this list  */
    char name[12];       /* Descriptive name of the vocabulary, or ""      */
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

/**
 * The standard Forth outer interpreter (terminal loop).
 * Prints the version banner, resets the VM, dictionary and search order, then
 * reads lines from the terminal and interprets them. After each line it
 * checks the data stack: a line may leave at most STACK_CAPACITY * 7 / 8 - 1
 * items, and underflow of up to STACK_CAPACITY / 16 items is caught. A line too long for TIB is not interpreted. After an error it
 * reports the error, empties the stacks (other VM registers keep their
 * values), resets the base, and continues.
 * Open the terminal with `serial_open` before calling.
 * @return 0 when `bye` is executed, the error code of the first error if
 *         SYS_OPTION_VALIDATION is set, or an output error if the prompt
 *         can't be written.
 */
int lfQuit(void);

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
 * Searches the wordlists in the search order for a visible (not smudged)
 * word that has all of `aux_flags` set in its aux field.
 * @param target_name Name to find (case-insensitive), or NULL for any name.
 * @param aux_flags   Flags the word must have, e.g. A_UNRESOLVED; 0 for none.
 * @param found_name  If not NULL, receives the found word's name.
 * @return The word's header, or NULL if there is none.
 */
const struct s_head* lfSearchContext(const char* target_name, uint32_t aux_flags,
                                     const char** found_name);

/**
 * Parses the next name and finds an unresolved label (A_UNRESOLVED, made by
 * `label`) with that name in the search order. If there is none, >IN is put
 * back, so the name can be parsed again (by lfHeader).
 * @return The label's header, or NULL.
 */
const struct s_head* lfParseLabel(void);

/**
 * Creates a dictionary header for the next word in the input stream.
 * Stores the name and header in text space, links the header into the
 * CURRENT wordlist, and makes it the latest header (see lfToHeader).
 * @param w Execution token or value for the header's w field.
 * @param aux Initial flags for the header's aux field (e.g., A_SMUDGED, A_CONSTANT).
 * The header's link and name are VM values, not C pointers (see s_head),
 * so the header is valid wherever its page is mapped.
 * @param name If not NULL, receives the VM byte address of the stored name.
 * @return 0 on success, or a negative error code (e.g., ERR_DICTIONARY_OVERFLOW).
 */
int lfHeader(uint32_t w, uint32_t aux, uint32_t* name);

/**
 * Finds the C address of a VM address.
 * @param a Cell address, or byte slice address (an 8-bit slice selects a
 *          byte of the cell, as in a name's address).
 * @return The C address, or NULL if a's page isn't mapped or a is past its
 *         read limit.
 */
char* lfVmBytes(uint32_t a);

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
 * @param name Descriptive name shown by `.wid`, or NULL for none. It is
 *        copied into the wid, truncated to 11 characters.
 * @return The new wordlist identifier (wid), or ERR_WID_OVERFLOW if all
 *         WIDS_MAX wordlists are in use.
 */
int lfAddWordlist(const char* name);

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

/* Cell offsets in the record compiled by `save-wids` */
#define WIDS_RECORD_SKIP   0  /* address of the cell after the record        */
#define WIDS_RECORD_COUNT  1  /* number of wordlists (wids_pointer)          */
#define WIDS_RECORD_TABLE  2  /* the s_wid structures, as raw bytes          */

/**
 * SAVE-WIDS  ( -- )
 * Compiles the wordlist record to text space, for the loader to restore
 * `wids` at bootup. The text pointer is aligned to a cell, then it compiles
 * (see WIDS_RECORD_*):
 *   - the address just past the record, to skip it;
 *   - the number of wordlists in use (wids_pointer);
 *   - that many s_wid structures as raw bytes, padded to a whole cell.
 * The heads are links (see s_head), not C pointers, so the record is valid
 * wherever the page is mapped. Use `save-wids` last before close-flash: words
 * defined after it aren't in the saved wordlists.
 * @return 0, ERR_DICTIONARY_OVERFLOW if text space is too small, or an
 *         ior from vmStore.
 */
int lfAPI_saveWids(void);

/**
 * Restores the wordlists from flash, for booting. Cell 1 holds the address
 * of the record compiled by `save-wids`. This checks the record, then sets
 * `wids_pointer` and `wids` from it. Heads and header links are VM values,
 * so nothing needs translating. A head that can't be resolved (not a link to
 * mapped VM memory or to a built-in list, e.g. a C pointer saved by an older
 * build) is set to NULL.
 * The search order is not changed. lfQuit calls this when
 * SYS_OPTION_BOOTING is set, then resets the VM and starts the app, unless
 * SYS_OPTION_NO_AUTORUN is set. If there is no valid record, lfQuit says
 * nothing, keeps the default wordlists and doesn't start the app.
 * @return 0, or ERR_BAD_BOOT_RECORD if cell 1 doesn't point to a valid record.
 */
int lfBootFromFlash(void);

#ifdef __cplusplus
}
#endif

#endif /* FORTH_H */
