#ifndef SERIAL_IO_H
#define SERIAL_IO_H

/*
 * Terminal I/O for the CH32H417's V5F core, over USB CDC.
 *
 * Same interface as src/target/desktop/serial_io.h. The V3F runs the USB
 * device and bridges it to two byte rings in shared SRAM
 * (Common/cdc_shared.h, Common/cdc_bridge.c); these functions read and
 * write those rings, so the V5F never touches the USB hardware.
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
 * Selects the USB CDC port as the terminal. Waits until the V3F has
 * initialized the shared rings (it does so before it wakes the V5F, so
 * normally this doesn't wait at all). Drops any input that arrived before
 * this call, such as the Ctrl+X presses that restarted the V5F, and counts
 * the start in v5f_starts, which tells the V3F the restart worked.
 *
 * @param name      Ignored.
 * @param baudrate  Ignored: the host's line coding has no effect on USB.
 * @return          0.
 */
int serial_open(char* name, int baudrate);

/**
 * Waits until the V3F has taken all output from the tx ring (sent it, or
 * discarded it because the terminal stopped reading). Doesn't wait while
 * no program has the port open: the V3F holds that output for later.
 */
void serial_close(void);

/**
 * Checks for a byte from the host. Does NOT block.
 *
 * @return 1 if a byte is available, 0 if not.
 */
int serial_ready(void);

/**
 * Checks whether serial_putc would have to wait: the tx ring is full
 * because the host is reading more slowly than the V5F writes. Never busy
 * while no program has the port open, since serial_putc then drops bytes
 * rather than wait. Does NOT block.
 *
 * @return 0 when ready for data, 1 when busy.
 */
int serial_busy(void);

/**
 * Reads a byte from the host, waiting until one arrives.
 *
 * @return The unsigned byte value.
 */
int serial_getc(void);

/**
 * Writes a byte to the host, waiting while the tx ring is full. The V3F
 * sends it in its next USB packet. While no program has the port open
 * (DTR clear), the V3F holds the ring, and this doesn't wait: once the
 * 4K ring is full it drops the byte. The held output (the oldest 4K)
 * appears when a terminal opens the port. If a program keeps the port
 * open but stops reading, the V3F discards output after a timeout, so
 * this never waits for long.
 *
 * @param c The character byte code to transmit.
 * @return  0.
 */
int serial_putc(char c);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_IO_H */
