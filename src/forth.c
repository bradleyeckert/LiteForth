#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "comp.h"
#include "tools.h"
#include "lfblocks.h"
#include "options.h"
#include <string.h>
#include <stddef.h>

// wsl: // cd /mnt/c/Users/User/Documents/GitHub/LiteForth

#if (FAT_FORTH & 1)
#include "utils.h"
#include "main.h"
#include "api0.h"
#endif

static int case_insensitive = CASE_INSENSITIVE;

static int lfTOINfetch(void) {
    uint32_t result;
    vmFetch(LF_TOIN, &result);
    return result;
}

static int lfTOINstore(int position) {
    return vmStore(LF_TOIN, position);
}

/*=========================================================================
* Define a Forth
=========================================================================*/

#define UOP(val) (W_PRIMITIVE | VM_UOPS | ((val) << SLOT0_POSITION) )
#define MACRO(s0, s1, s2) \
    (W_PRIMITIVE | W_MACRO | VM_UOPS | \
    ((s0) << SLOT0_POSITION) | ((s1) << LAST_SLOT_WIDTH)| (s2) )
#define DATA(idx) ((RAM_PAGE << (22 - VM_LOG2_PAGES)) + (idx))
#define API0(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_API0 | (idx))
#define SYS(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_SYS | (idx))
#define SYSTO(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_TOSYS | (idx))
#define SYSFM(idx) (W_PRIMITIVE | W_WIDE_INST | VMI_FROMSYS | (idx))

/*
 * Each header links to the one before it in its table, so a table is a
 * list whose head is its last row. PREV is the link to the previous row.
 * It counts rows with __COUNTER__ (supported by GCC, Clang, MSVC, IAR and
 * Keil), so rows can be added, removed or #if'd out without renumbering.
 * HEADS_BEGIN(table) must come right before the table, and nothing inside
 * a table may use __COUNTER__ except PREV.
 */
#define HEADS_BEGIN(table) enum { table##_row1 = __COUNTER__ + 1 }
#define PREV_IN(table) (struct s_head*)&table[__COUNTER__ - table##_row1]
#define LAST_HEAD(table) (&table[(sizeof(table) / sizeof(table[0])) - 1])

HEADS_BEGIN(only_heads);
#define PREV PREV_IN(only_heads)
static const struct s_head only_heads[] = {
    { NULL,     "bye",      API0(API_BYE), 0},
    { PREV,     "words",    API0(API_WORDS), 0},
    { PREV,     "forth",    API0(API_FORTH), 0},
    { PREV,     "only",     API0(API_ONLY), 0},
};
#undef PREV

