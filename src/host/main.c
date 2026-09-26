#include <stdio.h>
#include <stdlib.h>
#include "forth.h"
#include "vm.h"
#include "serial_io.h"
#include "options.h"
#include "memalloc.h"
#include "flash.h"
#include "blocks.h"
#include <malloc.h>
#include <string.h>

extern uint32_t g_block_capacity;

#define FLASHBYTES  (RAM_PAGE * FLASH_PAGE_CELLS * sizeof(int32_t))

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

            case 'o':
                if (i + 1 < argc) {
                    g_lf_sys_options = atoi(argv[++i]);
                }
                else {
                    fprintf(stderr, "Error: Missing options.\n");
                    return 1;
                }
                break;

            default:
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                fprintf(stderr, "Usage: %s [-t port_name] [-b baudrate]\n", argv[0]);
                return 1; // 
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

    pool_reset();
    int32_t* ram = pool_alloc(RAM_PAGE_CELLS);
    int32_t* flash = NULL;
    int needfree = 0;

    if ((g_lf_sys_options & SYS_OPTION_NO_FLASH) == 0) {
        int ior = flash_init(FLASHFILENAME, &flash);
        if (ior) return ior;
    }
    if (flash == NULL) {
        flash = (int32_t*)malloc(FLASHBYTES);
        if (flash == NULL) {
            fprintf(stderr, "Error: Unable to allocate flash memory.\n");
            return 1;
        }
        memset(flash, -1, FLASHBYTES);
    }
    if ((g_lf_sys_options & SYS_OPTION_NO_BLOCK) == 0) {
        int ior = blk_init(NULL, &g_block_capacity);
        if (ior) return ior;
    }

    for (int i = 0; i < VM_MEM_PAGES; i++) {
        if (i < RAM_PAGE) {
            // Assign pointer to page i of flash memory
            vm_memory[i] = &flash[i * FLASH_PAGE_CELLS];
            vm_memory_name[i] = "Flash";
            vm_memory_rd_limit[i] = FLASH_PAGE_CELLS;
            if ((g_lf_sys_options & SYS_OPTION_NO_FLASH) == 0) {
                vm_memory_wp_limit[i] = FLASH_PAGE_CELLS; // write-protected
            }
            vm_memory_executable[i] = FLASH_PAGE_CELLS;
        }
        else if (i == RAM_PAGE) {
            // Assign pointer to the RAM page
            vm_memory[i] = ram;
            vm_memory_name[i] = "RAM";
            vm_memory_rd_limit[i] = RAM_PAGE_CELLS;
            vm_memory_executable[i] = RAM_PAGE_CELLS;
        }
        else {
            // Reserved/Unmapped segments
            vm_memory[i] = NULL;
            vm_memory_rd_limit[i] = 0;
            vm_memory_wp_limit[i] = 0;
            vm_memory_executable[i] = 0;
            vm_memory_name[i] = "reserved";
        }
    }

// Now that memory is set up, to make development easier set up default pointers

    int32_t* mem = vm_memory[RAM_PAGE];
    if (mem == NULL) return 999;

    // udata space origin and limit
    mem[F_PTRS + 0] = LF_HERE0;
    mem[F_PTRS + 1] = LF_HERE0 + 0x200;
    // idata space origin and limit
    mem[F_PTRS + 2] = LF_HERE0 + 0x200;
    mem[F_PTRS + 3] = LF_HERE0 + 0x400;
    // code space origin and limit
    mem[F_PTRS + 4] = 0x80000001;
    mem[F_PTRS + 5] = 0x100;
    // text space origin and limit
    mem[F_PTRS + 6] = 0x100;
    mem[F_PTRS + 7] = 0x200;
    // initial idp
    mem[F_PTRS + 8] = LF_HERE0 + 0x200;

// Launch LiteForth

    int ior = serial_open(port_name, baudrate);
    if (ior) return ior;
    ior = QUIT();
    serial_close();

    if (needfree) free(flash);

    int err = pool_free(ram);
    if (err) return err;
    return ior;
}
