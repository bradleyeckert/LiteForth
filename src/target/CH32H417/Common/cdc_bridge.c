/*
 * USB CDC bridge for the V3F: USB endpoints <-> shared rings.
 *
 * Runs on top of the unmodified USB device driver from WCH's SimulateCDC
 * example (ch32h417_usbfs_device.c, UART.c/.h). That example bridges USB
 * to USART4 with UART_DataRx_Deal and UART_DataTx_Deal; cdc_bridge_poll
 * takes their place and bridges USB to the V5F instead.
 *
 * Host -> rx ring (bulk OUT, endpoint 2):
 *   The USB interrupt has the controller DMA each packet into one 64-byte
 *   slot of UART_Tx_Buf (named for the UART it would normally feed),
 *   stores its length in Uart.Tx_PackLen[], and counts it in
 *   Uart.Tx_RemainNum. When 14 of the 16 slots are full it NAKs endpoint 2
 *   and sets Uart.USB_Down_StopFlag, so the host waits. The bridge copies
 *   slot Uart.Tx_DealNum into the rx ring as space allows, keeping its
 *   place in Uart.Tx_CurPackPtr, frees each slot when it is used up, and
 *   re-ACKs endpoint 2 once slots are free. So when the V5F stops reading,
 *   the ring fills, then the slots, then the host waits: nothing is lost.
 *   The driver clears Tx_CurPackPtr whenever it resets the slots (bus
 *   reset, SET_LINE_CODING), so the copy position can't point into a
 *   stale packet.
 *
 * tx ring -> host (bulk IN, endpoint 3):
 *   When the endpoint is free, up to 64 bytes go from the ring into
 *   USBFS_EP3_Buf (which the controller DMAs from) and the endpoint is
 *   armed. Uart.USB_Up_IngFlag is set while that packet waits for the
 *   host; the interrupt clears it when the host takes it. There is no
 *   flush policy: whatever the V5F writes while a packet is in flight goes
 *   in the next one. After a full 64-byte packet, a zero-length packet
 *   follows if the ring is empty (Uart.USB_Up_Pack0_Flag), so the host's
 *   read returns.
 *
 *   A host polls endpoint 3 only while a program has the port open. TIM2's
 *   interrupt (UART.c, 100 us ticks) counts Uart.USB_Up_TimeOut; when a
 *   packet waits longer than CDC_TX_TIMEOUT, the bridge discards the tx
 *   ring until the host reads again, like a UART with nothing connected.
 *
 * Changes to the driver's state are made with the USB interrupt disabled.
 */
#include "cdc_bridge.h"
#include "cdc_shared.h"
#include "ch32h417_usbfs_device.h"  /* USBFSD, USBFS_EP3_Buf, Uart, UART_Tx_Buf */

#ifndef CDC_TX_TIMEOUT
#define CDC_TX_TIMEOUT  1000            /* TIM2 ticks of 100 us: 100 ms */
#endif

#define CDC_PACKET      DEF_USBD_FS_PACK_SIZE   /* 64 */
#define RING_MASK       (CDC_RING_SIZE - 1u)

_Static_assert((CDC_RING_SIZE & RING_MASK) == 0, "CDC_RING_SIZE must be a power of 2");
_Static_assert(sizeof(cdc_shared_t) <= 32 * 1024, "cdc_shared_t must fit in RAM_SHARED");

/* WCH's driver disables the interrupt twice, so it takes effect before the
   next instruction. Do the same. */
#define USB_IRQ_OFF()   do { NVIC_DisableIRQ(USBFS_IRQn); \
                             NVIC_DisableIRQ(USBFS_IRQn); } while (0)
#define USB_IRQ_ON()    NVIC_EnableIRQ(USBFS_IRQn)

void cdc_shared_init(void) {
    cdc_shared_t *sh = CDC_SHARED;
    sh->rx.head = sh->rx.tail = 0;
    sh->tx.head = sh->tx.tail = 0;
    CDC_FENCE();
    sh->magic = CDC_SHARED_MAGIC;
    CDC_FENCE();
}

/*=========================================================================
* Host -> rx ring
=========================================================================*/

/* Frees the slot at Tx_DealNum and lets the host send again when there is
   room. Call with the USB interrupt off and Tx_RemainNum nonzero. */
