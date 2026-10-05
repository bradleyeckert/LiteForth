/*
 * Terminal I/O over USB CDC for the CH32H417 (USBFS device, full speed).
 *
 * This sits on top of the USB device driver from WCH's SimulateCDC example
 * (ch32h417_usbfs_device.c, UART.c/.h, usb_desc.c/.h). That driver bridges
 * USB to USART4; here the bridge's two pump functions, UART_DataRx_Deal and
 * UART_DataTx_Deal, are not called, and LiteForth takes their place. The
 * driver files are used unmodified. See README.md.
 *
 * Host -> MCU (bulk OUT, endpoint 2):
 *   The USB interrupt has the controller DMA each packet into one 64-byte
 *   slot of UART_Tx_Buf (named for the UART it would normally feed), stores
 *   its length in Uart.Tx_PackLen[], and counts it in Uart.Tx_RemainNum.
 *   When 14 of the 16 slots are full it NAKs endpoint 2 and sets
 *   Uart.USB_Down_StopFlag, so the host waits. serial_getc reads bytes from
 *   slot Uart.Tx_DealNum, keeping its place in Uart.Tx_CurPackPtr, and frees
 *   each slot when it is used up. The bridge used Tx_CurPackPtr for the same
 *   job, and the driver clears it whenever it resets the slots (bus reset,
 *   SET_LINE_CODING), so the read position can never point into a stale
 *   packet.
 *
 * MCU -> Host (bulk IN, endpoint 3):
 *   serial_putc fills a 64-byte packet buffer. A packet goes to the
 *   endpoint (copied into USBFS_EP3_Buf, which the controller DMAs from)
 *   when it is full, or when the program polls for input. Uart.USB_Up_IngFlag
 *   is set while a packet waits for the host; the interrupt clears it when
 *   the host takes it. After a full 64-byte packet a zero-length packet
 *   follows if nothing else does (Uart.USB_Up_Pack0_Flag), so the host's
 *   read returns.
 *
 *   A host that enumerated the device keeps polling endpoint 3 only while
 *   a program has the port open. TIM2's interrupt (100 us ticks) counts
 *   Uart.USB_Up_TimeOut; when a packet waits longer than CDC_TX_TIMEOUT,
 *   output is discarded until the host reads again, so `emit` never hangs.
 *
 * Every change to the shared state is made with the USB interrupt disabled.
 */
#include <string.h>
#include "serial_io.h"
#include "ch32h417_usbfs_device.h"  /* USBFSD, USBFS_EP3_Buf, Uart, UART_Tx_Buf */

#ifndef CDC_TX_TIMEOUT
#define CDC_TX_TIMEOUT  1000            /* TIM2 ticks of 100 us: 100 ms */
#endif

#define CDC_PACKET      DEF_USBD_FS_PACK_SIZE   /* 64 */

/* WCH's driver disables the interrupt twice, so it takes effect before the
   next instruction. Do the same. */
#define USB_IRQ_OFF()   do { NVIC_DisableIRQ(USBFS_IRQn); \
                             NVIC_DisableIRQ(USBFS_IRQn); } while (0)
#define USB_IRQ_ON()    NVIC_EnableIRQ(USBFS_IRQn)

static uint8_t  tx_buf[CDC_PACKET];     /* packet being filled */
static uint16_t tx_len;                 /* bytes in tx_buf */
static uint8_t  tx_stalled;             /* host stopped reading: discard */

/*=========================================================================
* Output (endpoint 3 IN)
=========================================================================*/

/* 1 if endpoint 3 can take a packet now. Sets tx_stalled when the packet
   in the endpoint has waited too long, and clears it when the host has
   taken it. */
static int ep3_ready(void) {
    if (!USBFS_DevEnumStatus) return 0;
    if (Uart.USB_Up_IngFlag == 0) {
        tx_stalled = 0;
        return 1;
    }
    if (Uart.USB_Up_TimeOut >= CDC_TX_TIMEOUT) tx_stalled = 1;
    return 0;
}

