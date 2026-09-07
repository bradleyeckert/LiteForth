#ifndef TERMIO_H
#define TERMIO_H

#ifdef __cplusplus
extern "C" {
#endif

// Custom Error Codes for kbopen
#define KB_SUCCESS              0  // Port initialization successful
#define KB_ERR_INVALID_PARAM   -1  // Port name pointer was NULL or invalid
#define KB_ERR_THREAD_CREATE   -2  // Failed to create background thread (Linux TERM mode)
#define KB_ERR_OPEN_FAILED     -3  // System failed to open the specified target file/handle
#define KB_ERR_CFG_FAILED      -4  // System failed to set hardware configurations or baud rates

/**
 * @brief Initializes the COM port or terminal I/O.
 * 
 * @param port_name The name of the port to open (e.g., "COM1", "/dev/ttyS0").
 *                  Pass "TERM" to initialize standard terminal cooked input.
 * @param baud_rate The baud rate for the connection (e.g., 9600, 115200).
 *                  This value is ignored if port_name is "TERM".
 * @return int 0 if initialization is successful, or -1 on error.
 */
int kbopen(char *port_name, int baud_rate);

/**
 * @brief Returns the status of the opened input port.
 *
 * @return int -1 if a character is ready to be read,
 *              0 if no character is available.
 */
int kbfull(void);

/**
 * @brief Returns the status of the opened input port.
 *
 * @return int -1 if a character is ready to be written,
 *              0 if output is busy or not ready for writing.
 */
int kbready(void);

/**
 * @brief Returns the next byte from the open input port.
 * 
 * @return int The next available byte (0-255) as an integer, 
 *             or -1 if no character is ready.
 */
int kbgetc(void);

/**
 * @brief Transmits a single byte to either the serial port or standard output.
 *
 * @param c The character byte to be sent.
 */
void kbputc(char c);

/**
 * @brief Closes the open terminal thread or serial port handle if necessary.
 */
void kbclose(void);

#ifdef __cplusplus
}
#endif

#endif /* TERMIO_H */
