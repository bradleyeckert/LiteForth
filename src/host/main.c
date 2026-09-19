#include <stdio.h>
#include <stdlib.h>
#include "forth.h"
#include "vm.h"
#include "serial_io.h"
#include "options.h"

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

    int32_t flash_pages[RAM_PAGE][FLASH_PAGE_CELLS] = { 0 };
    int32_t ram_page[RAM_PAGE_CELLS] = { 0 };

    for (int i = 0; i < VM_MEM_PAGES; i++) {
        vm_memory[i] = NULL;
        vm_memory_rd_limit[i] = 0;
        vm_memory_wp_limit[i] = 0;
        vm_memory_executable[i] = 0;
        if (i < RAM_PAGE) {
            // Assign pointer to row i of flash memory
            vm_memory[i] = flash_pages[i];
            vm_memory_name[i] = "Flash";
            vm_memory_rd_limit[i] = FLASH_PAGE_CELLS;
    //        vm_memory_wp_limit[i] = FLASH_PAGE_CELLS; // initially write-protected
            vm_memory_executable[i] = FLASH_PAGE_CELLS;
        }
        else if (i == RAM_PAGE) {
            // Assign pointer to the RAM page
            vm_memory[i] = ram_page;
            vm_memory_name[i] = "RAM";
            vm_memory_rd_limit[i] = RAM_PAGE_CELLS;
            vm_memory_executable[i] = RAM_PAGE_CELLS;
        }
        else {
            // Reserved/Unmapped segments
            vm_memory_name[i] = "reserved";
        }
    }
    vm_memory[RAM_PAGE] = ram_page;
    vm_memory_rd_limit[RAM_PAGE] = RAM_PAGE_CELLS;
    vm_memory_executable[RAM_PAGE] = RAM_PAGE_CELLS;
    vm_memory_name[RAM_PAGE] = "RAM";
    serial_open(port_name, baudrate);
    int ior = QUIT();
    serial_close();
    return ior; // or in an embedded system, do a hard reset
}
/*
#define FLASH_PAGE_CELLS   1024 // Flash memory page size (>=1 sectors)
#define RAM_PAGE_CELLS     1024 // RAM page size
*/