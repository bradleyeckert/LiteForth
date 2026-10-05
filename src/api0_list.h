#ifndef API0_LIST_H
#define API0_LIST_H

#include "options.h"

/**
 * @file api0_list.h
 * @brief The list of API 0 functions: the single source of their indices.
 *
 * Each entry is X(id, function). The position of an entry is the index the
 * API0 instruction carries, and compiled code (including code saved in
 * flash images) depends on it. So:
 *
 *   - Only append new entries to the end of API0_LIST.
 *   - Never reorder, remove or reuse an entry. To retire a function, point
 *     its entry at a stub that returns ERR_INVALID_API_CALL.
 *
 * API0_TOOLS_LIST holds the optional tools. It is only present when
 * FAT_FORTH is enabled and always comes after API0_LIST, so the core
 * indices are the same in every build. Adding to API0_LIST therefore
 * shifts the tools' indices; code that uses the tools must be recompiled.
 *
 * api0.h turns the list into an enum of indices (API_BYE = 0, ...) and
 * api0.c into the function table. The unit_tests/api0 suite checks the
 * order against a committed copy.
 */

#define API0_LIST(X)                            \
    /* Terminal I/O first, indices 0 to 3 (API_T_RXQ to API_T_TXQ), so */ \
    /* that a restricted mode can allow only these calls.              */ \
    X(API_T_RXQ,            qkey)               \
    X(API_T_RX,             key)                \
    X(API_T_TXSTORE,        emit)               \
    X(API_T_TXQ,            qemit)              \
    X(API_BYE,              bye)                \
    X(API_WORDS,            lfAPI_words)        \
    X(API_FORTH,            lfAPI_forth)        \
    X(API_ONLY,             lfAPI_only)         \
    X(API_UMSTAR,           umstar)             \
    X(API_MSTAR,            mstar)              \
    X(API_MUDIVMOD,         mudivmod)           \
    X(API_STARDIVMOD,       stardivmod)         \
    X(API_COLON,            lfAPI_colon)        \
    X(API_SEMICOLON,        lfAPI_semicolon)    \
    X(API_TOOPTIONS,        lfAPI_setFlags)     \
    X(API_EMPTY,            lfAPI_empty)        \
    X(API_PAREN,            parenthesis)        \
    X(API_DOTPAREN,         dotParen)           \
    X(API_DOES,             lfAPI_dotDoes)      \
    X(API_CREATE,           lfAPI_dotCreate)    \
    X(API_CR,               lfCR)               \
    X(API_LITERAL,          lfAPI_literal)      \
    X(API_DOTWID,           lfAPI_dotWid)       \
    X(API_XTICK,            extick)             \
    X(API_PAGE,             tickpage)           \
    X(API_WORDLIST,         wordlist)           \
    X(API_OPEN_FLASH,       flashOpen)          \
    X(API_CLOSE_FLASH,      flashClose)         \
    X(API_RBRACKET,         endbracket)         \
    X(API_LBRACKET,         bracket)            \
    X(API_EXIT,             lfAPI_exit)         \
    X(API_CONSTANT,         lfAPI_constant)     \
    X(API_BITS,             lfAPI_bits)         \
    X(API_TOBODY,           lfAPI_toBody)       \
    X(API_COMMAQUOTE,       commaQ)             \
    X(API_BIT,              lfAPI_bit)          \
    X(API_COMMAINST,        lfAPI_inst)         \
    X(API_IMMEDIATE,        immediate)          \
    X(API_BLOCK,            lfAPI_block)        \
    X(API_BUFFER,           lfAPI_buffer)       \
    X(API_UPDATE,           lfAPI_update)       \
    X(API_SAVE_BUFFERS,     lfAPI_saveBuffers)  \
    X(API_FLUSH,            lfAPI_flush)        \
    X(API_EMPTY_BUFFERS,    lfAPI_emptyBuffers) \
    X(API_LOAD,             lfAPI_load)         \
    X(API_CAPACITY,         capacity)           \
    X(API_NEXTBLOCK,        lfAPI_nextBlock)    \
    X(API_POSTPONE,         lfAPI_postpone)     \
    X(API_COMPILE,          lfAPI_compile)      \
    X(API_NEWINST,          lfAPI_newinst)      \
    X(API_COLD,             coldboot)           \
    X(API_NONAME,           lfAPI_noname)       \
    X(API_EXECUTE,          execute)            \
    X(API_SAVE_WIDS,        lfAPI_saveWids)     \
    X(API_LABEL,            lfAPI_label)        \
    X(API_TOAUX,            toaux)

#if (FAT_FORTH & 1)
#define API0_TOOLS_LIST(X)                      \
    X(API_ENDTEST,          lfAPI_endTest)      \
    X(API_DOTEST,           lfAPI_doTest)       \
    X(API_BEGINTEST,        lfAPI_beginTest)    \
    X(API_HEX,              lfAPI_hex)          \
    X(API_DECIMAL,          lfAPI_decimal)      \
    X(API_DOTPAGE,          lfAPI_dotPage)      \
    X(API_DOTPAGES,         lfAPI_dotPages)     \
    X(API_DUMP,             lfAPI_dump)         \
    X(API_DUMPI,            lfAPI_dumpIns)      \
    X(API_DASM,             lfAPI_dasm)         \
    X(API_DOTS,             lfAPI_dotEss)       \
    X(API_DOT,              lfAPI_dot)          \
    X(API_SEE,              lfAPI_see)
#else
#define API0_TOOLS_LIST(X)
#endif

#endif /* API0_LIST_H */
