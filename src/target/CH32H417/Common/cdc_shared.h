/*
 * USB CDC byte streams shared between the two cores.
 *
 * The V3F runs the USB device (Common/ch32h417_usbfs_device.c) and moves
 * bytes between the USB endpoints and two rings in shared SRAM
 * (Common/cdc_bridge.c). The V5F reads and writes the rings through
 * serial_io (V5F/User/serial_io.c). Nothing else is shared, so each core
 * runs its own app.
 *
 *   host --EP2 OUT--> V3F --rx ring--> V5F   (serial_getc)
 *   host <--EP3 IN--- V3F <--tx ring-- V5F   (serial_putc)
 *
 * The rings live at CDC_SHARED_ADDR, the top 32K of the V3F's 512K SRAM,
 * which both cores can address. The V3F linker script (SRC/Ld/V3F) ends
 * the V3F's RAM below it; the V5F's RAM is its own TCM, so it can't
 * overlap. Nothing is linked into the region: both cores use this fixed
 * address, and the V3F initializes it (cdc_shared_init) before it wakes
 * the V5F.
 *
 * Each ring has one producer and one consumer, and each index is written
 * by only one core: the producer advances head, the consumer advances
 * tail. Indices count bytes and wrap at 2^32; head - tail is the number
 * of bytes waiting. Aligned 32-bit stores are atomic, so no lock (HSEM)
 * is needed. CDC_FENCE orders each core's accesses: a producer stores the
 * data before the new head, and a consumer reads the data before it
 * publishes the new tail.
 *
 * Host open: when a program opens the COM port, the host sends
 * SET_CONTROL_LINE_STATE with DTR set, and clears it when the port is
 * closed. The V3F's USB interrupt keeps that in host_open. While it's 0,
 * nobody is listening: the V3F holds the tx ring instead of sending it, and
 * the V5F's serial_putc drops bytes once the ring is full rather than
 * waiting, so the app keeps running. The held output (up to 4K, oldest
 * kept) appears when a terminal opens the port.
 *
 * Escape hatch: three consecutive Ctrl+X (0x18) bytes from the host make
 * the V3F restart the V5F in safe-boot mode, so a runaway app can always be
 * stopped from the terminal. The V3F sets safe_boot to CDC_SAFE_BOOT_MAGIC
 * and resets the whole chip, so the V5F only ever starts from a reset:
 * its PMP is set once, at its cold start, and nothing in machine mode
 * runs its start-up code again (V5F/User/pmp.c: entry 0, locked, keeps
 * even machine mode out of the code flash's low window, where that code
 * is). USB restarts too: the host sees the port go and come back.
 * serial_open counts each start in v5f_starts. safe_boot lives here,
 * outside both cores' .bss, so the reset doesn't clear it (cdc_shared_init
 * leaves it alone); the V5F clears it once it has seen it, after
 * its memory setup (the card). If Ctrl+X x3 comes again while safe_boot is
 * still set, that safe start never got that far: the V5F is stuck in its
 * C code, so the V3F sets recover to CDC_RECOVER_MAGIC and resets the
 * chip, and stage 1 starts in recovery (V3F/User/stage1.c) instead of
 * starting the V5F. So does Ctrl+X x3 as the first input after a Ctrl+X
 * reset, stuck or not: the V3F marks that reset (ctrlx_reset), and any
 * other byte from the host disarms it. So "Ctrl+X x3, then Ctrl+X x3 again"
 * always reaches recovery, however soon LiteForth is back (it clears
 * safe_boot before USB is even configured again).
 *
 * File transfers: while `ymodem` receives a file, the bytes from the host
 * are data and may hold three 0x18 in a row, so the V5F sets binary to
 * CDC_BINARY_MAGIC (serial_binary) and the V3F doesn't count Ctrl+X until
 * it's cleared. serial_open clears it, so a V5F restart can't leave the
 * escape off.
 *
 * Standby (lfStandby, V5F/User/lftime.c): the chip powers down only when
 * both cores sleep, so the V5F sets standby to CDC_STANDBY_MAGIC; the
 * V3F's bridge sees it, detaches USB and sleeps for good (cdc_standby).
 * When the RTC alarm wakes the chip, the V5F sets rtc_wake to
 * CDC_RTC_WAKE_MAGIC and resets it; the next boot's lfRtcInit takes the
 * mark (so `alarm?` is true) and clears it. Like safe_boot, rtc_wake
 * survives the reset; cdc_shared_init clears standby, so the V3F doesn't
 * go straight back to sleep.
 */
#ifndef CDC_SHARED_H
#define CDC_SHARED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDC_SHARED_ADDR     0x20178000u     /* RAM_SHARED in Link_v3f.ld */
#define CDC_SHARED_MAGIC    0x43444331u     /* "CDC1": rings initialized */
#define CDC_RING_SIZE       4096u           /* bytes per ring, power of 2 */
#define CDC_SAFE_BOOT_MAGIC 0x5AFEB007u     /* safe_boot: skip the app's autorun */
                                            /* (HSEM0 wakes the V3F at boot; HSEM2 guards the
                                               debug UART, DEBUG_HSEM in SRC/Debug/debug.h) */