/* Hands a packet of len bytes (0..64) to endpoint 3. Call only when
   ep3_ready() says so. */
static void ep3_send(const uint8_t* p, uint16_t len) {
    USB_IRQ_OFF();
    if (len) memcpy(USBFS_EP3_Buf, p, len);
    Uart.USB_Up_IngFlag = 1;
    Uart.USB_Up_TimeOut = 0;
    Uart.USB_Up_Pack0_Flag = (len == CDC_PACKET);
    USBFSD->UEP3_TX_LEN = len;
    USBFSD->UEP3_TX_CTRL = (USBFSD->UEP3_TX_CTRL & ~USBFS_UEP_T_RES_MASK)
                         | USBFS_UEP_T_RES_ACK;
    USB_IRQ_ON();
}

/* Sends the partial packet, or the zero-length packet that ends a transfer
   of full packets, if the endpoint is free. Does not block. */
static void tx_flush(void) {
    if (!ep3_ready()) return;
    if (tx_len) {
        ep3_send(tx_buf, tx_len);
        tx_len = 0;
    } else if (Uart.USB_Up_Pack0_Flag) {
        ep3_send(tx_buf, 0);
    }
}

/*=========================================================================
* Input (endpoint 2 OUT)
=========================================================================*/

/* Frees the slot at Tx_DealNum and lets the host send again when there is
   room. Call with the USB interrupt off and Tx_RemainNum nonzero. */
static void rx_release(void) {
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

/* Returns the next byte from the host, or -1 if there is none. With take
   nonzero the byte is consumed; otherwise it stays (a peek). Empty slots
   (zero-length packets) are skipped. */
static int rx_next(int take) {
    int c = -1;
    USB_IRQ_OFF();
    while (Uart.Tx_RemainNum) {
        uint16_t slot = Uart.Tx_DealNum;
        uint16_t pos  = Uart.Tx_CurPackPtr;
        uint16_t len  = Uart.Tx_PackLen[slot];
        if (pos < len) {
            c = UART_Tx_Buf[slot * DEF_USB_FS_PACK_LEN + pos];
            if (take) {
                if (++pos >= len) rx_release();
                else Uart.Tx_CurPackPtr = pos;
            }
            break;
        }
        rx_release();                   /* empty or used up */
    }
    USB_IRQ_ON();
    return c;
}

/*=========================================================================
* serial_io.h interface
=========================================================================*/

int set_terminal_mode(int enable) {
    (void)enable;
    return 0;
}

int serial_open(char* name, int baudrate) {
    (void)name;
    (void)baudrate;
    tx_len = 0;
    tx_stalled = 0;
    return 0;
}

void serial_close(void) {
    while (tx_len || Uart.USB_Up_Pack0_Flag) {
        if (!USBFS_DevEnumStatus || tx_stalled) break;
        tx_flush();
    }
    tx_len = 0;
}

int serial_ready(void) {
    tx_flush();
    return rx_next(0) >= 0;
}

int serial_busy(void) {
    if (tx_len < CDC_PACKET) return 0;
    tx_flush();
    return (tx_len == CDC_PACKET) && !tx_stalled && USBFS_DevEnumStatus;
}

int serial_getc(void) {
    int c;
    while ((c = rx_next(1)) < 0) {
        tx_flush();                     /* show the prompt while waiting */
    }
    return c;
}

int serial_putc(char c) {
    if (!USBFS_DevEnumStatus) return 0; /* no host: discard */
    if (tx_len == CDC_PACKET) {         /* buffer full: wait for endpoint */
        while (!ep3_ready()) {
            if (tx_stalled) return 0;   /* nobody reading: discard */
        }
        ep3_send(tx_buf, tx_len);
        tx_len = 0;
    }
    tx_buf[tx_len++] = (uint8_t)c;
    if ((tx_len == CDC_PACKET) && ep3_ready()) {
        ep3_send(tx_buf, tx_len);
        tx_len = 0;
    }
    return 0;
}
