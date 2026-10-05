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
 * and signals HSEM CDC_RESTART_HSEM; the V5F's HSEM interrupt restarts it
 * from its reset entry, and serial_open counts the start in v5f_starts.
 * If that count doesn't change within CDC_RESTART_WAIT_MS, the V3F resets
 * the whole chip. safe_boot lives here, outside both cores' .bss, so a
 * restart doesn't clear it; the V5F clears it once it has seen it.
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
#define CDC_RESTART_HSEM    1               /* HSEM_ID1: V3F -> V5F restart request */
                                            /* (HSEM0 wakes the V3F at boot; HSEM2 guards the
                                               debug UART, DEBUG_HSEM in SRC/Debug/debug.h) */
#define CDC_RESTART_WAIT_MS 500             /* then the V3F resets the chip */
#define CDC_CTRL_X          0x18            /* three in a row restart the V5F */

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
    cdc_ring_t rx;                          /* host -> V5F: V3F produces */
    cdc_ring_t tx;                          /* V5F -> host: V5F produces */
} cdc_shared_t;

#define CDC_SHARED  ((cdc_shared_t *)CDC_SHARED_ADDR)

/* Orders memory accesses between the cores. */
#define CDC_FENCE() __asm__ volatile ("fence rw, rw" ::: "memory")

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
