#ifndef FORTH_H
#define FORTH_H

#include <stdint.h>
#include <stddef.h>

#define CONTEXT_MAX 8
#define WIDS_MAX 8
#define STACK_MAX 16

/* ========================================================================= */
/* 1. STRUCTURE DEFINITIONS                                                  */
/* ========================================================================= */

/**
 * Structure representing an individual entry (word) in the dictionary.
 */
struct header { 
    struct header *link;  /* Pointer to the previous word in the list */
    char *name;           /* Pointer to a C-string representing the word name */
    uint32_t w;           /* Execution token / Word identifier payload */
    uint32_t aux;         /* Auxiliary storage parameter */
}; 

/**
 * Structure representing a Wordlist / Vocabulary Identifier (WID).
 */
struct wid_structure {         /* Uses WIDS_MAX sizeof(wid_structure) of RAM */
    const struct header *head; /* Pointer to the top/latest word in this list */
    char *name;                /* Descriptive name of the vocabulary */
}; 

/* ========================================================================= */
/* 2. DICTIONARY API FUNCTIONS                                               */
/* ========================================================================= */

/**
 * Searches a specific vocabulary list for a header matching 'target_name'.
 * Single-pass character evaluation loop optimized for bare-metal systems.
 * 
 * @param wid_index        The index inside the 'wids' array (0 to 15).
 * @param target_name      The string identifier to search for.
 * @param case_insensitive If non-zero, performs a case-insensitive match.
 * @return                 A const pointer to the matching header, or NULL if not found.
 */
const struct header* search_wordlist(int wid_index, const char *target_name, int case_insensitive);

/**
 * Iterates through the active wordlists defined in the 'context' array.
 * Replicates the behavior of traditional Forth text interpreters.
 * 
 * @param target_name      The string identifier to search for.
 * @param case_insensitive If non-zero, performs a case-insensitive match.
 * @return                 A const pointer to the matching header, or NULL if not found.
 */
const struct header* search_context(const char *target_name, int case_insensitive);

/* ========================================================================= */
/* 4. INTERPRETER INTERFACE                                                  */
/* ========================================================================= */

/**
 * Core Forth Text Interpreter (EVALUATE).
 * Processes a specific raw text memory buffer slice of a given length.
 * 
 * @param str Pointer to the character stream data to interpret.
 * @param len The exact byte boundary size of the string string.
 * @return    0 on normal execution, -1 if a termination word (like BYE) is trapped.
 */
int interpret(char *str, size_t len);

/**
 * The standard Forth Outer Interpreter / Terminal Loop.
 * Enters an infinite terminal interaction stream, reading lines from console
 * and feeding them to the text interpreter layer.
 */
void QUIT(void);

#endif /* FORTH_H */
