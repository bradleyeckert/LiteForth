#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
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
#include "lftime.h"
#include "board.h"

extern uint32_t g_block_capacity;

static jmp_buf reset_point;             // lfReset jumps back to it

void lfReset(void) {
    longjmp(reset_point, 1);
}

// The total idata and udata spans RAM_PAGE_CELLS cells
int lfInitPointers(void) {
    uint32_t* mem = vm_memory[RAM_PAGE];
    if (mem == NULL) return ERR_ALLOCATE_FAILED;
    // udata space origin and limit
    mem[F_PTRS + 0] = _UDATA0;
    mem[F_PTRS + 1] = VARIABLE(RAM_PAGE_CELLS);
    // idata space origin and limit
    mem[F_PTRS + 2] = LF_HERE0; // start of IDATA is LF_PTRS
    mem[F_PTRS + 3] = _UDATA0;
    // code space origin and limit
    mem[F_PTRS + 4] = 0x80000003;
    mem[F_PTRS + 5] = _TEXT0;
    // text space origin and limit
    mem[F_PTRS + 6] = _TEXT0;
    mem[F_PTRS + 7] = FLASH_PAGE_CELLS;
    return 0;
}

static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s [-t port_name] [-b baudrate] [-o options]"
        " [-f flash_file] [-k blocks_file] [-K board_file]\n", prog);
}

int main(int argc, char* argv[]) {
    char* port_name = NULL;
    int baudrate = 115200;
    char* flash_name = NULL;            // NULL: FLASHFILENAME
    char* blocks_name = NULL;           // NULL: BLOCKFILENAME
    char* board_name = NULL;            // -K: a board file for sealed blocks
    static sealed_board board;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || argv[i][1] == '\0' || argv[i][2] != '\0') {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
        char opt = argv[i][1];
        if (!strchr("tbofkK", opt)) {
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
        case 'K': board_name = value;                   break;
        default:                                        break;
        }
    }
    if (port_name == NULL) {
        baudrate = 0; // default to stdio
    }

    uint32_t* flash = NULL;

    // Simulated flash and blocks live in files (by default in the working
    // directory), which are created if they don't exist.
    int ior = flash_init(flash_name, &flash);
    if (ior) return ior;
    if (board_name != NULL) {
        char msg[200];
        if (board_load(board_name, &board, msg, sizeof msg) != 0) {
            fprintf(stderr, "Error: %s\n", msg);
            return 1;
        }
        blk_set_board(&board);
    }
    ior = blk_init(blocks_name, &g_block_capacity);
    if (ior) return ior;
    ior = serial_open(port_name, baudrate);
    if (ior) return ior;
    lfRtcInit();                        // the host's clock: always starts

    // lfReset (lfStandby) comes back here: like a reset, it starts over
    // with the options lf was started with and a fresh memory map.
    uint32_t boot_options = g_lf_sys_options;
    if (setjmp(reset_point)) {
        g_lf_sys_options = boot_options;
    }
    pool_reset();
    uint32_t* ram = pool_alloc(RAM_PAGE_CELLS);

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

    ior = lfQuit();
    lfSetColor(COLOR_NORMAL);
    serial_close();

    int err = pool_free(ram);
    if (err) return err;
    return ior;
}
