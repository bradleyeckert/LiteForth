#ifndef SERIAL_IO_H
#define SERIAL_IO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sets the standard input terminal/console to either raw or cooked mode.
 * Compatible with macOS, Linux, and Windows.
 * 
 * @param enable  Pass 1 to enter RAW mode, pass 0 to restore COOKED mode.
 * @return        0 on success, ERR_TERM_NOT_A_TTY if stdin is not a terminal,
 *                or another negative ERR_* code on failure.
 */
int set_terminal_mode(int enable);

/**
 * Opens a hardware serial/COM port, or selects stdio as the terminal.
 * Supports standard speeds up to 3,000,000 bits per second.
 * 
 * @param name      The system device string (e.g. "COM3", "/dev/ttyUSB0").
 *                  Ignored when baudrate is 0.
 * @param baudrate  The desired integer bit speed, or 0 to use stdio instead
 *                  of a serial port.
 * @return          0 on success, or a negative ERR_* code on failure.
 */
int serial_open(char* name, int baudrate);

/**
 * Closes the serial port, if one is open, and switches back to stdio.
 */
void serial_close(void);

/**
 * Checks if there is pending data to read from standard input or the active serial COM port.
 * Does NOT block.
 * 
 * @return 1 if data is available, 0 if empty, or a negative ERR_* code on error.
 */
int serial_ready(void);

/**
 * Checks if the underlying serial hardware buffer is busy.
 * Does NOT block.
 * 
 * @return 0 when ready for data, 1 when busy/full, or a negative ERR_* code on error.
 */
int serial_busy(void);

/**
 * Reads a single byte from stdin, or from the serial port if one is open.
 * In stdio mode, reaching the end of redirected input (e.g. `lf < file.f`)
 * reconnects stdin to the console, so input continues from the keyboard.
 * 
 * @return The unsigned byte value on success. On failure: EOF in stdio mode,
 *         or ERR_TERM_RX_FAILED (including no data) on a serial port.
 */
int serial_getc(void);

/**
 * Writes a single byte to stdout, or to the serial port if one is open.
 * Output to stdout is flushed immediately.
 * 
 * @param c The character byte code to transmit.
 * @return  0 on success, or ERR_TERM_TX_FAILED on error.
 */
int serial_putc(char c);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_IO_H */
