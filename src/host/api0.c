#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include "utils.h"

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

/* `.` */
static int dot(void) {
    return lfDot(vmPop());
}

/* `.S` */
static int dotEss(void) {
    return lfDotS();
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
    uint32_t divisorS = (uint32_t)vmPop();
    if (divisorS == 0) return ERR_DIVISION_BY_ZERO;
    uint32_t dividendH = (uint32_t)vmPop();
    uint32_t dividendL = (uint32_t)vmPop();
    uint64_t dividend = ((uint64_t)dividendH << 32) | dividendL;
    uint64_t divisor = (uint64_t)divisorS;
    uint64_t q = dividend / divisor;
    vmPush((uint32_t)(dividend % divisor));
    vmPush((uint32_t)q);
    vmPush((uint32_t)(q >> 32));
    return 0;
}

// */MOD ( n multiplier divisor -- rem quot )
// : */  */MOD NIP ;
static int stardivmod(void) {
    int32_t divisorS = (uint32_t)vmPop();
    if (divisorS == 0) return ERR_DIVISION_BY_ZERO;
    int32_t multiplier = (uint32_t)vmPop();
    int32_t n = (uint32_t)vmPop();
    int64_t d = (int64_t)n * (int64_t)multiplier;
    vmPush((uint32_t)(int32_t)(d % divisorS));
    vmPush((uint32_t)(int32_t)(d / divisorS));
    return 0;
}

/* ,LIT */
static int commaLit(void) {
    return lfCompileLit(vmPop());
}

typedef int(*APIfn) (void);

static const APIfn API0fns[] = {
    bye, lfAPI_words, lfAPI_forth, lfAPI_only, umstar,
    mstar, mudivmod, stardivmod, qkey, key, 
    emit, lfAPI_header, lfAPI_setFlags, lfAPI_getFlags, lfAPI_paren, 
    lfAPI_dotParen, dotEss, dot, lfCR, lfSpace, 
    lfAPI_dotWid, lfAPI_tickx, tickpage, commaLit, bye
#if (FAT_FORTH & 1)
    , lfAPI_endTest, lfAPI_doTest, lfAPI_beginTest, lfAPI_hex, lfAPI_decimal
    , lfAPI_dotPage, lfAPI_dotPages, lfAPI_dump, lfAPI_dumpIns, lfAPI_dasm
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
