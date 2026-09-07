#ifndef SERIAL_IO_H
#define SERIAL_IO_H

#ifdef __cplusplus
extern "C" {
#endif

// --- SYSTEM ERROR CODES ---
#define SERIAL_SUCCESS            0
#define ERR_NOT_A_TTY            -1  // Device is not an interactive terminal
#define ERR_PORT_OPEN_FAILED     -2  // OS failed to open the serial port
#define ERR_GET_STATE_FAILED     -3  // Failed to read port/terminal capabilities
#define ERR_SET_STATE_FAILED     -4  // Failed to write new configurations
#define ERR_INVALID_HANDLE       -5  // The active port handle is invalid or uninitialized
#define ERR_IO_CHECK_FAILED      -6  // Driver query or select() polling failed

/**
 * Sets the standard input terminal/console to either raw or cooked mode.
 * Compatible with macOS, Linux, and Windows.
 * 
 * @param enable  Pass 1 to enter RAW mode, pass 0 to restore COOKED mode.
 * @return        SERIAL_SUCCESS on success, or a negative ERR_* code on failure.
 */
int set_terminal_mode(int enable);

/**
 * Opens a hardware serial/COM port based on string inputs.
 * Supports standard speeds up to 3,000,000 bits per second.
 * 
 * @param name      The system device string (e.g. "COM3", "/dev/ttyUSB0", or "TERM").
 * @param baudrate  The desired integer bit speed.
 * @return          SERIAL_SUCCESS on success, or a negative ERR_* code on failure.
 */
int serial_open(char* name, int baudrate);

/**
 * Safely releases open OS device resources and handles recycling pointers 
 * back to safe defaults.
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
 * Reads a single byte. Maps to standard input if terminal mode is active, 
 * or reads from the underlying active serial port if a hardware port is open.
 * 
 * @return The unsigned byte character value on success, or EOF on error/no-data.
 */
int serial_getc(void);

/**
 * Writes a single byte. Maps to standard output if terminal mode is active, 
 * or streams to the underlying active serial port if a hardware port is open.
 * 
 * @param c The character byte code to transmit.
 * @return  The character byte written on success, or EOF on error.
 */
int serial_putc(int c);

#ifdef __cplusplus
}
#endif

#endif /* SERIAL_IO_H */
