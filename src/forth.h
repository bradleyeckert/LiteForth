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
#define F_CONTEXT    (F_PTRS + 6)
#define F_BLOCKBUFS  (F_CONTEXT + ((CONTEXT_MAX + 7) / 4))
#define F_HERE0      (F_BLOCKBUFS + (BLOCK_SIZE_CELLS * SYSTEM_BLOCKS))

#define LF_PACKEDSTATE vm_memory[RAM_PAGE]  /* Packed Forth state          */
#define LF_BASE      BITFIELD(6, 0, 0)
#define LF_TIBSTATE  BITFIELD(2, 6, 0)
#define LF_STATE     BITFIELD(1, 8, 0)
#define LF_DPL       BITFIELD(6, 9, 0)
#define LF_TOIN      BITFIELD(13, 15, 0)
#define LF_MSPACE    BITFIELD(2, 28, 0)
#define LF_CURRENT   BITFIELD(8, 0, F_CURRENT)
#define LF_TIB       BITFIELD(8, 0, F_TIB)  /* Terminal Input Buffer       */
#define LF_BLK       VARIABLE(F_BLK)        /* Allow 4G blocks             */
#define LF_PTRS      VARIABLE(F_PTRS)       /* dictionary pointers 6-cell  */
#define LF_CONTEXT   BITFIELD(8, 0, F_CONTEXT)    /* context list          */
#define LF_HERE0     VARIABLE(F_HERE0)      /* first free RAM              */

#define CURRENT     ((int8_t *)&vm_memory[RAM_PAGE][F_CURRENT])
#define CONTEXT     ((int8_t *)&vm_memory[RAM_PAGE][F_CONTEXT])
#define TIB         ((char *)&vm_memory[RAM_PAGE][F_TIB])
#define TIBSIZE     (TIBCELLS * sizeof(int32_t)) // C only
#define BLK         vm_memory[RAM_PAGE][F_BLK]

#define SYS_FLAGS_LOCKED      0x8000        /* `>options` ignores changes  */
#define SYS_FLAG_VERBOSE      0x0010        /* echo input lines            */
#define SYS_FLAG_IGNORE_CR    0x0008        /* ignore CR                   */
#define SYS_FLAG_VALIDATION   0x0004        /* quit immediately upon error */
#define SYS_FLAG_NO_DOTESS    0x0002        /* do not display the stack    */
#define SYS_FLAG_NO_OK        0x0001        /* do not display "ok>"        */

// Flags in word->w[31:27]
#define W_PRIMITIVE     0x80000000 // the xt is a primitive in slot 1
#define W_MACRO         0x40000000 // the xt is all primitives except nops

// Flags in word->aux[31:24]
#define A_SMUDGED       0x80000000 // this bit is set by `:`
#define A_IMMEDIATE     0x40000000 // this word is immediate
#define A_NO_EXECUTE    0x20000000 // only execute while compiling
#define A_NO_TAIL_CALL  0x10000000 // don't allow tail recursion
#define A_CONSTANT      0x08000000 // w is a constant
#define A_VOCABULARY    0x04000000 // vocabulary
#define A_NOTHING       0x02000000 // do nothing
#define A_IMMED_ONLY    (A_IMMEDIATE | A_NO_EXECUTE)


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
 * Structure representing a CONSTANT.
 * 
 * An array of constants simplifies ad-hoc additions to the constants list.
 */
typedef struct {
    int32_t value;
    const char* name;
} ConstantMapping;

/**
 * The standard Forth Outer Interpreter / Terminal Loop.
 * Enters an infinite terminal interaction stream, reading lines from console
 * and feeding them to the text interpreter layer.
 */
int QUIT(void);

int lfDotS(void);
int lfHeader(uint32_t w, uint32_t aux);
int lfToHeader(uint32_t w, uint32_t aux);
int lfCompileLit(int32_t num);
int lfAddWordlist(char* name);

int lfAPI_dotWid(void);
int lfAPI_words(void);
int lfAPI_getFlags(void);
int lfAPI_setFlags(void);
int lfAPI_paren(void);
int lfAPI_dotParen(void);
int lfAPI_tickx(void);
int lfAPI_only(void);
int lfAPI_forth(void);
int lfAPI_vocabulary(void);

#ifdef __cplusplus
}
#endif

#endif /* FORTH_H */
