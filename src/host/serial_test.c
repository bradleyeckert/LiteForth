#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "serial_io.h"

// Platform-independent sleeping wrapper to avoid CPU hogging
#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #define sys_sleep_ms(ms) Sleep(ms)
#else
    #include <unistd.h>
    #define sys_sleep_ms(ms) usleep((ms) * 1000)
#endif

int main(int argc, char* argv[]) {
    // Default values if no parameters are supplied
    char* port_name = "COM3"; 
    int baud_rate = 115200;

    // Parse command line arguments (Usage: ./program [port] [baud])
    if (argc > 1) port_name = argv[1];
    if (argc > 2) baud_rate = atoi(argv[2]);

    printf("Initializing port %s at %d baud...\n", port_name, baud_rate);

    // 1. Open the target interface (or virtual com0com device)
    int open_res = serial_open(port_name, baud_rate);
    if (open_res != SERIAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to open device %s (Code: %d)\n", port_name, open_res);
        return EXIT_FAILURE;
    }

    // 2. Put local console into raw mode to capture individual keys instantly
    int term_res = set_terminal_mode(1);
    if (term_res != SERIAL_SUCCESS) {
        fprintf(stderr, "Error: Failed to configure raw console (Code: %d)\n", term_res);
        serial_close();
        return EXIT_FAILURE;
    }

    printf("Full-duplex terminal connection live. Press Ctrl+C to exit.\n\n");

    // 3. Full-duplex main polling thread loop
    while (1) {
        // --- OUTBOUND TRAFFIC: Local Keyboard -> Serial Port ---
        // Temporarily flag internal console context to intercept your keystrokes
        set_terminal_mode(1); 
        if (serial_ready() > 0) {
            int local_key = serial_getc();
            
            if (local_key != EOF) {
                // Return context to serial port tracking and safely stream the byte out
                set_terminal_mode(0);
                if (serial_busy() == 0) {
                    serial_putc(local_key);
                }
            }
        }

        // --- INBOUND TRAFFIC: Serial Port -> Local Screen ---
        set_terminal_mode(0); // Scope polling checks explicitly to hardware COM channel
        if (serial_ready() > 0) {
            int remote_byte = serial_getc();
            
            if (remote_byte != EOF) {
                // Route the remote incoming byte to your local screen terminal monitor
                set_terminal_mode(1);
                serial_putc(remote_byte);
            }
        }

        // 4. Minor yielding delay to keep CPU usage close to 0%
        sys_sleep_ms(5); 
    }

    // Clean up lifecycle resources (unreachable here, exit via signal)
    set_terminal_mode(0);
    serial_close();
    return EXIT_SUCCESS;
}
