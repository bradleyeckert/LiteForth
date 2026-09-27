#include "../../serial_io.h"
#include "../../errcodes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int serial_puts(const char* str) {
    if (!str) return EOF;
    while (*str) {
        if (serial_putc((unsigned char)*str++) == EOF) {
            return EOF;
        }
    }
    return 0;
}

void print_help(void) {
    printf("\n==========================================");
    printf("\n  serial_io Echo Demo");
    printf("\n==========================================");
    printf("\n  Usage:");
    printf("\n    ./demo_terminal [<port_name>] [<baudrate>]");
    printf("\n");
    printf("\n  Arguments:");
    printf("\n    <port_name>  Serial COM port device (e.g., /dev/ttyUSB0 or COM3).");
    printf("\n                 If omitted, runs in stdio terminal mode.");
    printf("\n    <baudrate>   Baud rate speed (default: 115200).");
    printf("\n");
    printf("\n  In-Session Commands:");
    printf("\n    /help        Show this help menu");
    printf("\n    /quit        Exit the terminal demo");
    printf("\n==========================================\n\n");
}

int main(int argc, char** argv) {
    char* port_name = NULL;
    int baudrate = 0; // Default to terminal mode

    // Check for explicit -h or --help flags before starting
    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        print_help();
        return 0;
    }

    if (argc >= 2) {
        port_name = argv[1];
        baudrate = (argc >= 3) ? atoi(argv[2]) : 115200;
    }

    print_help();

    if (port_name) {
        printf("Opening serial port %s at %d baud...\n", port_name, baudrate);
        int err = serial_open(port_name, baudrate);
        if (err < 0) {
            printf("Error opening serial port: %d\n", err);
            return 1;
        }

        char msg[256];
        snprintf(msg, sizeof(msg), "Connected to %s! Type your input below:\r\n\r\n", port_name);
        serial_puts(msg);
    }
    else {
        printf("Running in Interactive Stdio Terminal Mode.\n\n");
        serial_open(NULL, 0);
    }

    // Set standard input to raw mode for interactive key-by-key handling
    set_terminal_mode(1);

    char line_buf[256];
    size_t line_pos = 0;

    while (1) {
        if (serial_ready() > 0) {
            int c = serial_getc();
            if (c == EOF) break;

            // Handle Enter key / Newline
            if (c == '\r' || c == '\n') {
                serial_putc('\r');
                serial_putc('\n');
                line_buf[line_pos] = '\0';

                // Check for terminal commands
                if (strcmp(line_buf, "/quit") == 0) {
                    break;
                }
                else if (strcmp(line_buf, "/help") == 0) {
                    set_terminal_mode(0); // Restore cooked mode briefly for print
                    print_help();
                    set_terminal_mode(1);
                }

                line_pos = 0;
            }
            // Handle Backspace (ASCII 8 or 127)
            else if (c == 8 || c == 127) {
                if (line_pos > 0) {
                    line_pos--;
                    serial_putc('\b');
                    serial_putc(' ');
                    serial_putc('\b');
                }
            }
            // Standard printable characters
            else if (c >= 32 && c <= 126) {
                if (line_pos < sizeof(line_buf) - 1) {
                    line_buf[line_pos++] = (char)c;
                    serial_putc(c); // Echo character
                }
            }
        }
    }

    // Clean up
    set_terminal_mode(0);
    serial_close();
    printf("\nTerminal session closed.\n");

    return 0;
}