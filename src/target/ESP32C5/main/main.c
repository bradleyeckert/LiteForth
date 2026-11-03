/*
 * LiteForth on the ESP32-C5 (ESP-IDF application), after
 * src/target/desktop/main.c and src/target/CH32H417/V5F/User/main.c.
 *
 * app_main() runs as a FreeRTOS task: it brings up the terminal (CH340 UART),
 * maps the VM's memory (flash pages + a RAM page from the pool), and runs
 * lfQuit(). Milestone 1: REPL + flash, no SD/LCD/security. Wi-Fi/BLE/OTA, when
 * added, run in their own IDF tasks. See doc/ESP32C5/esp32c5-notes.md.
 */
#include <stdio.h>
#include <setjmp.h>
#include "forth.h"
#include "vm.h"
#include "memalloc.h"
#include "tools.h"
#include "../../../errcodes.h"   /* src/errcodes.h */
#include "options.h"
#include "serial_io.h"
#include "flash.h"
#include "blocks.h"
#include "lftime.h"
#include "main.h"
#include "esp_system.h"             /* esp_restart */

extern uint32_t g_block_capacity;  /* defined in src/api0.c */

static jmp_buf reset_point;        /* lfReset jumps back here (as the desktop does) */

void lfReset(void) {
    longjmp(reset_point, 1);
}

/* The total idata and udata spans RAM_PAGE_CELLS cells. (Identical to the
   desktop and CH32H417 ports.) */
int lfInitPointers(void) {
    uint32_t* mem = vm_memory[RAM_PAGE];
    if (mem == NULL) return ERR_ALLOCATE_FAILED;
    mem[F_PTRS + 0] = _UDATA0;                   // udata origin
    mem[F_PTRS + 1] = VARIABLE(RAM_PAGE_CELLS);  // udata limit
    mem[F_PTRS + 2] = LF_HERE0;                  // idata origin (LF_PTRS)
    mem[F_PTRS + 3] = _UDATA0;                   // idata limit
    mem[F_PTRS + 4] = 0x80000003;                // code origin
    mem[F_PTRS + 5] = _TEXT0;                    // code limit
    mem[F_PTRS + 6] = _TEXT0;                    // text origin
    mem[F_PTRS + 7] = FLASH_PAGE_CELLS;          // text limit
    return 0;
}

/* Flash pages 0..RAM_PAGE-1 in the lfflash partition, the RAM page from the
   pool, the rest unmapped. No VM_IO_PAGE on this target (milestone 1). */
static int lfMapMemory(uint32_t** flash) {
    pool_reset();
    uint32_t* ram = pool_alloc(RAM_PAGE_CELLS);
    if (ram == NULL) return ERR_ALLOCATE_FAILED;

    int ior = flash_init(NULL, flash);
    if (ior) return ior;
    ior = blk_init(NULL, &g_block_capacity);
    if (ior) return ior;

    for (int i = 0; i < VM_MEM_PAGES; i++) {
        if (i < RAM_PAGE) {
            vm_memory[i] = flash_page(i);
            vm_memory_name[i] = "Flash";
            vm_memory_rd_limit[i] = FLASH_PAGE_CELLS;
            vm_memory_wp_limit[i] = FLASH_PAGE_CELLS;   // write-protected
            vm_memory_executable[i] = FLASH_PAGE_CELLS;
        } else if (i == RAM_PAGE) {
            vm_memory[i] = ram;
            vm_memory_name[i] = "RAM";
            vm_memory_rd_limit[i] = RAM_PAGE_CELLS;
            vm_memory_executable[i] = RAM_PAGE_CELLS;
        } else {
            vm_memory[i] = NULL;
            vm_memory_rd_limit[i] = 0;
            vm_memory_wp_limit[i] = 0;
            vm_memory_executable[i] = 0;
            vm_memory_name[i] = "reserved";
        }
    }
    return 0;
}

static void put(const char *s) { while (*s) serial_putc(*s++); }

void app_main(void) {
    serial_open(NULL, 0);                    /* the CH340 UART terminal */

    uint32_t boot_options = g_lf_sys_options;
    if (setjmp(reset_point)) {
        g_lf_sys_options = boot_options;    /* lfReset / bye restart: fresh map */
    }

    uint32_t* flash = NULL;
    int ior = lfMapMemory(&flash);
    if (ior) {
        char m[48];
        snprintf(m, sizeof m, "LiteForth memory setup failed, ior=%d\r\n", ior);
        put(m);
        return;
    }

    /* Boot from flash once core.f has saved an image (cell 1 holds the boot
       record's address). Erased NOR flash reads 0xFFFFFFFF. */
    if (flash[1] != 0xFFFFFFFFu) {
        g_lf_sys_options |= SYS_OPTION_BOOTING;
    }
    put((g_lf_sys_options & SYS_OPTION_BOOTING)
        ? "LiteForth (ESP32-C5): booting from flash\r\n"
        : "LiteForth (ESP32-C5): flash is blank\r\n");

    while (1) {
        lfQuit();                           // returns on `bye`
        lfSetColor(COLOR_NORMAL);
        serial_close();
    }
}
