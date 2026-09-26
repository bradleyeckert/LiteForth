#include <string.h>
#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include "utils.h"
#include "memalloc.h"
#include "flash.h"
#include "comp.h"
#include "tools.h"
#include "api0.h"
#include "lfblocks.h"

/*==========================================================================
* API 0 
*=========================================================================*/

/* BYE */
static int bye(void) {
    return ERR_QUIT;
}

/* EMIT */
static int emit(void) {
    return serial_putc((char)vmPop());
}

/* KEY? */
static int qkey(void) {
    int flag = serial_ready();
    return vmPush(flag);
}

/* KEY */
static int key(void) {
    int c = serial_getc();
    return vmPush(c);
}

/* `'PAGE` */
static int tickpage(void) {
    int32_t val = vmPop();
    val = val << (22 - VM_LOG2_PAGES);
    return vmPush(val);
}

static int umstar_x(int sign) {
    uint32_t a = (uint32_t)vmPeek(0);
    uint32_t b = (uint32_t)vmPeek(1);
    int invert = 0;
    if (sign) {
        invert = (a ^ b) & 0x80000000;
        if (a & 0x80000000) a = 0 - a;
        if (b & 0x80000000) b = 0 - b;
    }
    uint64_t p = (uint64_t)a * (uint64_t)b;
    if (invert) {
        p = 0 - p;
    }
    vmPoke(1, (int32_t)(p & 0xFFFFFFFF));
    vmPoke(0, (int32_t)(p >> 32));
    return 0;
}

/* UM* */
static int umstar(void) {
    return umstar_x(0);
}

/* M* */
static int mstar(void) {
    return umstar_x(1);
}

/* MU/MOD ( dividendL dividendH divisor -- rem ql qh )
* Used for numeric conversion and as a primitive for UM/MOD
* : UM/MOD  MU/MOD ROT DROP ;
*/
static int mudivmod(void) {
    uint32_t divisor = (uint32_t)vmPop();
    if (divisor == 0) return ERR_DIVISION_BY_ZERO;

    uint32_t dividendH = (uint32_t)vmPop();
    uint32_t dividendL = (uint32_t)vmPop();
    uint64_t dividend = ((uint64_t)dividendH << 32) | (uint64_t)dividendL;
    uint32_t rem = 0;

    uint64_t quotient = divide64by32(dividend, divisor, &rem);

    // Push remainder, quotient low, quotient high
    vmPush((uint32_t)rem);
    vmPush((uint32_t)quotient);
    vmPush((uint32_t)(quotient >> 32));

    return 0;
}

// */MOD ( n multiplier divisor -- rem quot )
// : */  */MOD NIP ;
static int stardivmod(void) {
    int32_t divisorS = (int32_t)vmPop();
    if (divisorS == 0) return ERR_DIVISION_BY_ZERO;

    int32_t multiplier = (int32_t)vmPop();
    int32_t n = (int32_t)vmPop();

    // Compute 64-bit full product
    int64_t d = (int64_t)n * (int64_t)multiplier;

    // Determine signs
    int dividend_negative = (d < 0);
    int divisor_negative = (divisorS < 0);
    int quot_negative = dividend_negative ^ divisor_negative;

    // Get absolute values (64-bit dividend, 32-bit divisor)
    uint64_t u_dividend = dividend_negative ? (uint64_t)(-d) : (uint64_t)d;
    uint32_t u_divisor = divisor_negative ? (uint32_t)(-divisorS) : (uint32_t)divisorS;

    uint32_t u_remainder = 0;

    uint64_t u_quotient = divide64by32(u_dividend, u_divisor, &u_remainder);

    // Apply signs (Symmetric division: remainder takes sign of dividend)
    int32_t quotient = quot_negative ? -(int32_t)u_quotient : (int32_t)u_quotient;
    int32_t remainder = dividend_negative ? -(int32_t)u_remainder : (int32_t)u_remainder;

    vmPush((uint32_t)remainder);
    vmPush((uint32_t)quotient);

    return 0;
}

int openpage = -1;
int32_t* cache = NULL;
int32_t* flash = NULL;

