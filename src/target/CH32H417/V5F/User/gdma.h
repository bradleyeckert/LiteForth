#ifndef GDMA_H
#define GDMA_H

#include <stdint.h>

/*
 * Guarded DMA: the only way the V5F's user mode (all of LiteForth) moves
 * data by DMA. PMP entry 2 (pmp.c) makes DMA1, DMA2 and DMAMUX read-only
 * to user mode, so it can't aim a DMA channel anywhere: a DMA controller
 * isn't bound by the PMP, and could otherwise read or write any memory,
 * the board record and machine-mode code included. User mode asks a
 * machine-mode service (`ecall`, pmp.c) instead, which:
 *
 *   - programs only the channels in its own table (gdma.c), each with a
 *     fixed peripheral register, direction rules and DMAMUX request;
 *   - points the channel's memory side only at its own dedicated buffer
 *     (GDMA_BUF_BYTES in DTCM), never at an address user mode gives it;
 *   - copies between that buffer and user mode's buffer, which must lie in
 *     DTCM's .data or .bss (guard_buffer_ok: not the stack, where machine
 *     mode's own frames are, not code, not the peripherals), checked on
 *     every call, from the call's own arguments, never from saved state;
 *   - burns after use: gdma_finish stops the channel, clears its
 *     registers, flags and DMAMUX request, and zeroes the whole buffer.
 *
 * A transfer is gdma_start, gdma_poll until it's done, then gdma_finish,
 * which a write needs too (to burn). Machine mode decides from the
 * hardware (the channel's CFGR and flags, which user mode can read but
 * not write), not from anything in RAM, whether a transfer is running,
 * which way it went, and whether it finished.
 *
 * The buffer itself has no PMP entry (there are four, all used): user mode
 * could read or write it, but nothing there is anything it doesn't
 * already have or send, and it's zeroed after every transfer.
 */

enum {
    GDMA_SDIO = 0,                      /* DMA2 channel 1 <-> SDIO_FIFO (blocks.c) */
    GDMA_CHANNELS
};

enum {
    GDMA_TO_PERIPH = 0,                 /* memory to peripheral: a write */
    GDMA_FROM_PERIPH = 1                /* peripheral to memory: a read */
};

#define GDMA_BUF_BYTES  (32u * 512u)    /* 16 KB: 32 card sectors (blocks.c splits longer runs) */

/* The ecall services (pmp.c's serve passes these on to gdma_serve) */
enum {
    GDMA_SVC_START = 8,                 /* a = channel | dir << 8, b = buf, c = bytes */
    GDMA_SVC_FINISH = 9,                /* a = channel, b = buf or 0, c = bytes or 0 */
    GDMA_SVC_LAST = 9
};

/* ---- User mode ---------------------------------------------------------- */

/**
 * Starts a transfer of bytes (4 to GDMA_BUF_BYTES, a multiple of 4) on
 * channel ch in direction dir. For GDMA_TO_PERIPH, buf is copied into the
 * DMA buffer first; for GDMA_FROM_PERIPH, buf may be NULL, and if it isn't
 * it's checked now as gdma_finish will check it, so a read into memory
 * machine mode won't write to fails before it starts. gdma_finish takes
 * the destination.
 * @return 0; ERR_INVALID_NUMERIC for a bad channel, direction or size;
 * ERR_INVALID_ADDRESS if buf isn't a buffer machine mode may read;
 * ERR_BLK_BUSY if the channel hasn't been finished since its last start.
 */
int gdma_start(int ch, int dir, const uint32_t *buf, uint32_t bytes);

/**
 * How channel ch's transfer is going, read from its flags (user mode may
 * read the DMA registers): 1 still moving, 0 done.
 * @return 1, 0, or ERR_BUS_FAULT if the channel reported a transfer error.
 */
int gdma_poll(int ch);

/** Units (words) channel ch still has to move: its CNTR, for debugging */
uint32_t gdma_left(int ch);

/**
 * Ends channel ch's transfer, finished or not, and burns it (above). If it
 * was a GDMA_FROM_PERIPH transfer, buf isn't NULL and the transfer
 * completed without an error, first copies bytes (4 to GDMA_BUF_BYTES, a
 * multiple of 4) from the DMA buffer to buf. With buf NULL, nothing is
 * copied: that's how to stop a transfer.
 * @return 0; ERR_INVALID_NUMERIC for a bad channel or size;
 * ERR_INVALID_ADDRESS if buf isn't a buffer machine mode may write;
 * ERR_BUS_FAULT if a copy was asked for but the transfer hadn't completed
 * or had an error. The channel is burned in every case but a bad channel.
 */
int gdma_finish(int ch, uint32_t *buf, uint32_t bytes);

/* ---- Machine mode ------------------------------------------------------- */

/**
 * MACHINE MODE ONLY: pmp.c's ecall handler calls this for services
 * GDMA_SVC_START..GDMA_SVC_LAST. Nothing in it may fault (a fault in a
 * trap handler locks the core up): every address is checked first.
 * @return The service's ior, as gdma_start and gdma_finish describe.
 */
int gdma_serve(uint32_t n, uint32_t a, uint32_t b, uint32_t c);

#endif /* GDMA_H */
