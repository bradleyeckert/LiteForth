/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2025/05/26
 * Description        : Main program body for V5F.
 *********************************************************************************
 * Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "debug.h"
#include "hardware.h"
#include "../../../../forth.h"          /* LiteForth core, in src/ */
#include "../../../../vm.h"
#include "../../../../memalloc.h"
#include "../../../../tools.h"
#include "../../../../errcodes.h"
#include "options.h"                    /* LiteForth target files, here */
#include "serial_io.h"
#include "flash.h"
#include "blocks.h"
#include "lftime.h"
#include "main.h"

/*********************************************************************
 * LiteForth on the V5F, after src/target/desktop/main.c.
 *
 * The terminal is the USB CDC port: the V3F runs USB and passes the bytes
 * through the shared rings (serial_io.c). Flash pages are in the code
 * flash (flash.c), and there is no block storage yet (blocks.c).
 */
extern uint32_t g_block_capacity;

// The total idata and udata spans RAM_PAGE_CELLS cells
int lfInitPointers(void) {
    int32_t* mem = vm_memory[RAM_PAGE];
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

/* Sets up LiteForth's memory: flash pages 0..RAM_PAGE-1 in the code flash,
   the RAM page from the pool, and the rest unmapped. Returns an ior. */
static int lfMapMemory(int32_t** flash)
{
    pool_reset();
    int32_t* ram = pool_alloc(RAM_PAGE_CELLS);
    if (ram == NULL) return ERR_ALLOCATE_FAILED;

    int ior = flash_init(NULL, flash);
    if (ior) return ior;
    ior = blk_init(NULL, &g_block_capacity);
    if (ior) return ior;

    for (int i = 0; i < VM_MEM_PAGES; i++) {
        if (i < RAM_PAGE) {
            // Assign pointer to page i of flash memory
            vm_memory[i] = &(*flash)[i * FLASH_PAGE_CELLS];
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
    return 0;
}

/* Runs LiteForth on the USB CDC terminal. Never returns: `bye` restarts it. */
static void LiteForth(void)
{
    int32_t* flash = NULL;
    int ior = lfMapMemory(&flash);
    if (ior) {
        printf("V5F: LiteForth memory setup failed, ior=%d\r\n", ior);
        return;
    }

    // Boot from flash once go.f has saved an image: it stores the boot
    // record's address in cell 1. Erased flash reads FLASH_ERASED_WORD
    // (0xE339E339) on this chip; 0xFFFFFFFF counts as blank too.
    uint32_t boot = (uint32_t)flash[1];
    if (boot != FLASH_ERASED_WORD && boot != 0xFFFFFFFFu) {
        g_lf_sys_options |= SYS_OPTION_BOOTING;
    }
    printf("V5F: LiteForth starting, %s\r\n",
           (g_lf_sys_options & SYS_OPTION_BOOTING) ? "booting from flash" : "flash is blank");

    serial_open(NULL, 0);
    while (1) {
        ior = lfQuit();                 // returns on bye
        lfSetColor(COLOR_NORMAL);
        serial_close();
        printf("V5F: lfQuit returned %d, restarting\r\n", ior);
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    SystemAndCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(9600);
    printf("V5F SystemCoreClk:%d\r\n", SystemCoreClock);
    
#if (Run_Core == Run_Core_V3FandV5F)
    HSEM_FastTake(HSEM_ID0);
    HSEM_ReleaseOneSem(HSEM_ID0, 0);    /* wake the V3F, which runs USB */
    printf("V5F released HSEM0, running LiteForth\r\n");
    lfTimeInit();                       /* no Delay_Us/Delay_Ms on the V5F after this */
    LiteForth();

#elif (Run_Core == Run_Core_V3F)

#elif (Run_Core == Run_Core_V5F)
    Hardware();
#endif

    while(1)
    {
        ;
    }
}
