#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "forth.h"
#include "vm.h"
#include "serial_io.h"
#include "options.h"
#include "memalloc.h"
#include "flash.h"
#include "blocks.h"
#include "tools.h"
#include "errcodes.h"
#include "main.h"

extern uint32_t g_block_capacity;

// The total idata and udata spans RAM_PAGE_CELLS cells
int lfInitPointers(void) {
    uint32_t* mem = vm_memory[RAM_PAGE];
    if (mem == NULL) return ERR_ALLOCATE_FAILED;
    // udata space origin and limit
    mem[F_PTRS + 0] = LF_HERE0 + 0x400;
    mem[F_PTRS + 1] = VARIABLE(RAM_PAGE_CELLS);
    // idata space origin and limit
    mem[F_PTRS + 2] = LF_HERE0; // start of IDATA is LF_PTRS
    mem[F_PTRS + 3] = LF_HERE0 + 0x400;
    // code space origin and limit
    mem[F_PTRS + 4] = 0x80000003;
    mem[F_PTRS + 5] = FLASH_PAGE_CELLS / 4;
    // text space origin and limit
    mem[F_PTRS + 6] = FLASH_PAGE_CELLS / 4;
    mem[F_PTRS + 7] = FLASH_PAGE_CELLS;
    // initial idp
    mem[F_PTRS + 8] = LF_HERE0;
    return 0;
}

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s [-t port_name] [-b baudrate] [-o options]"
        " [-f flash_file] [-k blocks_file]\n", prog);
}

int main(int argc, char* argv[]) {
    char* port_name = NULL;
    int baudrate = 115200;
    char* flash_name = NULL;            // NULL: FLASHFILENAME
    char* blocks_name = NULL;           // NULL: BLOCKFILENAME

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0' || argv[i][2] != '\0') {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        char opt = argv[i][1];
        if (!strchr("tbofk", opt)) {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        if (i + 1 >= argc) {            // every option takes a value
            fprintf(stderr, "Error: Missing value for -%c.\n", opt);
            usage(argv[0]);
            return 1;
        }
        char* value = argv[++i];
        switch (opt) {
        case 't': port_name = value;                    break;
        case 'b': baudrate = atoi(value);               break;
        case 'o': g_lf_sys_options = atoi(value);       break;
        case 'f': flash_name = value;                   break;
        case 'k': blocks_name = value;                  break;
        default:                                        break;
        }
    }
    if (port_name == NULL) {
        baudrate = 0; // default to stdio
    }

    pool_reset();
    uint32_t* ram = pool_alloc(RAM_PAGE_CELLS);
    uint32_t* flash = NULL;

    // Simulated flash and blocks live in files (by default in the working
    // directory), which are created if they don't exist.
    int ior = flash_init(flash_name, &flash);
    if (ior) return ior;
    ior = blk_init(blocks_name, &g_block_capacity);
    if (ior) return ior;

    for (int i = 0; i < VM_MEM_PAGES; i++) {
        if (i < RAM_PAGE) {
            // Assign pointer to page i of flash memory
            vm_memory[i] = &flash[i * FLASH_PAGE_CELLS];
            vm_memory_name[i] = "Flash";
            vm_memory_rd_limit[i] = FLASH_PAGE_CELLS;
            vm_memory_wp_limit[i] = FLASH_PAGE_CELLS; // write-protected
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

// Launch LiteForth

    ior = serial_open(port_name, baudrate);
    if (ior) return ior;
    ior = lfQuit();
    lfSetColor(COLOR_NORMAL);
    serial_close();

    int err = pool_free(ram);
    if (err) return err;
    return ior;
}