/* CLOSE-FLASH  ( -- ) */
static int flashClose(void) {
    if (openpage < 0) return 0; // already closed
    if (openpage >= RAM_PAGE) return ERR_FLASH_INVALID_SECTOR;
    if (flash == NULL) return ERR_FLASH_INVALID_SECTOR;

    // 1. Calculate base pointer for this page in flash memory
    int32_t* flash_page_ptr = flash + (openpage * FLASH_PAGE_CELLS);

    // 2. Restore vm_memory page pointer back to backing flash memory
    vm_memory[openpage] = flash_page_ptr;
    vm_memory_wp_limit[openpage] = FLASH_PAGE_CELLS; // write-protect

    // 3. Persist RAM cache contents to disk/flashmem
    int ior = flash_program((uint32_t*)cache, openpage);

    openpage = -1;

    // 4. Burn (zero-fill) the cache buffer before freeing
    if (cache != NULL) {
        memset(cache, 0, FLASH_PAGE_CELLS * sizeof(int32_t));
    }

    // 5. Free allocated cache memory and check for errors
    int free_res = pool_free(cache);
    cache = NULL;

    if (free_res != 0) return free_res;
    if (ior != 0) return ERR_FLASH_INVALID_SECTOR;

    return 0;
}

/* OPEN-FLASH  ( addr -- ) */
static int flashOpen(void) {
    int ior = 0;
    int32_t addr = vmPop();
    int page = (addr & 0x3FFFFF) >> (22 - VM_LOG2_PAGES);

    if (page >= RAM_PAGE) return ERR_FLASH_INVALID_SECTOR;

    // If a page is already open, flush/close it first
    if (openpage >= 0) {
        if (openpage == page) return 0; // Already open
        ior = flashClose();
        if (ior) return ior;
    }

    cache = pool_alloc(FLASH_PAGE_CELLS);
    if (!cache) return ERR_ALLOCATE_FAILED; // Guard against allocation failure

    flash = vm_memory[page]; // the currently closed flash page

    // Copy backing flash memory contents into RAM cache
    memcpy(cache, flash, FLASH_PAGE_CELLS * sizeof(int32_t));

    // Point vm_memory page to RAM cache and remove write protection
    vm_memory[page] = cache;
    vm_memory_wp_limit[page] = 0;

    openpage = page;
    return 0;
}

/* [  ( -- ) */
static int bracket(void) {
    return lfSTATEstore(1);
}

/* ]  ( -- ) */
static int endbracket(void) {
    return lfSTATEstore(0);
}

/* WORDLIST  ( -- wid ) */
static int wordlist(void) {
    int ior = lfAddWordlist(lfCreatedName);
    if (ior < 0) return ior;
    lfCreatedName = NULL;
    return vmPush(ior);
}

/* IMMEDIATE  ( -- ) */
static int immediate(void) {
    lfToHeader(0, A_IMMEDIATE);
    return 0;
}

uint32_t g_block_capacity = 0;

/* |BLOCKS|  ( -- ) */
static int capacity(void) {
    vmPush(g_block_capacity);
    return 0;
}

typedef int(*APIfn) (void);

static const APIfn API0fns[] = {
    bye, lfAPI_words, lfAPI_forth, lfAPI_only, umstar,
    mstar, mudivmod, stardivmod, qkey, key, 
    emit, lfAPI_colon, lfAPI_semicolon, lfAPI_setFlags, lfAPI_getFlags,
    lfAPI_paren, lfAPI_dotParen, lfAPI_dotDoes, lfAPI_dotCreate, lfCR,
    lfAPI_here, lfAPI_dotWid, lfAPI_tickx, tickpage, wordlist,
    flashOpen, flashClose, endbracket, bracket, lfAPI_exit,
    lfAPI_constant, lfAPI_bits, lfAPI_toBody, lfAPI_comma, lfAPI_bit,
    lfAPI_inst, immediate, lfAPI_block, lfAPI_buffer, lfAPI_update,
    lfAPI_saveBuffers, lfAPI_flush, lfAPI_emptyBuffers, lfAPI_load, capacity,
    lfAPI_nextBlock
#if (FAT_FORTH & 1)
    , lfAPI_endTest, lfAPI_doTest, lfAPI_beginTest, lfAPI_hex, lfAPI_decimal
    , lfAPI_dotPage, lfAPI_dotPages, lfAPI_dump, lfAPI_dumpIns, lfAPI_dasm
    , lfAPI_dotEss, lfAPI_dot, lfAPI_see
#endif
};

// VMapi0Call and VMapi1Call are exported to vm.c

#define API0fs ((int)(sizeof(API0fns)/sizeof(API0fns[0])))

int VMapi0Call(int fn) {
    if (fn < API0fs) {
        return API0fns[fn]();
    }
    return ERR_INVALID_API_CALL;
}

/*
* API 1 (external)
*/

int VMapi1Call(int fn) {
    (void)fn;
    return ERR_INVALID_API_CALL;
}
