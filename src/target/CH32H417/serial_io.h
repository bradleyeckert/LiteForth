#ifndef SERIAL_IO_H
#define SERIAL_IO_H

/*
 * Terminal I/O for the CH32H417 over USB CDC (USBFS, full speed).
 *
 * Same interface as src/target/desktop/serial_io.h. Bytes from the host
 * arrive on bulk OUT endpoint 2, and bytes to the host leave on bulk IN
 * endpoint 3, using the USB device driver from WCH's SimulateCDC example
 * (see README.md in this directory for how to wire it in).
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Does nothing: a CDC port has no line discipline, so it is always "raw".
 *
 * @param enable  Ignored.
 * @return        0.
 */
int set_terminal_mode(int enable);

/**
 * Selects the USB CDC port as the terminal and clears any output buffered
 * by an earlier session. The USB device must already be initialized
 * (USBFS_RCC_Init and USBFS_Device_Init, plus TIM2_Init for the output
 * timeout), as the SimulateCDC example's Hardware() does.
 *
 * @param name      Ignored.
 * @param baudrate  Ignored: the host's line coding has no effect on USB.
 * @return          0.
 */
int serial_open(char* name, int baudrate);

/**
 * Sends any buffered output, waiting until the host takes it or stops
 * reading (CDC_TX_TIMEOUT), then discards whatever is left.
 */
void serial_close(void);

/**
 * Checks for a byte from the host. Does NOT block. Also sends any output
 * that is waiting in the partly filled packet buffer, so text appears on
 * the terminal as soon as the program goes back to polling for input.
 *
 * @return 1 if a byte is available, 0 if not.
 */
int serial_ready(void);

/**
 * Checks whether serial_putc would have to wait. That happens when the
 * 64-byte packet buffer is full and the host has not yet taken the previous
 * packet. Does NOT block.
 *
 * @return 0 when ready for data, 1 when busy.
 */
int serial_busy(void);

/**
 * Reads a byte from the host, waiting until one arrives. While it waits it
 * sends any buffered output.
 *
 * @return The unsigned byte value.
 */
int serial_getc(void);

/**
 * Writes a byte to the host. Bytes are collected into 64-byte packets; a
 * full packet is sent at once, and a partial one is sent by the next
 * serial_ready, serial_getc or serial_close. If no host has configured the
 * device, or the host has not read a packet for CDC_TX_TIMEOUT (no terminal
 * has the port open), the byte is discarded, like a UART with nothing on
 * the other end.
 *
 * @param c The character byte code to transmit.
 * @return  0.
 */
int serial_putc(char c);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_IO_H */