// Flash cost per entry: 16 bytes plus name string (length+1 bytes).
// 64 entries is about 1.4 KB
// The forth wordlist continues into the only wordlist.
HEADS_BEGIN(forth_heads);
#define PREV PREV_IN(forth_heads)
static const struct s_head forth_heads[] = {  
    { (struct s_head*)LAST_HEAD(only_heads), "invert", UOP(VMU_INV),  0},
    { PREV, "inv",          UOP(VMU_INV),                             0},
    { PREV, "over",         UOP(VMU_OVER),                            0},
    { PREV, "a!",           UOP(VMU_ASTORE),                          0},
    { PREV, "xor",          UOP(VMU_XOR),                             0},
    { PREV, "+",            UOP(VMU_PLUS),                            0},
    { PREV, "and",          UOP(VMU_AND),                             0},
    { PREV, ">r",           UOP(VMU_PUSH),                            0},
    { PREV, "unext",        UOP(VMU_UNEXT),                           0},
    { PREV, "2*",           UOP(VMU_TWOSTAR),                         0},
    { PREV, "dup",          UOP(VMU_DUP),                             0},
    { PREV, "drop",         UOP(VMU_DROP),                            0},
    { PREV, "@a",           UOP(VMU_FETCHA),                          0},
    { PREV, "@a+",          UOP(VMU_FETCHAPLUS),                      0},
    { PREV, "@as",          UOP(VMU_FETCHASIGN),                      0},
    { PREV, "r@",           UOP(VMU_R),                               0},
    { PREV, "r>",           UOP(VMU_POP),                             0},
    { PREV, "2/c",          UOP(VMU_TWODIVC),                         0},
    { PREV, "2/",           UOP(VMU_TWODIV),                          0},
    { PREV, "!a",           UOP(VMU_STOREA),                          0},
    { PREV, "!a+",          UOP(VMU_STOREAPLUS),                      0},
    { PREV, "!b",           UOP(VMU_STOREB),                          0},
    { PREV, "!b+",          UOP(VMU_STOREBPLUS),                      0},
    { PREV, "swap",         UOP(VMU_SWAP),                            0},
    { PREV, "+*",           UOP(VMU_PLUSSTAR),                        0},
    { PREV, "b",            UOP(VMU_B),                               0},
    { PREV, "b!",           UOP(VMU_BSTORE),                          0},
    { PREV, "@b",           UOP(VMU_FETCHB),                          0},
    { PREV, "@b+",          UOP(VMU_FETCHBPLUS),                      0},
    { PREV, "a",            UOP(VMU_A),                               0},
    { PREV, "cy",           UOP(VMU_CY),                              0},
    { PREV, "u!",           UOP(VMU_USTORE),                          0},
    { PREV, "2dup",         MACRO(VMU_OVER,VMU_OVER,VMU_NOP),         0},
    { PREV, "2drop",        MACRO(VMU_DROP,VMU_DROP,VMU_NOP),         0},
    { PREV, "!",            MACRO(VMU_ASTORE,VMU_STOREA,VMU_NOP),     0},
    { PREV, "@",            MACRO(VMU_ASTORE,VMU_FETCHA,VMU_NOP),     0},
    { PREV, "s@",           MACRO(VMU_ASTORE,VMU_FETCHASIGN,VMU_NOP), 0},
    { PREV, "nip",          MACRO(VMU_SWAP,VMU_DROP,VMU_NOP),         0},
    { PREV, "tuck",         MACRO(VMU_SWAP,VMU_OVER,VMU_NOP),         0},
    { PREV, "um+",          MACRO(VMU_PLUS,VMU_CY,VMU_NOP),           0},
    { PREV, "slice+",       SYS(VMS_FIELDPLUS),                       0},
    { PREV, "]shr",         SYS(VMS_SHR),                             0},
    { PREV, "]shl",         SYS(VMS_SHL),                             0},
    { PREV, "break",        SYS(VMS_BREAK),                           0},
    { PREV, "capusec",      SYS(VMS_GETUSEC),                         0},
    { PREV, "shft[",        SYSTO(VMSTO_SHIFT),                       0},
    { PREV, "]task",        SYS(VMS_TASK),                            0},
    { PREV, "yeet",         SYSTO(VMSTO_YEET),                        0},
    { PREV, "task[",        SYSFM(VMSFROM_TASK),                      0},
    { PREV, "x@",           SYSFM(VMSFROM_X),                         0},
    { PREV, "y@",           SYSFM(VMSFROM_Y),                         0},
    { PREV, "um*",          API0(API_UMSTAR),                         0},
    { PREV, "m*",           API0(API_MSTAR),                          0},
    { PREV, "mu/mod",       API0(API_MUDIVMOD),                       0},
    { PREV, "*/mod",        API0(API_STARDIVMOD),                     0},
    { PREV, "t_rx?",        API0(API_T_RXQ),                          0},
    { PREV, "t_rx",         API0(API_T_RX),                           0},
    { PREV, "t_tx!",        API0(API_T_TXSTORE),                      0},
    { PREV, "t_tx?",        API0(API_T_TXQ),                          0},
    { PREV, ":",            API0(API_COLON),                          0},
    { PREV, ";",            API0(API_SEMICOLON),        A_IMMEDIATE | 0},
    { PREV, ">options",     API0(API_TOOPTIONS),                      0},
    { PREV, "empty",        API0(API_EMPTY),                          0},
    { PREV, "(",            API0(API_PAREN),            A_IMMEDIATE | 0},
    { PREV, "\xEF\xBB\xBF(", API0(API_PAREN),           A_IMMEDIATE | 0},
    { PREV, ".(",           API0(API_DOTPAREN),                       0},
    { PREV, "does>",        API0(API_DOES),             A_IMMEDIATE | 0},
    { PREV, "create",       API0(API_CREATE),                         0},
    { PREV, "cr",           API0(API_CR),                             0},
    { PREV, "literal",      API0(API_LITERAL),          A_IMMEDIATE | 0},
    { PREV, ".wid",         API0(API_DOTWID),                         0},
    { PREV, "x'",           API0(API_XTICK),                          0},
    { PREV, ">aux",         API0(API_TOAUX),                          0},
    { PREV, "char",         API0(API_CHAR),                           0},
    { PREV, "page",         API0(API_PAGE),                           0},
    { PREV, "wordlist",     API0(API_WORDLIST),                       0},
    { PREV, "open-flash",   API0(API_OPEN_FLASH),                     0},
    { PREV, "close-flash",  API0(API_CLOSE_FLASH),                    0},
    { PREV, "]",            API0(API_RBRACKET),                       0},
    { PREV, "[",            API0(API_LBRACKET),         A_IMMEDIATE | 0},
    { PREV, "exit",         API0(API_EXIT),             A_IMMEDIATE | 0},
    { PREV, "constant",     API0(API_CONSTANT),                       0},
    { PREV, "bits",         API0(API_BITS),                           0},
    { PREV, ">body",        API0(API_TOBODY),                         0},
    { PREV, "_,\"",         API0(API_COMMAQUOTE),                     0},
    { PREV, "bit",          API0(API_BIT),                            0},
    { PREV, ",inst",        API0(API_COMMAINST),                      0},
    { PREV, "immediate",    API0(API_IMMEDIATE),                      0},
    { PREV, "block",        API0(API_BLOCK),                          0},
    { PREV, "buffer",       API0(API_BUFFER),                         0},
    { PREV, "update",       API0(API_UPDATE),                         0},
    { PREV, "save-buffers", API0(API_SAVE_BUFFERS),                   0},
    { PREV, "flush",        API0(API_FLUSH),                          0},
    { PREV, "empty-buffers", API0(API_EMPTY_BUFFERS),                 0},
    { PREV, "load",         API0(API_LOAD),                           0},
    { PREV, "capacity",     API0(API_CAPACITY),                       0},
    { PREV, "-->",          API0(API_NEXTBLOCK),                      0},
    { PREV, "postpone",     API0(API_POSTPONE),         A_IMMEDIATE | 0},
    { PREV, ",compile",     API0(API_COMPILE),                        0},
    { PREV, "|inst",        API0(API_NEWINST),          A_IMMEDIATE | 0},
    { PREV, "cold",         API0(API_COLD),                           0},
    { PREV, ":noname",      API0(API_NONAME),                         0},
    { PREV, "save-wids",    API0(API_SAVE_WIDS),                      0},
    { PREV, "label",        API0(API_LABEL),                          0},
#if (FAT_FORTH & 1)                                     
    { PREV, "}t",           API0(API_ENDTEST),                        0},
    { PREV, "->",           API0(API_DOTEST),                         0},
    { PREV, "t{",           API0(API_BEGINTEST),                      0},
    { PREV, "hex",          API0(API_HEX),                            0},
    { PREV, "decimal",      API0(API_DECIMAL),                        0},
    { PREV, ".page",        API0(API_DOTPAGE),                        0},
    { PREV, ".pages",       API0(API_DOTPAGES),                       0},
    { PREV, "dump",         API0(API_DUMP),                           0},
    { PREV, "dumpi",        API0(API_DUMPI),                          0},
    { PREV, "dasm",         API0(API_DASM),                           0},
    { PREV, ".s",           API0(API_DOTS),                           0},
    { PREV, ".",            API0(API_DOT),                            0},
    { PREV, "see",          API0(API_SEE),                            0},
#endif
};
#undef PREV

