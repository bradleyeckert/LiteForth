//#include <stdint.h>
//#include <stddef.h> 
#include "forth.h"
#include <stdio.h>  /* Included for printf in main */

static const struct header my_headers[] = { 
    { NULL,                            "FIRST",  10, 100 }, 
    { (struct header *)&my_headers[0], "SECOND", 20, 200 }, 
    { (struct header *)&my_headers[1], "THIRD",  30, 300 } 
}; 

struct wid_structure wids[WIDS_MAX] = { 
    [0] = { 
        .head = &my_headers[2], 
        .name = "Root Vocabulary" 
    } 
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
   a \0 in either string. */

const struct header* search_wordlist(int wid_index, const char *target_name, int case_insensitive) {
    if (wid_index < 0 || wid_index >= WIDS_MAX) {
        return NULL;
    }

    const struct header *link = wids[wid_index].head;

    while (link != NULL) {
        const char *s1 = link->name;
        const char *s2 = target_name;
        int match = 1;

        while (*s1 != '\0' || *s2 != '\0') {
            char c1 = *s1;
            char c2 = *s2;

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
const struct header* search_context(const char *target_name, int case_insensitive) {
    for (int i = 0; i < CONTEXT_MAX; i++) {
        int wid_idx = context[i];
        
        // Stop searching if we hit the end of the defined context order (-1)
        if (wid_idx == -1) {
            break;
        }
        
        const struct header *found = search_wordlist(wid_idx, target_name, case_insensitive);
        if (found != NULL) {
            return found; // Return immediately upon first match in priority order
        }
    }
    return NULL; // Word not found in any active wordlist
}

void execute_word(const struct header* word) {
	printf(" Executing word: %s (w=%u, aux=%u)\n", word->name, word->w, word->aux);
}

// Simple data stack for our interpreter demonstration
static int32_t data_stack[STACK_MAX];
static int8_t  sp = -1;

void push(int32_t val) {
    if (sp < STACK_MAX - 1) data_stack[++sp] = val;
}

int32_t pop(void) {
    if (sp >= 0) return data_stack[sp--];
    printf(" Stack underflow! ");
    return 0;
}

/**
 * Attempts to interpret a raw token text as a numeric literal (base 10).
 * Returns 1 if successful and pushes to stack, 0 if it's not a number.
 */
int parse_literal(const char* token) {
    char* endptr;
    long val = strtol(token, &endptr, 10);
    if (endptr != token && *endptr == '\0') {
        push((int32_t)val);
        return 1;
    }
    return 0;
}


/* ========================================================================= */
/* FACTORED INTERPRET INTERACTION COMPONENT                                  */
/* ========================================================================= */

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
    if (str[len] != '\0') {
        str[len] = '\0';
    }

    // Parse the buffer into white-space delimited tokens
    char* token = strtok(str, " ");

    while (token != NULL) {
        if (*token == '\0') {
            token = strtok(NULL, " ");
            continue;
        }

        // Check for interpreter execution exit condition
        if (strcmp(token, "BYE") == 0 || strcmp(token, "bye") == 0) {
            return -1;
        }

        // A. Check the current active vocabulary lists
        const struct header* word = search_context(token, 1); // 1 = Case Insensitive

        if (word != NULL) {
            execute_word(word);
        }
        // B. Fall back to numeric evaluation
        else if (parse_literal(token)) {
            printf(" %s ", token);
        }
        // C. Trap unknown symbols and abort line parsing
        else {
            printf(" ? Unknown token: %s\n", token);
            return 0;
        }

        token = strtok(NULL, " ");
    }

    return 0;
}

/* ========================================================================= */
/* SIMPLIFIED OUTER 'QUIT' MAIN TERMINAL INTERFACE LOOP                      */
/* ========================================================================= */

#define TIB_SIZE 256

void QUIT(void) {
    char tib[TIB_SIZE];

    printf("Embedded Forth Engine initialized. Type 'BYE' to exit.\n");
    printf("ok\n");

    while (1) {
        // Query the terminal interface environment
        if (fgets(tib, TIB_SIZE, stdin) == NULL) {
            break;
        }

        // Measure input and trim trailing newlines
        size_t input_len = strcspn(tib, "\r\n");
        tib[input_len] = '\0';

        // Direct execution routing to our clean, decoupled interpret engine
        int signals = interpret(tib, input_len);
        if (signals == -1) {
            break; // Exit caught
        }

        printf(" ok\n");
    }
}
