#include <stdio.h>
#include <stdlib.h>
#include "forth.h"
#include "vm.h"
#include "serial_io.h"

#define TEST_MEM_SIZE 1024

int main(int argc, char* argv[]) {
    char* port_name = NULL;
    int baudrate = 115200;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            switch (argv[i][1]) {
            case 't':
                if (i + 1 < argc) {
                    port_name = argv[++i];
                }
                else {
                    fprintf(stderr, "Error: Missing tty port name.\n");
                    return 1;
                }
                break;

            case 'b':
                if (i + 1 < argc) {
                    baudrate = atoi(argv[++i]);
                }
                else {
                    fprintf(stderr, "Error: Missing baud rate.\n");
                    return 1;
                }
                break;

            default:
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                fprintf(stderr, "Usage: %s [-t port_name] [-b baudrate]\n", argv[0]);
                return 1;
            }
        }
        else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [-t port_name] [-b baudrate]\n", argv[0]);
            return 1;
        }
    }
    if (port_name == NULL) {
        baudrate = 0; // default to stdio
    }

    int32_t code_page0[TEST_MEM_SIZE] = { 0 };
    int32_t ram_page[TEST_MEM_SIZE] = { 0 };

    for (int i = 0; i < VM_SEGMENTS; i++) {
        vm_memory[i] = code_page0;
        vm_memory_rd_limit[i] = TEST_MEM_SIZE;
        vm_memory_wp_limit[i] = 0;
        vm_memory_executable[i] = TEST_MEM_SIZE;
    }
    vm_memory[1] = ram_page;
	serial_open(port_name, baudrate);
    int ior = QUIT();
    serial_close();
    return ior; // or in an embedded system, do a hard reset
}
