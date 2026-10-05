/*
 * Terminal I/O for the V5F over USB CDC, through the rings the V3F shares
 * (Common/cdc_shared.h). The V5F consumes the rx ring and produces the tx
 * ring; the V3F (Common/cdc_bridge.c) does the other half and runs the
 * USB device.
 *
 * Each index is written by only one core, so these functions need no lock:
 * the V5F writes rx.tail and tx.head, and only reads the others.
 */
#include "serial_io.h"
#include "cdc_shared.h"

#define RING_MASK   (CDC_RING_SIZE - 1u)

/* 1 while a program on the host has the port open (DTR), as the V3F's USB
   interrupt records it. While it's 0 the V3F holds the tx ring, so writing
   must never wait for room. */
static int host_open(void) {
    return CDC_SHARED->host_open != 0;
}

int set_terminal_mode(int enable) {
    (void)enable;
    return 0;
}

int serial_open(char* name, int baudrate) {
    (void)name;
    (void)baudrate;
    cdc_ring_t *r = &CDC_SHARED->rx;
    while (CDC_SHARED->magic != CDC_SHARED_MAGIC) {
        cdc_backoff();                  /* the V3F hasn't set up the rings yet */
    }
    CDC_FENCE();
    r->tail = r->head;                  /* drop input from before this start, */
    CDC_FENCE();                        /* e.g. the Ctrl+X that restarted us */
    CDC_SHARED->v5f_starts++;           /* tells the V3F we're running */
    return 0;
}

void serial_close(void) {
    while (cdc_ring_count(&CDC_SHARED->tx) && host_open()) {
        cdc_backoff();                  /* the V3F sends or discards it */
    }
}

int serial_ready(void) {
    return cdc_ring_count(&CDC_SHARED->rx) != 0;
}

int serial_busy(void) {
    return cdc_ring_space(&CDC_SHARED->tx) == 0 && host_open();
}

int serial_getc(void) {
    cdc_ring_t *r = &CDC_SHARED->rx;
    while (cdc_ring_count(r) == 0) {
        cdc_backoff();                  /* wait for the host */
    }
    CDC_FENCE();                        /* head before data */
    uint32_t tail = r->tail;
    int c = r->data[tail & RING_MASK];
    CDC_FENCE();                        /* data read before tail */
    r->tail = tail + 1;
    return c;
}

int serial_putc(char c) {
    cdc_ring_t *t = &CDC_SHARED->tx;
    while (cdc_ring_space(t) == 0) {
        if (!host_open()) return 0;     /* nobody listening and the ring is full: drop it */
        cdc_backoff();                  /* wait for the V3F to send */
    }
    CDC_FENCE();                        /* V3F has finished with that space */
    uint32_t head = t->head;
    t->data[head & RING_MASK] = (uint8_t)c;
    CDC_FENCE();                        /* data before head */
    t->head = head + 1;
    return 0;
}
