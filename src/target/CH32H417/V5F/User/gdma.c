/*
 * Guarded DMA: user mode's DMA through a machine-mode service. See gdma.h.
 */
#include <string.h>
#include "ch32h417.h"
#include "gdma.h"
#include "guard.h"
#include "pmp.h"                        /* pmp_call3 */
#include "../../../../errcodes.h"

/* PMP entry 2's block (pmp.c) holds every DMA register, and the store
   emulation's windows are RCC and CRC (guard.c) */
_Static_assert(DMA1_BASE >= GUARD_BLOCK_BASE
               && DMAMUX_BASE + sizeof(DMAMUX_TypeDef) <= GUARD_BLOCK_BASE + GUARD_BLOCK_SIZE,
               "DMA1, DMA2 and DMAMUX must lie in PMP entry 2's block");
_Static_assert(RCC_BASE == GUARD_RCC_BASE && CRC_BASE == GUARD_CRC_BASE,
               "guard.h's RCC and CRC windows");
_Static_assert(GDMA_BUF_BYTES / 4 <= 0xFFFFu, "CNTR counts words in 16 bits");

/* A channel the service may program: everything about it but the length
   and direction is fixed here */
typedef struct {
    DMA_Channel_TypeDef *chan;
    DMA_TypeDef *dma;
    uint32_t flags;                     /* its GL, TC, HT and TE flags */
    uint32_t tc, te;
    uint32_t periph;                    /* the peripheral register */
    uint32_t cfgr;                      /* CFGR, but for DIR and EN */
    uint8_t mux;                        /* DMAMUX channel (WCH's 0-based number) */
    uint8_t request;                    /* DMAMUX request, 1-based as in the RM */
} channel;

/* 32-bit words on both sides, the memory side incrementing, no
   memory-to-memory, no circular or double-buffer mode */
#define CFGR_WORDS  (DMA_PeripheralInc_Disable | DMA_MemoryInc_Enable \
                     | DMA_PeripheralDataSize_Word | DMA_MemoryDataSize_Word \
                     | DMA_Mode_Normal | DMA_Priority_High | DMA_M2M_Disable)

static const channel channels[GDMA_CHANNELS] = {
    /* The microSD card (blocks.c): DMA2 channel 1, through DMAMUX channel
       9 (DMA_MuxChannel9), request 111 (SDIO; ch32h417-notes.md). WCH's
       SDIO driver uses DMA1 channel 1, which the V3F's UART code also
       uses, so this one doesn't. */
    [GDMA_SDIO] = {
        DMA2_Channel1, DMA2,
        DMA2_FLAG_GL1 | DMA2_FLAG_TC1 | DMA2_FLAG_HT1 | DMA2_FLAG_TE1,
        DMA2_FLAG_TC1, DMA2_FLAG_TE1,
        (uint32_t)&SDIO->FIFO, CFGR_WORDS, DMA_MuxChannel9, 111
    },
};

/* ---- User mode ---------------------------------------------------------- */

int gdma_start(int ch, int dir, const uint32_t *buf, uint32_t bytes) {
    uint32_t a = (ch >= 0 && ch < 256 && (dir == GDMA_TO_PERIPH || dir == GDMA_FROM_PERIPH))
                 ? (uint32_t)ch | (uint32_t)dir << 8 : 0xFFFFFFFFu;
    return pmp_call3(GDMA_SVC_START, a, (uint32_t)buf, bytes);
}

int gdma_poll(int ch) {
    if (ch < 0 || ch >= GDMA_CHANNELS) {
        return ERR_INVALID_NUMERIC;
    }
    const channel *k = &channels[ch];
    uint32_t fl = k->dma->INTFR;        /* a read: allowed */
    if (fl & k->te) {
        return ERR_BUS_FAULT;
    }
    return (fl & k->tc) ? 0 : 1;
}

uint32_t gdma_left(int ch) {
    return (ch >= 0 && ch < GDMA_CHANNELS) ? channels[ch].chan->CNTR : 0;
}

int gdma_finish(int ch, uint32_t *buf, uint32_t bytes) {
    uint32_t a = (ch >= 0 && ch < 256) ? (uint32_t)ch : 0xFFFFFFFFu;
    return pmp_call3(GDMA_SVC_FINISH, a, (uint32_t)buf, buf ? bytes : 0);
}

/* ---- Machine mode ------------------------------------------------------- */

/* The only memory any channel here ever points at */
static uint32_t dma_buf[GDMA_BUF_BYTES / 4] __attribute__((aligned(32)));

/* Link_v5f.ld: DTCM's .data and .bss (and the unused heap) run from
   _data_vma up to the stack at _susrstack; memalloc.c's pool is in
   general-purpose SRAM, _pool_start to _pool_end, where nothing of
   machine mode is */
extern uint32_t _data_vma[], _susrstack[], _pool_start[], _pool_end[];

static int user_buffer_ok(uint32_t p, uint32_t n) {
    uint32_t x = (uint32_t)dma_buf;
    return guard_buffer_ok(p, n, (uint32_t)_data_vma, (uint32_t)_susrstack,
                           x, x + GDMA_BUF_BYTES)
        || guard_buffer_ok(p, n, (uint32_t)_pool_start, (uint32_t)_pool_end, 0, 0);
}

/* memcpy without calling it: this file runs in machine mode, from flash,
   and must call nothing user mode can write (mmode.h). Volatile, so the
   compiler doesn't turn the loop back into a memcpy call. */