#define CDC_CTRL_X          0x18            /* three in a row reset the chip, the V5F safe */
#define CDC_STANDBY_MAGIC   0x5133B00Bu     /* standby: the V5F asks the V3F to sleep */
#define CDC_RTC_WAKE_MAGIC  0xA1A4A4E0u     /* rtc_wake: the RTC alarm caused this reset */
#define CDC_BINARY_MAGIC    0xB1A4DA7Au     /* binary: host bytes are data, no Ctrl+X escape */
#define CDC_UPDATE_MAGIC    0x5A9E0F11u     /* update: a checked package waits in lfstage.bin */
#define CDC_CONFIRM_MAGIC   0xC0F1A3EDu     /* update: the V5F started LiteForth (ends a trial) */
#define CDC_RECOVER_MAGIC   0x2EC0BE27u     /* recover: the next boot goes into stage 1's recovery */
#define CDC_NOSTART_MAGIC   0x4E530000u     /* nostart: this | the boots in a row the V5F didn't start */
#define CDC_CTRLX_RESET_MAGIC 0xC7A1B007u   /* ctrlx_reset: this boot came from Ctrl+X x3 */
#define CDC_REVISION_MAGIC  0x5E7E4510u     /* rev_magic: revision holds this boot's version */

typedef struct {
    volatile uint32_t head;                 /* next byte to write (producer) */
    volatile uint32_t tail;                 /* next byte to read (consumer) */
    uint8_t data[CDC_RING_SIZE];
} cdc_ring_t;

typedef struct {
    volatile uint32_t magic;                /* CDC_SHARED_MAGIC once set up */
    volatile uint32_t host_open;            /* V3F: 1 while a program has the port open (DTR) */
    volatile uint32_t safe_boot;            /* CDC_SAFE_BOOT_MAGIC: next V5F start skips the app */
    volatile uint32_t v5f_starts;           /* V5F: counts serial_open calls */
    volatile uint32_t standby;              /* V5F: CDC_STANDBY_MAGIC asks the V3F to sleep */
    volatile uint32_t rtc_wake;             /* V5F: CDC_RTC_WAKE_MAGIC after an alarm wake-up */
    volatile uint32_t binary;               /* V5F: CDC_BINARY_MAGIC during a file transfer */
    volatile uint32_t update;               /* V5F: CDC_UPDATE_MAGIC after -2 ymodem, for stage 1
                                               (V3F/User/stage1.c) at the next reset, which clears
                                               it; CDC_CONFIRM_MAGIC when LiteForth starts, which
                                               ends a new image's trial. cdc_shared_init leaves it
                                               alone: it must survive the reset that starts the
                                               install */
    volatile uint32_t recover;              /* V3F: CDC_RECOVER_MAGIC when Ctrl+X x3 came again
                                               before the last safe start got LiteForth going: the
                                               chip resets into stage 1's recovery, which clears it.
                                               Survives the reset, as safe_boot does */
    volatile uint32_t nostart;              /* V3F: CDC_NOSTART_MAGIC | n, n boots in a row whose
                                               V5F never started (stage1_wait_v5f); anything else
                                               counts as 0. Survives the reset */
    cdc_ring_t rx;                          /* host -> V5F: V3F produces */
    cdc_ring_t tx;                          /* V5F -> host: V5F produces */
    volatile uint32_t ctrlx_reset;          /* V3F: CDC_CTRLX_RESET_MAGIC across the reset that
                                               Ctrl+X x3 makes; the next boot's cdc_shared_init
                                               takes it and clears it. Last, so the fields the
                                               V5F reads don't move */
    volatile uint32_t revision;             /* V3F: the installed V5F image's version, from stage
                                               1's record (the package's --revision; 0 before any
                                               package), for Forth's `revision` */
    volatile uint32_t rev_magic;            /* V3F: CDC_REVISION_MAGIC once stage 1 has set
                                               revision at this boot; cdc_shared_init clears it */
} cdc_shared_t;

#define CDC_SHARED  ((cdc_shared_t *)CDC_SHARED_ADDR)

/* Orders memory accesses between the cores. (unit_tests/stage1sim, on the
   desktop, defines its own.) */
#ifndef CDC_FENCE
#define CDC_FENCE() __asm__ volatile ("fence rw, rw" ::: "memory")
#endif

/* Waits a little between polls of the shared SRAM. The V3F executes from
   the same SRAM and the USB controller DMAs into it, so a core spinning on
   a ring index at full speed could starve them. Roughly 100 cycles. */
static inline void cdc_backoff(void) {
    for (int i = 0; i < 100; i++) __asm__ volatile ("nop");
}

/* Bytes waiting in a ring. */
static inline uint32_t cdc_ring_count(const cdc_ring_t *r) {
    return r->head - r->tail;
}

/* Free space in a ring. */
static inline uint32_t cdc_ring_space(const cdc_ring_t *r) {
    return CDC_RING_SIZE - (r->head - r->tail);
}

#ifdef __cplusplus
}
#endif

#endif /* CDC_SHARED_H */