static const ConstantMapping constant_table[] = {
    { -1,               "true"},
    { 0,                "false"},
    { LF_STATE,         "state"},
    { LF_BASE,          "base"},
    { LF_CURRENT,       "current"},
    { LF_CONTEXT,       "context"},
    { CONTEXT_MAX,      "|context|"},
    { LF_DPL,           "dpl"},
    { LF_TOIN,          ">in"},
    { VARIABLE(F_BLK),  "blk"},
    { BLOCK_SIZE_CELLS, "|block|"},
    { LF_TIB,           "TIB"},
    { VARIABLE(F_PTRS), "dp[]"},
    { LF_MSPACE,        "dp^" },
    { VM_LOG2_PAGES,    "log2pages"},
    { VARIABLE(F_HERE0),"ram-base"},
    { ((STACK_CAPACITY - 1) << 16) | (STACK_CAPACITY - 1), "stack-masks"},
    { LF_DEADTIB,       "stop-tib"},
    { VMI_CALL,         "_call"},
    { VMI_JUMP,         "_jump"},
    { VMI_LIT,          "_lit"},
    { VMI_ZBRAN,        "_0bran"},
    { VMI_BRAN,         "_bran"},
    { VMI_PBRAN,        "_pbran"},
    { VMI_RCALL,        "_rcall"},
    { VMI_NEXT,         "_next"},
    { VMI_PFX,          "_pfx"},
    { VMI_PFX1,         "_pfx1"},
    { VMI_USER,         "_user"},
    { VMI_API0,         "_api0"},
    { VMI_API1,         "_api1"},
    { W_PRIMITIVE,      "w_primitive"},
    { W_MACRO,          "w_macro"},
    { W_WIDE_INST,      "w_wide_inst"},
    { W_NO_TAIL_CALL,   "w_no_tail_call"},
    { A_SMUDGED,        "a_smudged"},
    { A_IMMEDIATE,      "a_immediate"},
    { A_CONSTANT,       "a_constant"}
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
            if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
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

/* ======================================================================= */
/* WORDLIST TABLE                                                          */
/* ======================================================================= */

struct s_wid wids[WIDS_MAX];

/*
 * Links (documented with s_head in forth.h). A link is a tagged value, so
 * that headers in flash hold no C addresses:
 *   0               end of the list
 *   (a << 1) | 1    a header in VM memory at cell address a
 *   (n << 2) | 2    the built-in headers of wordlist n (builtin_heads[n])
 *   other           a C pointer to a built-in header (links inside the tables)
 * The name of a header in VM memory is a VM byte address, not a C pointer.
 * This scheme works because C pointers are always 4-byte-aligned, (2 LSBs 00).
 */
#define EMPTY_WIDS  2
#define LINK_BUILTIN(n)  ((const struct s_head*)(uintptr_t)(((n) << 2) | 2))

static const struct s_head* const builtin_heads[EMPTY_WIDS] = {
    LAST_HEAD(forth_heads), LAST_HEAD(only_heads)
};

static struct s_wid wids_empty[] = { // wordlists
    {.head = LINK_BUILTIN(0), .name = "`forth" },
    {.head = LINK_BUILTIN(1), .name = "`only" }
};

static int wids_pointer = EMPTY_WIDS;

// lfVmBytes (documented in forth.h)
char* lfVmBytes(uint32_t a) {
    int page = (a & 0x3FFFFF) >> (22 - VM_LOG2_PAGES);
    uint32_t cell = a & VM_PAGE_MASK;
    if ((vm_memory[page] == NULL) || (cell >= vm_memory_rd_limit[page])) return NULL;
#ifdef VM_IO_PAGE
    if (page == VM_IO_PAGE) return NULL;    // registers, never names or headers
#endif
    return (char*)&vm_memory[page][cell] + ((a >> 25) & 3);
}

// The link to a header at VM cell address a
static const struct s_head* linkToVM(uint32_t a) {
    return (const struct s_head*)(uintptr_t)(((uintptr_t)a << 1) | 1);
}

/*
 * Follows a link: returns the header it refers to, or NULL at the end of the
 * list (or if it can't be resolved). If name isn't NULL, it receives the
 * header's name as a C string.
 */
static const struct s_head* lfFollow(const struct s_head* link, const char** name) {
    uintptr_t v = (uintptr_t)link;
    const struct s_head* h;
    if (v & 1) {                        // header in VM memory
        h = (const struct s_head*)lfVmBytes((uint32_t)(v >> 1));
        if ((h != NULL) && (name != NULL)) {
            *name = lfVmBytes((uint32_t)(uintptr_t)h->name);
            if (*name == NULL) *name = "";
        }
        return h;
    }
    if (v & 2) {                        // built-in list of a wordlist
        v >>= 2;
        h = (v < EMPTY_WIDS) ? builtin_heads[v] : NULL;
    } else {
        h = link;                       // C pointer into a built-in table
    }
    if ((h != NULL) && (name != NULL)) *name = h->name;
    return h;
}

static void lfResetWids(void) { // initialize the wordlists
    memcpy(wids, wids_empty, sizeof(wids_empty));
    wids_pointer = EMPTY_WIDS;
}

// EMPTY  ( -- )  Resets the dictionary
int lfAPI_empty(void) {
    lfResetWids();
    return lfInitPointers();
}

// Start a new wordlist
int lfAddWordlist(const char* name) {
    if (wids_pointer >= WIDS_MAX) return ERR_WID_OVERFLOW;
    struct s_wid* w = &wids[wids_pointer];
    w->head = NULL;
    memset(w->name, 0, sizeof(w->name));
    if (name != NULL) {
        strncpy(w->name, name, sizeof(w->name) - 1);
    }
    return wids_pointer++;
}


/* CONTEXT array holds indices into the wids array, ordered by search priority.
   Terminated with -1 to indicate the end of the search order. ONLY and
   FORTH, which set it, are in api0.c. */

/* .WID  ( n -- ) */
int lfAPI_dotWid(void) {
    uint8_t wid = (uint8_t)vmPop();
    if (wid >= wids_pointer) return ERR_SEARCH_ORDER_OVERFLOW;
    char* s = wids[wid].name;
    if (s[0] == '\0')  return lfDot(wid);
    lf_puts(s); return lfSpace();
}

/* WORDS */
int lfAPI_words(void) {
    const char* name = NULL;
    const struct s_head* link = lfFollow(wids[CONTEXT[0]].head, &name);

    while (link != NULL) {
        if ((link->aux & A_SMUDGED) == 0) {
            if (link->aux & A_IMMEDIATE) {
                lfSetColor(COLOR_BRIGHT_YELLOW);
            }
            else if (link->w & W_PRIMITIVE) {
                lfSetColor(COLOR_GREEN);
            }
            else if (link->aux & A_CONSTANT) {
                lfSetColor(COLOR_BRIGHT_CYAN);
            }
            lf_puts(name);
            lfSetColor(COLOR_NORMAL);
            lfSpace();
        }
        link = lfFollow(link->link, &name);
    }
    return 0;
}

/* ========================================================================= */
/* LOOKUP FUNCTION                                                           */
/* ========================================================================= */

uint32_t g_neighbor_w = 0; // the w of the word defined after the found one

// Find a visible word in a wordlist by name (or any name, if target_name is
// NULL) that has all of aux_flags set. See lfSearchContext.
static const struct s_head* search_wordlist(int wid_index, const char *target_name,
                                            uint32_t aux_flags, const char** found_name) {
    if (wid_index < 0 || wid_index >= wids_pointer) {
        return NULL;
    }
    const char* name = NULL;
    const struct s_head *link = lfFollow(wids[wid_index].head, &name);
    while (link != NULL) {
        if (((target_name == NULL) || TheStringsMatch((char*)name, (char *)target_name))
            && ((link->aux & A_SMUDGED) == 0)
            && ((link->aux & aux_flags) == aux_flags)) {
            if (found_name != NULL) *found_name = name;
            return link;
        }
        g_neighbor_w = link->w;
        link = lfFollow(link->link, &name);
    }
    return NULL;
}

// lfSearchContext (documented in forth.h): index through the context
const struct s_head* lfSearchContext(const char* target_name, uint32_t aux_flags,
                                     const char** found_name) {
    for (int i = 0; i < CONTEXT_MAX; i++) {
        int wid_idx = CONTEXT[i];
        if (wid_idx == -1) break; // end of context
        const struct s_head *found = search_wordlist(wid_idx, target_name,
                                                     aux_flags, found_name);
        if (found != NULL) return found; // found it
    }
    return NULL; // Word not found in any active wordlist
}

// Find a word by name in the context
static const struct s_head* search_context(const char *target_name) {
    return lfSearchContext(target_name, 0, NULL);
}

// lfParseLabel (documented in forth.h)
const struct s_head* lfParseLabel(void) {
    int toin = lfTOINfetch();
    char token[32] = { 0 };
    const struct s_head* label = NULL;
    if (lfParseWord(token, sizeof(token)) == 0) {
        label = lfSearchContext(token, A_UNRESOLVED, NULL);
    }
    if (label == NULL) lfTOINstore(toin);   // leave the name for lfHeader
    return label;
}

// Traverse all wordlists in the context for the name of a value
char* lfFindLabel(uint32_t value, uint32_t mask, uint32_t must, uint32_t expected) {
    for (int i = 0; i < CONTEXT_MAX; i++) {
        int wid_idx = CONTEXT[i];
        if (wid_idx == -1) break; // end of context
        const char* name = NULL;
        const struct s_head* link = lfFollow(wids[wid_idx].head, &name);
        while (link != NULL) { // traverse the wordlist
            uint32_t xt = link->w;
            if ((xt & must) == expected) {
                xt &= mask;
                if (xt == value) {
                    if ((link->aux & A_SMUDGED) == 0) {
                        return (char*)name;
                    }
                }
            }
            link = lfFollow(link->link, &name);
        }
    }
    return NULL;
}

/*
* .S depends on "empty" marker VM_EMPTYSTACK
*/
int lfDotS(void) {
    int depth = vmPeek(VM_REG_sp);  // sp counts the items on the stack
    if (depth) {
        lf_puts("( ");
        if (depth > DOT_S_MAX) {
            lf_putc('[');
            lfDotB(depth, lfBASEfetch(), 0, 0);
            lf_puts("]... ");
            depth = DOT_S_MAX;
        }
        while (depth--) {
            lfDot(vmPeek(depth));
        }
        lf_puts(") ");
    }
    return 0;
}


/* =========================================================================
TIB is a fixed buffer in Forth data space for terminal input. >IN and BLK
describe the current input source; LOAD saves and restores them around
each block it interprets.
========================================================================= */

uint32_t g_lf_sys_options = 0;
static uint32_t linecount = 0;

/*
 * Reads one line from the terminal into TIB, NUL-terminated, and stores its
 * length (counting the NUL) in *length.
 *
 * While no input is waiting and the app is running (SYS_OPTION_RUNNING, set
 * by `cold`), the app's VM code runs from its PC until its next `break`.
 * If it fails instead (an error or `yeet`), the VM sends it to its yeet
 * handler, and it keeps running from there: the app handles its own errors,
 * as on a Forth chip. The exception is running VM_STEP_LIMIT steps without a
 * `break`: the app is stuck, so it is stopped and ERR_VM_TIMEOUT is returned,
 * ending the line early.
 *
 * With `stop-tib` (LF_DEADTIB) set, a running app has the keyboard: it runs
 * even while input is waiting, and the terminal reads nothing, so the app
 * gets every key (`key?`, `key`). Once the app stops, the terminal reads
 * again.
 *
 * Returns 0, ERR_TIB_OVERFLOW if the line did not fit in TIB (the rest of it
 * is discarded), or ERR_VM_TIMEOUT.
 */
// A running app with `stop-tib` set gets the keyboard (see loadTIB)
static int appHasKeys(void) {
    if (!(g_lf_sys_options & SYS_OPTION_RUNNING)) return 0;
    uint32_t deadtib = 0;
    vmFetch(LF_DEADTIB, &deadtib);
    return deadtib;
}

static int loadTIB(int* length) {
    char *tib = (char*)TIB; // reset the TIB pointer
    int remaining = TIBSIZE; // remaining space in TIB
    int ior = 0;

    while (1) {
        if (appHasKeys() || (serial_ready() < 1)) {
#ifdef yield2c
            yield2c(); // Yield to other tasks while waiting...
#endif
            if (g_lf_sys_options & SYS_OPTION_RUNNING) {
                int err = vmRun(0, VM_STEP_LIMIT, 0);
                if (err == ERR_VM_TIMEOUT) {    // stuck: stop the app
                    g_lf_sys_options &= ~SYS_OPTION_RUNNING;
                    ior = err;
                    break;
                }
            }
            continue;
        }
        int c = serial_getc();
        if (c < 0) {
            g_lf_sys_options &= ~(SYS_OPTION_NO_OK | SYS_OPTION_NO_DOTESS);
            break;                  // treat error as a normal terminator
        }
        if (c == '\r') {
            if (g_lf_sys_options & SYS_OPTION_IGNORE_CR) continue;
            break;
        }
        if (c == '\n') break;       // CRLF inserts lines
        if (c == 0xFF) c = ' ';     // replace 0xFF with blank
        if (remaining > 1) {
            *tib++ = (char)c;
            remaining--;
        }
        else {                      // ignore input remaining until EOL
            ior = ERR_TIB_OVERFLOW;
        }
    }
    *tib++ = 0;                     // Null-terminate the TIB
    remaining--;
    *length = TIBSIZE - remaining;
    return ior;
}

static void lfDotLinecount(void) {
    lf_puts("Line ");
    lfDot(linecount);
}

/*
parseStr assumes `str` is always correctly terminated with a null character.
It extracts the next terminator-delimited token from `str` and returns a pointer
to the next character after the token. If no token is found, it returns NULL.
The extracted token is stored in a static buffer for later use.
*/

static char* source = NULL; // current position in the input string
static int source_len = 0;  // explicit length bound for the current input stream

static char TOINchar(void) {
    int toin = lfTOINfetch();
    if (toin >= source_len) {
        return '\0';
    }
    return source[toin];
}

static void TOINbump(void) {
    lfTOINstore(1 + lfTOINfetch());
}

int lfParseWord(char* dest, int destSize) {
    int ior = 0;

    // 1. Skip leading whitespace
    while (1) {
        char c = TOINchar();
        if (c == '\0') break;
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        TOINbump();
    }

    // 2. Copy token characters
    int i = 0;
    while (1) {
        char c = TOINchar();
        if (c == '\0' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            break;
        }
        if (i < (destSize - 1)) {
            dest[i++] = c;
        }
        else {
            ior = ERR_PARSED_STRING_OVERFLOW;
        }
        TOINbump();
    }

    dest[i] = '\0';

    // 3. Skip single trailing delimiter IF we stopped on one (not EOF)
    char delimiter = TOINchar();
    if (delimiter == ' ' || delimiter == '\t' || delimiter == '\r' || delimiter == '\n') {
        TOINbump();
    }

    return ior;
}

static int load_depth = 0;      // number of LOADs being interpreted
static int traced = 0;          // a LOAD printed input trace lines
static char* lastparsed = NULL;
static void printTraceLine(void);

/*
 * Interprets the current input source (source, source_len, >IN, BLK) until
 * >IN reaches its end. LOAD calls this recursively for each block, so it
 * keeps no input frames of its own and prints no trace.
 * The token buffer is static to keep each level of recursion small on the C
 * stack. That is safe because a level never reads it again after executing
 * a word, and it leaves `lastparsed` naming the innermost failing token.
 */
static int interpretSource(void) {
    static char token[32];      // token buffer for parsing
    while (lfTOINfetch() < source_len) {
        int ior = lfParseWord(token, sizeof(token));
        lastparsed = token;
        if (ior) return ior;

        // No token: only whitespace remained, or the source hit a NUL.
        if (token[0] == '\0') break;

        // A. Check active wordlists
        const struct s_head* word = search_context(token);
        int state = lfSTATEfetch();

        if (word != NULL) {
            if (word->aux & A_IMMEDIATE) {
                if (state && (word->aux & A_NO_EXECUTE)) {
                    return ERR_INTERPRET_COMPILE_ONLY;
                }
                state = 0;
            }

            if (state) {
                ior = lfCompileWord(word);
            }
            else {
                ior = lfExecuteWord(word);
            }

            int base = lfBASEfetch();
            if (base < 2) {
                ior = ERR_INVALID_BASE;
                lfBASEstore(10);
            }
            if (ior) return ior;
            continue;
        }
        int32_t value;

        // B. Constant / Numeric fallback
        ior = findConstant(token, &value);
        if (ior) {
            ior = lfParseNumber(token, lfBASEfetch(), &value);
        }
        if (ior) return ior;    // undefined word or parsing error

        if (state) {
            lfCompileLit(value);
        }
        else {
            vmPush(value);
        }
    }
    return 0;
}

/*
 * Forth text interpreter (documented in forth.h).
 * Blocks are interpreted by LOAD, which calls interpretSource recursively.
 * On an error inside a block, each LOAD prints its line of the input trace
 * as the error unwinds, and the terminal line is printed here last.
 */
int lfInterpret(char* str, int len) {
    if (str == NULL || len == 0) {
        return 0;
    }

    source = str;
    source_len = len;
    lfTOINstore(0);
    BLK = 0;
    traced = 0;

    int ior = interpretSource();
    if (ior == 0) {
        lfAPI_emptyBuffers(); // so it will load external edits
    }
    else if (ior != ERR_QUIT && traced) {
        printTraceLine();
    }
    return ior;
}

/*
 * Resets the interpreter state before QUIT reads the first line, and again
 * after each error: color, BASE = 10 (and STATE, >IN etc. = 0), definitions
 * into the forth wordlist, line count, and empty stacks (SP and RP = 0).
 * The other VM registers keep their values; lfQuit resets the whole VM once,
 * at startup.
 */
static void quitReset(void) {
    lfSetColor(COLOR_NORMAL);
    LF_PACKEDSTATE[0] = 10; // most Forth state variables
    vmStore(LF_CURRENT, 0);
    linecount = 0;
    vmPoke(VM_REG_sp, 0);
    vmPoke(VM_REG_rp, 0);
}

/*
 * Shows the stack and the `ok>` prompt, unless turned off by the system
 * options. The prompt comes before the input for compatibility with cooked
 * input; the terminal echoes the newline locally.
 * Returns an error if the output stream is lost.
 */
static int prompt(void) {
    if ((g_lf_sys_options & SYS_OPTION_NO_DOTESS) == 0) {
        lfDotS();
    }
    if ((g_lf_sys_options & SYS_OPTION_NO_OK) == 0) {
        return lf_puts("ok>");
    }
    return 0;
}

/*
 * Checks the data stack depth after a line. sp counts items modulo
 * STACK_CAPACITY, so taking items from an empty stack wraps it to the top of
 * the range. If the upper 4 bits of sp are all set, the stack underflowed
 * (by up to STACK_CAPACITY / 16 items). If only the upper 3 are, it is
 * about to wrap and is reported as overflow. So a line may leave at most
 * STACK_CAPACITY * 7 / 8 - 1 items (111 for 128). Reading sp is a
 * dependency on the VM.
 */
#define SP_UPPER4 (STACK_MASK & ~(STACK_MASK >> 4))
#define SP_UPPER3 (STACK_MASK & ~(STACK_MASK >> 3))

static int checkStackDepth(void) {
    int depth = vmPeek(VM_REG_sp);
    if ((depth & SP_UPPER4) == SP_UPPER4) return ERR_STACK_UNDERFLOW;
    if ((depth & SP_UPPER3) == SP_UPPER3) return ERR_STACK_OVERFLOW;
    return 0;
}

/*
 * Reads one line from the terminal and interprets it. A line too long for
 * TIB is not interpreted at all, since running part of it could leave a
 * definition half-compiled. Nor is a line cut short by ERR_VM_TIMEOUT.
 */
static int interpretLine(void) {
    int len;
    linecount++;
    int ior = loadTIB(&len);
    if (g_lf_sys_options & SYS_OPTION_VERBOSE) {
        lfCR();
        lfDotLinecount();
        lf_puts(TIB);
    }
    if (ior) return ior;
    ior = lfInterpret((char*)TIB, len);
    if (ior) return ior;
    return checkStackDepth();
}

// Reports an error from interpretLine in decimal, in red.
static void reportError(int ior) {
    lfBASEstore(10);
    lfSetColor(COLOR_BRIGHT_RED);
    if (ior == ERR_UNDEFINED_WORD) {
        lf_puts(lastparsed);
        lf_puts(" ?");
        lfCR();
        return;
    }
    lf_puts("Error: ior=");
    lfDot(ior);
#if (FAT_FORTH & 1)
    lf_puts(get_error_message(ior));
    lfCR();
#endif
}

// QUIT (documented in forth.h). Open the terminal with serial_open first.
int lfQuit(void) {
    // u8 keeps the bytes UTF-8 on any compiler (MSVC without /utf-8 would
    // convert a plain literal to the code page). In C23 u8 strings are
    // unsigned char (char8_t), hence the cast.
    lf_puts((const char*)u8"[幸运狐] v");
    lfDotB(TF_VERSION, 10, 2, 3);
    lfCR();
    lfAPI_empty();
    lfAPI_only();
    lfAPI_forth();
    vmReset();
    if (g_lf_sys_options & SYS_OPTION_BOOTING) {
        // Restore the wordlists saved by save-wids. Without a valid record
        // (e.g. a new, blank flash file), quietly keep the defaults and
        // don't start the app.
        if (lfBootFromFlash() == 0) {
            vmReset();
            if (!(g_lf_sys_options & SYS_OPTION_NO_AUTORUN)) {
                g_lf_sys_options |= SYS_OPTION_RUNNING; // start the app
            }
        }
    }
    while (1) {
        quitReset();
        int ior;
        do {
            ior = prompt();
            if (ior) return ior;        // lost the output stream
            ior = interpretLine();
        } while (ior == 0);

        if (ior == ERR_QUIT) {          // bye, unless a label is unresolved
            const char* name = NULL;
            if (lfSearchContext(NULL, A_UNRESOLVED, &name) == NULL) return 0;
            lf_puts(name);
            lfSpace();
            reportError(ERR_UNRESOLVED_LATER);
            return ERR_UNRESOLVED_LATER;
        }
        reportError(ior);
        if (g_lf_sys_options & SYS_OPTION_VALIDATION) {
            lfDotLinecount();
            return ior;                 // quit after the first error
        }
    }
}

/* Tick primitive */
const struct s_head* lfTickWord(void) {
    char token[32] = { 0 };
    int ior = lfParseWord(token, sizeof(token));
    if (ior) return NULL;
    const struct s_head* word = search_context(token);
    if (word == NULL) return NULL;
    return word;
}

static uint32_t latest = 0;             // VM address of the last header, or 0

// Modify the last created header (immediate, etc.)
int lfToHeader(uint32_t w, uint32_t aux) {
    struct s_head* h = (latest) ? (struct s_head*)lfVmBytes(latest) : NULL;
    if (h == NULL) return ERR_UNSUPPORTED_OPERATION;
    h->w |= w;
    h->aux ^= aux;
    return 0;
}

/*
 * SAVE-WIDS  ( -- )  (documented in forth.h)
 * Heads are links (see lfFollow), not C pointers, so the record is valid
 * wherever the flash page is mapped.
 */
int lfAPI_saveWids(void) {
    int32_t tp = 0;
    lfTpFetch(&tp);
    tp = (int32_t)lfSetSliceWidth((uint32_t)tp, 32);
    int n = wids_pointer;
    int bytes = n * (int)sizeof(struct s_wid);
    int cells = WIDS_RECORD_TABLE + (bytes + 3) / 4;
    int ior = lfTpStore(tp + cells);    // fails if text space is too small
    if (ior) return ior;
    ior = vmStore(tp + WIDS_RECORD_SKIP, tp + cells);
    if (!ior) ior = vmStore(tp + WIDS_RECORD_COUNT, n);
    for (int i = WIDS_RECORD_TABLE; (i < cells) && !ior; i++) {
        int32_t x = 0;
        int offset = (i - WIDS_RECORD_TABLE) * 4;
        int len = bytes - offset;
        memcpy(&x, (char*)wids + offset, (len < 4) ? len : 4);
        ior = vmStore(tp + i, x);
    }
    return ior;
}

/*
 * Boot from flash (lfBootFromFlash, documented in forth.h).
 * A head is kept if it's a link to a header in mapped VM memory or to a
 * built-in list; anything else (such as a C pointer from another build)
 * can't be resolved and is set to NULL.
 */
static const struct s_head* checkHead(const struct s_head* link) {
    uintptr_t v = (uintptr_t)link;
    if (v == 0) return NULL;
    if (v & 1) return (lfFollow(link, NULL) != NULL) ? link : NULL;
    if (v & 2) return ((v >> 2) < EMPTY_WIDS) ? link : NULL;
    return NULL;
}

int lfBootFromFlash(void) {
    uint32_t addr = 0;
    if (vmFetch(1, &addr)) return ERR_BAD_BOOT_RECORD;
    int page = (addr >> (22 - VM_LOG2_PAGES)) & (VM_MEM_PAGES - 1);
    uint32_t offset = addr & VM_PAGE_MASK;
    if ((addr >> 22) || (page >= RAM_PAGE) || (vm_memory[page] == NULL))
        return ERR_BAD_BOOT_RECORD;     // not a cell address in flash
    uint32_t limit = vm_memory_rd_limit[page];
    if (offset + WIDS_RECORD_TABLE > limit) return ERR_BAD_BOOT_RECORD;
    uint32_t* record = &vm_memory[page][offset];
    int n = record[WIDS_RECORD_COUNT];
    if ((n < 1) || (n > WIDS_MAX)) return ERR_BAD_BOOT_RECORD;
    uint32_t bytes = (uint32_t)n * sizeof(struct s_wid);
    uint32_t cells = WIDS_RECORD_TABLE + (bytes + 3) / 4;
    if ((offset + cells > limit) || ((uint32_t)record[WIDS_RECORD_SKIP] != addr + cells))
        return ERR_BAD_BOOT_RECORD;

    lfResetWids();
    memcpy(wids, &record[WIDS_RECORD_TABLE], bytes);
    for (int i = 0; i < n; i++) {
        wids[i].name[sizeof(wids[i].name) - 1] = '\0';
        wids[i].head = checkHead(wids[i].head);
    }
    wids_pointer = n;
    latest = 0;
    return 0;
}

// Create a header structure in Forth memory space
int lfHeader(uint32_t w, uint32_t aux, uint32_t* name) {
    char token[32] = { 0 };
    int ior = lfParseWord(token, sizeof(token));
    if (ior) return ior;

    // Resolve destination memory location in 32-bit cells
    uint32_t* textptr = &vm_memory[RAM_PAGE][F_PTRS_TP];
    int32_t  f_hp = *textptr;
    int32_t  f_hmax = textptr[1];
    int page = (f_hp & 0x3FFFFF) >> (22 - VM_LOG2_PAGES);
    int32_t ch_dest = lfSetSliceWidth(f_hp, 8); // LF byte address for name
    uint32_t dest = ch_dest & VM_PAGE_MASK; // cell index within page

    uint32_t* cell_dest = &vm_memory[page][dest];

    // Name string starts here: the header holds its VM byte address
    uint32_t name_dest = (uint32_t)ch_dest;
    if (name != NULL) {
        *name = name_dest;
    }
    char* src = token;
    char c = 0;

    // Use VM functions to respect the sandbox
    do {
        c = *src++;
        int ior = vmStore(ch_dest, c);
        if (ior) return ior;
        ch_dest = vmFieldPlus(ch_dest);
    } while (c);
    /*
    * vmStore did not give an error, assume struct s_head will not step on
    * anything critical. Pad with 0 bytes to cell-align.
    */
    while ((ch_dest >> 22) & 0x1F) {
        vmStore(ch_dest, 0);
        ch_dest = vmFieldPlus(ch_dest);
    }
    ch_dest &= VM_PAGE_MASK;
    cell_dest = &vm_memory[page][ch_dest];

    // Construct struct s_head header directly in the next cell boundary
    struct s_head* target_head = (struct s_head*)cell_dest;
    uint32_t head_addr = (page << (22 - VM_LOG2_PAGES)) | ch_dest;
    latest = head_addr;
    target_head->name = (char*)(uintptr_t)name_dest;
    target_head->w = w;
    uint8_t mywid = *CURRENT;
    target_head->aux = aux | mywid;

    // Insert header at top of current wordlist (linked list). Links are
    // tagged values (see lfFollow), so they stay valid in flash.
    s_wid* current = &wids[mywid];
    target_head->link = (struct s_head*)current->head;
    current->head = linkToVM(head_addr);

    // Advance cell_dest past the struct s_head
    int32_t head_cells = (sizeof(struct s_head) + sizeof(int32_t) - 1) / sizeof(int32_t);
    cell_dest += head_cells;

    // Calculate new f_hp address (start_f_hp + total cell delta)
    int32_t total_cells_used = (int32_t)(cell_dest - &vm_memory[page][dest]);
    int32_t new_f_hp = (page << (22 - VM_LOG2_PAGES)) | ((dest + total_cells_used) & 0x3FFFFF);

    // Bounds check before writing back to F_PTRS
    if (new_f_hp >= f_hmax) {
        return ERR_DICTIONARY_OVERFLOW;
    }

    *textptr = new_f_hp;
    return 0;
}

/*
 * lfParseInputString is a input tool that parses input up to a terminator
 * or the end of input. Each character, after conversion by an escape FSM,
 * is output to callback function `echo`.
 */

static const char* esc_chars0 = "abefnrtv";
static const char* esc_chars1 = "\a\b\x1b\f\n\r\t\v";

int lfParseInputString(putcfunc* echo, char terminator) {
    int escaped = 0;
    int ior = 0;
    while (1) {
        char c = TOINchar();
        if (c == '\0') break;
        TOINbump();

        if (escaped) {
            escaped = 0;
            int i = 0;
            char b;
            while ((b = esc_chars0[i])) {
                if (b == c) {
                    c = esc_chars1[i];
                    break;
                } i++;
            }
        }
        else if (c == '\\') {
            escaped = 1;
            continue;
        }
        else if (c == terminator) {
            break;
        }
        if (echo) {
            ior = echo(c);
        }
    }
    return ior;
}

// Make block `blk`, whose buffer is at Forth address `f_addr`, the input
// source, starting at its beginning.
static void setBlockSource(int32_t blk, int32_t f_addr) {
    source = (char*)&vm_memory[RAM_PAGE][f_addr & VM_PAGE_MASK];
    source_len = sizeof(int32_t) * BLOCK_SIZE_CELLS;
    lfTOINstore(0);
    BLK = blk;
}

/**
 * LOAD  ( blk -- )
 * Interprets block blk by calling interpretSource recursively. The caller's
 * input source, BLK and >IN are kept in locals and restored afterwards.
 * Nested loads may reuse the caller's block buffer, so a block caller's
 * buffer is assigned again on the way out. Every level therefore finds its
 * own block resident when interpretSource returns to it.
 */
int lfAPI_load(void) {
    int32_t blk = vmPop();
    if (blk == 0) return ERR_INVALID_BLOCK_NUMBER;
    if (load_depth >= MAX_LOAD_NESTING) {
        return ERR_STACK_OVERFLOW;
    }
    // Read the block before touching the input source, so a failed read
    // leaves the current input stream intact.
    int32_t f_addr = 0;
    int ior = lfAssignBlock(blk, &f_addr);
    if (ior) return ior;

    char* caller_source = source;
    int caller_len = source_len;
    int caller_toin = lfTOINfetch();
    int32_t caller_blk = BLK;

    setBlockSource(blk, f_addr);
    load_depth++;
    ior = interpretSource();
    load_depth--;

    // On an error, print the line of this block where it happened (or where
    // a nested LOAD was called) before the caller's input comes back.
    if (ior != 0 && ior != ERR_QUIT) {
        printTraceLine();
        traced = 1;
    }

    source = caller_source;
    source_len = caller_len;
    lfTOINstore(caller_toin);
    BLK = caller_blk;
    if (caller_blk != 0) {
        int err = lfAssignBlock(caller_blk, &f_addr);
        source = (char*)&vm_memory[RAM_PAGE][f_addr & VM_PAGE_MASK];
        if (ior == 0) ior = err;
    }
    return ior;
}

// Print one line of the input trace: where the current source stopped.
static void printTraceLine(void) {
    int toin = lfTOINfetch();
    if (toin > source_len) toin = source_len;
    int row = (toin / SCREEN_COLUMNS) + 1;
    int col = (toin % SCREEN_COLUMNS) + 1;

    int line_start = toin;
    while (line_start > 0 && source[line_start - 1] != '\n' && source[line_start - 1] != '\r') {
        line_start--;
    }

    if (BLK) {
        lf_puts("Screen ");
        lfDot10(BLK);
    }
    else {
        lf_puts("Terminal");
    }
    lf_puts(" [");
    lfDot10(row);
    lf_putc(':');
    lfDot10(col);
    lf_puts("] ");

    for (int p = line_start; p < toin; p++) {
        lf_putc(source[p]);
    }
    lfCR();
}

/**
 * --> ( -- )
 * Stop interpreting the current block and continue with block BLK + 1.
 * The next block replaces the current one in place (a tail call), so a
 * chain of screens does not use up LOAD nesting. The LOAD that is running
 * restores its caller when the last block of the chain ends.
 */
int lfAPI_nextBlock(void) {
    if (BLK == 0) {
        return ERR_INVALID_BLOCK_NUMBER;
    }
    int32_t f_addr = 0;
    int ior = lfAssignBlock(BLK + 1, &f_addr);
    if (ior) return ior;
    setBlockSource(BLK + 1, f_addr);
    return 0;
}
