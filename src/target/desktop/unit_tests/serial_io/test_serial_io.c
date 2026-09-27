#include "../../serial_io.h"
#include "../../errcodes.h"
#include <stdio.h>
#include <assert.h>

void test_terminal_mode_toggle(void) {
    // Attempting raw terminal mode when stdin is a tty or not
    int res = set_terminal_mode(1);
    if (res == ERR_TERM_NOT_A_TTY) {
        printf("set_terminal_mode: stdin is not a TTY (skipping raw mode test)\n");
    } else {
        assert(res == 0);
        // Restore terminal back to cooked mode
        assert(set_terminal_mode(0) == 0);
        printf("test_terminal_mode_toggle passed!\n");
    }
}

void test_invalid_serial_port(void) {
    // Attempt to open a non-existent serial port
    int res = serial_open("NON_EXISTENT_COM_PORT_999", 115200);
    assert(res == ERR_PORT_OPEN_FAILED);

    // Verify ready, busy, getc, and putc return appropriate error codes when uninitialized
    assert(serial_ready() == ERR_INVALID_HANDLE);
    assert(serial_busy() == ERR_INVALID_HANDLE);
    assert(serial_getc() == ERR_TERM_RX_FAILED);
    assert(serial_putc('A') == ERR_TERM_TX_FAILED);

    // Clean up handle state back to terminal mode
    serial_close();
    printf("test_invalid_serial_port passed!\n");
}

void test_terminal_mode_fallback(void) {
    // Baudrate 0 forces terminal/stdio mode
    int res = serial_open(NULL, 0);
    assert(res == 0);

    // In terminal mode, serial_busy should immediately report non-busy (0)
    assert(serial_busy() == 0);

    serial_close();
    printf("test_terminal_mode_fallback passed!\n");
}

int main(void) {
    printf("Running unit tests for serial_io...\n");
    test_terminal_mode_toggle();
    test_invalid_serial_port();
    test_terminal_mode_fallback();
    printf("All serial_io tests passed successfully!\n");
    return 0;
}