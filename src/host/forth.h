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

#define VARIABLE(addr) ((RAM_PAGE << (22 - VM_LOG2_PAGES)) | addr)
#define BITFIELD(size, pos, addr) ((size << 27) | (pos << 22) | VARIABLE(addr))
#define BFADDR(idx) (RAM_BASE << (22 - VM_LOG2_PAGES))

#define F_TIB        2
#define F_BLK        (F_TIB + TIBCELLS)
#define F_HERE0      (F_BLK + 1)            /* offset to first free RAM    */

#define LF_PACKEDSTATE vm_memory[RAM_PAGE]  /* Packed Forth state          */
#define LF_BASE      BITFIELD(6, 0, 0)
#define LF_TIBSTATE  BITFIELD(2, 6, 0)
#define LF_STATE     BITFIELD(1, 8, 0)
#define LF_DPL       BITFIELD(6, 9, 0)
#define LF_TOIN      BITFIELD(13, 15, 0)
#define LF_PAGE      BITFIELD(8, 0, 1)
#define LF_TIB       BITFIELD(8, 0, F_TIB)  /* Terminal Input Buffer       */
#define LF_BLK       VARIABLE(F_BLK)        /* Allow 4G blocks             */

#define TIB         ((char *)&vm_memory[RAM_PAGE][F_TIB])
#define TIBSIZE     (TIBCELLS * sizeof(int32_t)) // C only
#define BLK         vm_memory[RAM_PAGE][F_BLK]

#define SYS_FLAGS_LOCKED      0x0010        /* `>options` ignores changes  */
#define SYS_FLAG_VERBOSE      0x0008        /* echo input lines            */
#define SYS_FLAG_VALIDATION   0x0004        /* quit immediately upon error */
#define SYS_FLAG_NO_DOTESS    0x0002        /* do not display the stack    */
#define SYS_FLAG_NO_OK        0x0001        /* do not display "ok>"        */


/* ======================================================================= */
/* 1. STRUCTURE DEFINITIONS                                                */
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
 * Structure representing dictionary pointers.
 * 
 * These are set up for the current page using:
 * `<codesize> <headsize> init_here`
 * The three `unused` values for the page can be calculated from these fields.
 * CODE always begins at `page-base 1 +`, leaving `page-base` for a link.
 * cp0 is hard-wired at 1.
 * hp0 is cp0 + codesize.
 * dp0 is hp0 + datasize.
 */
typedef struct s_dictptr {
    uint32_t cp;         /* Address of code in page (16-bit fields) */
    uint32_t hp;         /* Address of headers in page */
    uint32_t dp;         /* Address of data in page */
    uint16_t hp0;        /* Offset to start of headers in page */
    uint16_t dp0;        /* Offset to start of data in page */
} s_dictptr;



/* ======================================================================= */
/* 2. DICTIONARY API FUNCTIONS                                             */
/* ======================================================================= */

/**
 * Searches a specific vocabulary list for a header matching 'target_name'.
 * Single-pass character evaluation loop optimized for bare-metal systems.
 * 
 * @param wid_index        The index inside the 'wids' array (0 to 15).
 * @param target_name      The string identifier to search for.
 * @param case_insensitive If non-zero, performs a case-insensitive match.
 * @return                 A const pointer to the matching header, or 
 *                         NULL if not found.
 */
const struct s_head* search_wordlist(int wid_index, const char *target_name,
    int case_insensitive);

/**
 * Iterates through the active wordlists defined in the 'context' array.
 * Replicates the behavior of traditional Forth text interpreters.
 * 
 * @param target_name      The string identifier to search for.
 * @param case_insensitive If non-zero, performs a case-insensitive match.
 * @return                 A const pointer to the matching header, or 
 *                         NULL if not found.
*/
const struct s_head* search_context(const char *target_name, int case_insensitive);

/* ========================================================================= */
/* 4. INTERPRETER INTERFACE                                                  */
/* ========================================================================= */

/**
 * Core Forth Text Interpreter (EVALUATE).
 * Processes a specific raw text memory buffer slice of a given length.
 * 
 * @param str Pointer to the character stream data to interpret.
 * @param len The exact byte boundary size of the string string.
 * @return    0 on normal execution, error code if error.
 */
int interpret(char *str, size_t len);

/**
 * The standard Forth Outer Interpreter / Terminal Loop.
 * Enters an infinite terminal interaction stream, reading lines from console
 * and feeding them to the text interpreter layer.
 */
int QUIT(void);

int serial_puts(const char* s);
int lfCR(void);
int lfSpace(void);
int lfDot(int32_t val);
int lfDotB(uint32_t val, int base, int dpl, int digits);
int lfBASEfetch(void);
int lfBASEstore(int base);
int lfUnused(int type, int page);
int lfPAGEfetch(void);

#ifdef __cplusplus
}
#endif

#endif /* FORTH_H */