static void slot_release(void) {
    Uart.Tx_PackLen[Uart.Tx_DealNum] = 0;
    Uart.Tx_CurPackPtr = 0;
    if (++Uart.Tx_DealNum >= DEF_UARTx_TX_BUF_NUM_MAX) {
        Uart.Tx_DealNum = 0;
    }
    Uart.Tx_RemainNum--;
    if (Uart.USB_Down_StopFlag && (Uart.Tx_RemainNum < 2)) {
        USBFSD->UEP2_RX_CTRL = (USBFSD->UEP2_RX_CTRL & ~USBFS_UEP_R_RES_MASK)
                             | USBFS_UEP_R_RES_ACK;
        Uart.USB_Down_StopFlag = 0;
    }
}

static void host_to_ring(void) {
    cdc_ring_t *r = &CDC_SHARED->rx;
    USB_IRQ_OFF();
    while (Uart.Tx_RemainNum) {
        uint16_t slot = Uart.Tx_DealNum;
        uint16_t pos  = Uart.Tx_CurPackPtr;
        uint16_t len  = Uart.Tx_PackLen[slot];
        if (pos < len) {
            uint32_t space = cdc_ring_space(r);
            if (space == 0) break;      /* V5F is behind: leave it in the slot */
            uint32_t n = len - pos;
            if (n > space) n = space;
            CDC_FENCE();                /* V5F has finished with that space */
            uint32_t head = r->head;
            const uint8_t *src = &UART_Tx_Buf[slot * DEF_USB_FS_PACK_LEN + pos];
            for (uint32_t i = 0; i < n; i++) {
                r->data[(head + i) & RING_MASK] = src[i];
            }
            CDC_FENCE();                /* data before head */
            r->head = head + n;
            pos += n;
            if (pos < len) {            /* ring full partway through */
                Uart.Tx_CurPackPtr = pos;
                break;
            }
        }
        slot_release();                 /* used up, or a zero-length packet */
    }
    USB_IRQ_ON();
}

/*=========================================================================
* tx ring -> host
=========================================================================*/

/* Hands a packet of len bytes (0..64) in USBFS_EP3_Buf to endpoint 3. */
static void ep3_arm(uint16_t len) {
    USB_IRQ_OFF();
    Uart.USB_Up_IngFlag = 1;
    Uart.USB_Up_TimeOut = 0;
    Uart.USB_Up_Pack0_Flag = (len == CDC_PACKET);
    USBFSD->UEP3_TX_LEN = len;
    USBFSD->UEP3_TX_CTRL = (USBFSD->UEP3_TX_CTRL & ~USBFS_UEP_T_RES_MASK)
                         | USBFS_UEP_T_RES_ACK;
    USB_IRQ_ON();
}

/* Drops everything in the tx ring. */
static void ring_discard(cdc_ring_t *t) {
    uint32_t head = t->head;
    CDC_FENCE();
    t->tail = head;
}

static void ring_to_host(void) {
    static uint8_t stalled;             /* host stopped reading */
    cdc_ring_t *t = &CDC_SHARED->tx;

    if (!USBFS_DevEnumStatus) {         /* no host has configured us */
        ring_discard(t);
        return;
    }
    if (Uart.USB_Up_IngFlag) {          /* last packet not taken yet */
        if (Uart.USB_Up_TimeOut >= CDC_TX_TIMEOUT) stalled = 1;
        if (stalled) ring_discard(t);
        return;
    }
    stalled = 0;

    uint32_t n = cdc_ring_count(t);
    if (n == 0) {
        if (Uart.USB_Up_Pack0_Flag) ep3_arm(0);
        return;
    }
    if (n > CDC_PACKET) n = CDC_PACKET;
    CDC_FENCE();                        /* head before data */
    uint32_t tail = t->tail;
    for (uint32_t i = 0; i < n; i++) {
        USBFS_EP3_Buf[i] = t->data[(tail + i) & RING_MASK];
    }
    CDC_FENCE();                        /* data read before tail */
    t->tail = tail + n;
    ep3_arm((uint16_t)n);
}

void cdc_bridge_poll(void) {
    host_to_ring();
    ring_to_host();
}