static void copy(void *dest, const void *src, uint32_t bytes) {
    volatile uint8_t *d = dest;
    const volatile uint8_t *s = src;
    if ((((uintptr_t)d | (uintptr_t)s | bytes) & 3u) == 0) {
        volatile uint32_t *dw = (volatile uint32_t *)d;
        const volatile uint32_t *sw = (const volatile uint32_t *)s;
        for (uint32_t i = 0; i < bytes / 4; i++) {
            dw[i] = sw[i];
        }
        return;
    }
    for (uint32_t i = 0; i < bytes; i++) {
        d[i] = s[i];
    }
}

static int size_ok(uint32_t bytes) {
    return bytes >= 4 && bytes <= GDMA_BUF_BYTES && (bytes & 3u) == 0;
}

/* Sets DMAMUX channel mux's request field (request - 1, as
   DMA_MuxChannelConfig writes it; 0 to clear it) */
static void mux_field(uint8_t mux, uint32_t field) {
    volatile uint32_t *r = &DMAMUX->CFGR0_3 + (mux / 4u);
    uint32_t shift = (mux % 4u) * 8u;
    *r = (*r & ~(0x7Fu << shift)) | ((field & 0x7Fu) << shift);
}

/* Stops channel k and forgets everything about the transfer: its
   registers, flags and DMAMUX request, and the whole buffer */
static void burn(const channel *k) {
    DMA_Channel_TypeDef *c = k->chan;
    c->CFGR = c->CFGR & ~(uint32_t)DMA_CFGR1_EN;   /* stop it first */
    c->CFGR = 0;
    c->CNTR = 0;
    c->PADDR = 0;
    c->MADDR = 0;
    c->M1ADDR = 0;
    k->dma->INTFCR = k->flags;
    mux_field(k->mux, 0);
    volatile uint32_t *p = dma_buf;     /* volatile: not optimized away */
    for (uint32_t i = 0; i < GDMA_BUF_BYTES / 4; i++) {
        p[i] = 0;
    }
}

static int serve_start(uint32_t a, uint32_t src, uint32_t bytes) {
    uint32_t ch = a & 0xFFu, dir = (a >> 8) & 0xFFu;
    if ((a >> 16) || ch >= GDMA_CHANNELS || dir > GDMA_FROM_PERIPH || !size_ok(bytes)) {
        return ERR_INVALID_NUMERIC;
    }
    const channel *k = &channels[ch];
    DMA_Channel_TypeDef *c = k->chan;
    if (c->CFGR & DMA_CFGR1_EN) {
        return ERR_BLK_BUSY;            /* not finished since its last start */
    }
    if ((src || dir == GDMA_TO_PERIPH) && !user_buffer_ok(src, bytes)) {
        return ERR_INVALID_ADDRESS;     /* a read's is checked again at the end */
    }
    if (dir == GDMA_TO_PERIPH) {
        copy(dma_buf, (const void *)src, bytes);
    }
    /* DMA1's clock as well as DMA2's: DMAMUX needs it (blocks.c had both) */
    RCC->HBPCENR |= RCC_HBPeriph_DMA1 | RCC_HBPeriph_DMA2;
    c->CFGR = 0;
    k->dma->INTFCR = k->flags;
    c->PADDR = k->periph;
    c->MADDR = (uint32_t)dma_buf;
    c->M1ADDR = 0;
    c->CNTR = bytes / 4;
    c->CFGR = k->cfgr | (dir == GDMA_TO_PERIPH ? DMA_DIR_PeripheralDST : DMA_DIR_PeripheralSRC);
    mux_field(k->mux, (uint32_t)k->request - 1u);
    c->CFGR |= DMA_CFGR1_EN;
    return 0;
}

static int serve_finish(uint32_t ch, uint32_t dest, uint32_t bytes) {
    if (ch >= GDMA_CHANNELS) {
        return ERR_INVALID_NUMERIC;
    }
    const channel *k = &channels[ch];
    /* What happened, from the hardware: a read set up here (CFGR as
       serve_start wrote it, EN aside, which the hardware may clear) that
       completed without an error */
    uint32_t cfgr = k->chan->CFGR & ~(uint32_t)DMA_CFGR1_EN;
    uint32_t fl = k->dma->INTFR;
    int was_read = cfgr == (k->cfgr | DMA_DIR_PeripheralSRC);
    int done = (fl & k->tc) && !(fl & k->te);
    int err = 0;
    if (dest) {
        if (!size_ok(bytes)) {
            err = ERR_INVALID_NUMERIC;
        } else if (!user_buffer_ok(dest, bytes)) {
            err = ERR_INVALID_ADDRESS;
        } else if (!was_read || !done) {
            err = ERR_BUS_FAULT;
        } else {
            k->chan->CFGR = k->chan->CFGR & ~(uint32_t)DMA_CFGR1_EN;
            copy((void *)dest, dma_buf, bytes);
        }
    }
    burn(k);
    return err;
}

int gdma_serve(uint32_t n, uint32_t a, uint32_t b, uint32_t c) {
    switch (n) {
    case GDMA_SVC_START:  return serve_start(a, b, c);
    case GDMA_SVC_FINISH: return serve_finish(a, b, c);
    default:              return ERR_UNSUPPORTED_OPERATION;
    }
}
