#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>  
#include "termio.h"

#ifdef _WIN32
#include <windows.h>
#define sleep_ms(x) Sleep(x)
#else
#include <unistd.h>
#define sleep_ms(x) usleep((x) * 1000)
#endif

// Helper function to test kbputc by writing an entire string byte-by-byte
static void test_kbputs(const char* str) {
    while (*str) {
        kbputc(*str++);
    }
}

int main(int argc, char* argv[]) {
    // 1. Ensure an argument is passed
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port_name> [baud_rate]\n", argv[0]);
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s TERM\n", argv[0]);
        fprintf(stderr, "  %s /dev/ttyUSB0 115200\n", argv[0]);
        fprintf(stderr, "  %s COM3 9600\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* port_name = argv[1];
    int baud_rate = 9600; // Default fallback baud rate

    if (argc >= 3) {
        baud_rate = atoi(argv[2]);
    }

    printf("Attempting to open port '%s' at %d baud...\n", port_name, baud_rate);

    // 2. Initialize the device stream
    int init_status = kbopen(port_name, baud_rate);
    if (init_status != KB_SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize port '%s' (Error Code: %d).\n", port_name, init_status);
        return EXIT_FAILURE;
    }

    printf("Port opened successfully!\n");
    if (strcmp(port_name, "TERM") == 0) {
        printf("Note: Standard terminal cooked input requires hitting [Enter] to submit data.\n");
    }
    printf("Press 'q' or ASCII 27 (ESC) to exit the loop.\n");
    printf("--- Testing kbputc: Sending initialization banner below ---\n");

    // Test kbputc transmission over the interface
    test_kbputs("-> Link Active. Echoing all inputs:\n");

    // 3. Execution Polling Loop
    bool running = true;
    while (running) {
        // Check if data is pending in the queue via kbfull
        if (kbfull() == -1) {
            int ch = kbgetc();
            if (ch != -1) {
                // Check exit conditions ('q' or ESC character)
                if (ch == 'q' || ch == 27) {
                    test_kbputs("\nExit condition detected. Terminating loop...\n");
                    running = false;
                }
                else {
                    // Visual terminal tracking block
                    if (strcmp(port_name, "TERM") == 0) {
                        if (ch >= 32 && ch <= 126) {
                            printf("[Read]: %c -> [Echo via kbputc]: ", (char)ch);
                        }
                        else {
                            printf("[Read]: ASCII %d -> [Echo via kbputc]: ", ch);
                        }
                    }

                    // Test kbputc functionality dynamically by echoing the input byte back
                    kbputc((char)ch + 1);

                    // Add an extra newline for visual readability when testing standard text files on terminal
                    if (strcmp(port_name, "TERM") == 0) {
                        kbputc('\n');
                    }
                }
            }
        }

        // Throttle the loop slightly to prevent 100% CPU core saturation
        sleep_ms(10);
    }

    // 4. Safely release file structures or threads
    test_kbputs("Closing port and cleaning up assets...\n");
    kbclose();
    printf("Done.\n");

    return EXIT_SUCCESS;
}
