/*
 * Block mass storage for the CH32H417 (V5F), on the microSD card. See
 * blocks.h, doc/exfat.md and doc/sdcard.md for the card layout.
 *
 * The card is reached through the SDIO peripheral. WCH's driver (sdio.c,
 * from EVT/EXAM/SDIO/SDIO_SD, unmodified) brings it up. Transfers use DMA
 * (BLK_DMA, below), driven from here: WCH's DMA mode waits for the
 * transfer and uses DMA1 channel 1, which the V3F's UART code also uses
 * (USART4 TX; channel 2 is its RX). This file uses DMA2 channel 1
 * instead, routed to the SDIO request through DMAMUX channel 9.
 *
 * Everything is a run of 1 to BLK_MAX_SECTORS 512-byte card sectors,
 * moved with one multiple-block command (CMD18 or CMD25) and ended with
 * CMD12. A 4 KB block is 8 sectors from the block space's base sector
 * (card.c finds it: lfblocks.fb4 on the exFAT volume). Buffers
 * are cell arrays, so they meet the 4-byte alignment DMA needs.
 */
#include <string.h>
#include "debug.h"
#include "sdio.h"
#include "blocks.h"
#include "lftime.h"
#include "../../../../card.h"       /* src/card.h */
#include "../../../../errcodes.h"   /* src/errcodes.h */

#ifndef BLK_DMA
#define BLK_DMA             1           /* 0: WCH's polling transfers, which wait */
#endif
#define BLK_DMA_CHANNEL     DMA2_Channel1
#define BLK_DMA_FLAG_TC     DMA2_FLAG_TC1
#define BLK_DMAMUX_CHANNEL  DMA_MuxChannel9 /* feeds DMA2 channel 1 */
#define BLK_DMA_REQUEST     111         /* SDIO, in the RM's DMAMUX table (1-based) */
#define BLK_TIMEOUT_US      1000000u    /* a step that takes longer fails: ERR_BLK_TIMEOUT */
#define SDIO_DCTRL_DMAEN    (1u << 3)   /* SDIO_DCTRL: DMA enable (RM, SDIO chapter) */

#define SECTOR_BYTES        512u
#define SECTORS_PER_BLOCK   (BLK_SIZE_BYTES / SECTOR_BYTES)   /* 8 */
#define SECTOR_CELLS        (SECTOR_BYTES / 4)

_Static_assert(BLK_SIZE_BYTES % SECTOR_BYTES == 0, "a block must be whole sectors");
_Static_assert(BLK_MAX_SECTORS == BLK_MAX_LENGTH * SECTORS_PER_BLOCK, "runs of sectors and of blocks");

static card_map map;                    /* where the block space is */
static card_map map_copy;               /* what blk_init put there: see map_ok */
static int map_bad;                     /* map changed: no more transfers */
static uint32_t card_sectors;           /* the card's size */
static int writable;                    /* block 0 has the signature */

static int status;                      /* blk_status: 0, an error, 1 reading, 2 writing */

/* Debugging a stray write seen on the board ("1 block" gave -92 with a
   capacity of 121, then the chip reset): map must not change after
   blk_init. If it has, print both copies on USART1 once and refuse every
   transfer from then on, so a damaged map can't aim a write anywhere. */
static void dump(const char *name, const void *p) {
    const uint8_t *b = (const uint8_t *)p;
    printf("V5F blocks: %s at %08lX:", name, (unsigned long)(uintptr_t)p);
    for (unsigned i = 0; i < sizeof(card_map); i++) {
        printf("%s%02X", (i % 16) ? " " : "\r\n  ", b[i]);
    }
    printf("\r\n");
}

static int map_ok(void) {
    if (map_bad) {
        return 0;
    }
    if (memcmp(&map, &map_copy, sizeof map) != 0) {
        map_bad = 1;
        printf("V5F blocks: the block map changed after startup; transfers stopped\r\n");
        dump("map", &map);
        dump("copy", &map_copy);
        return 0;
    }
    return 1;
}

/* Checks a request for length blocks from blk; on success *lba is the
   first card sector */
static int check_run(uint32_t blk, int length, uint32_t *lba) {
    if (length < 1 || length > BLK_MAX_LENGTH) {
        return ERR_INVALID_NUMERIC;
    }
    if (blk >= map.blocks || (uint32_t)length > map.blocks - blk) {
        return ERR_BLK_BOUNDS;
    }
    *lba = map.base + blk * SECTORS_PER_BLOCK;
    return 0;
}

/* Checks a request for count sectors from lba */
static int check_sectors(uint32_t lba, int count) {
    if (count < 1 || count > BLK_MAX_SECTORS) {
        return ERR_INVALID_NUMERIC;
    }
    if (lba >= card_sectors || (uint32_t)count > card_sectors - lba) {
        return ERR_BLK_BOUNDS;
    }
    return 0;
}

int blk_write_fence(uint32_t lba, int count) {
    if (!map.mounted || lba < map.vol.start
        || (uint64_t)lba + (uint32_t)count > (uint64_t)map.vol.start + map.vol.sectors) {
        return ERR_BLK_WRITE_PROTECTED;
    }
    return 0;
}

#if !BLK_DMA
/* Reads count sectors from lba now; bounds already checked */
static int read_now(uint32_t lba, uint32_t *dest, int count) {
    if (SD_ReadDisk((u8 *)dest, lba, (u8)count) != SD_OK) {
        return ERR_BLK_READ_FAIL;
    }
    return 0;
}

/* Writes count sectors from lba now; protection and bounds already checked */
static int write_now(uint32_t lba, uint32_t *src, int count) {
    if (SD_WriteDisk((u8 *)src, lba, (u8)count) != SD_OK) {
        return ERR_BLK_WRITE_FAIL;
    }
    return 0;
}
#endif

/*=========================================================================
* Unwaited transfers: blk_sector_read_imm and blk_sector_write_imm start a
* transfer of card sectors, blk_status tells how it went. The block
* functions are built on them.
*
* With DMA the transfer is a little state machine that blk_status steps
* (no interrupts, so nothing to share with the V3F's handlers):
*   start: data path, CMD18/CMD25, DMA          -> 1 reading / 2 writing
*   SDIO DATAEND and DMA done: CMD12
*   card not back in the transfer state (CMD13 each call): still 1 or 2;
*   a read's CMD12 leaves the card busy for a moment too, and a command
*   sent meanwhile gets no data (the board's second read in a row failed)
*   back in the transfer state                   -> 0
* An SDIO error flag ends it with an error, and so does BLK_TIMEOUT_US
* without progress: the data phase, and the card's busy time after
* CMD12, each get that long. The time is checked only while the step
* isn't finished, so a caller that asks late still gets 0.
=========================================================================*/

#if BLK_DMA

/* In sdio.c but not in sdio.h, which stays as WCH ships it */
SD_Error CmdResp1Error(u8 cmd);         /* waits for and checks an R1 response */
SD_Error IsCardProgramming(u8 *pstatus);    /* CMD13: the card's state */

enum { IDLE, MOVING, SETTLING };        /* DMA transfer steps */
static int step;
static uint64_t started;                /* lfGetTimeMicroSec at the start */

#define SDIO_DATA_ERRORS   (SDIO_FLAG_DCRCFAIL | SDIO_FLAG_DTIMEOUT | SDIO_FLAG_TXUNDERR \
                           | SDIO_FLAG_RXOVERR | SDIO_FLAG_STBITERR)

/* Sends a command with a short response and checks it (CmdResp1Error,
   which waits for the response: microseconds) */
static SD_Error command(uint8_t index, uint32_t arg) {
    SDIO_CmdInitTypeDef cmd;
    cmd.SDIO_Argument = arg;
    cmd.SDIO_CmdIndex = index;
    cmd.SDIO_Response = SDIO_Response_Short;
    cmd.SDIO_Wait = SDIO_Wait_No;
    cmd.SDIO_CPSM = SDIO_CPSM_Enable;
    SDIO_SendCommand(&cmd);
    return CmdResp1Error(index);
}

/* Sets up the data path for count sectors in direction dir */
static void data_path(uint32_t dir, int count) {
    SDIO_DataInitTypeDef data;
    data.SDIO_DataTimeOut = SD_DATATIMEOUT;
    data.SDIO_DataLength = SECTOR_BYTES * (uint32_t)count;
    data.SDIO_DataBlockSize = SDIO_DataBlockSize_512b;
    data.SDIO_TransferDir = dir;
    data.SDIO_TransferMode = SDIO_TransferMode_Block;
    data.SDIO_DPSM = SDIO_DPSM_Enable;
    SDIO_DataConfig(&data);
}

/* Points DMA2 channel 1 at buf and the SDIO FIFO and starts it; dir is
   DMA_DIR_PeripheralSRC (card to memory) or DMA_DIR_PeripheralDST */
static void dma_start(uint32_t *buf, uint32_t dir, int count) {
    DMA_InitTypeDef d = {0};
    DMA_Cmd(BLK_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA2, BLK_DMA_FLAG_TC);
    d.DMA_PeripheralBaseAddr = (uint32_t)&SDIO->FIFO;
    d.DMA_Memory0BaseAddr = (uint32_t)buf;
    d.DMA_DIR = dir;
    d.DMA_BufferSize = SECTOR_CELLS * (uint32_t)count;
    d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    d.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    d.DMA_Mode = DMA_Mode_Normal;
    d.DMA_Priority = DMA_Priority_High;
    d.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(BLK_DMA_CHANNEL, &d);
    DMA_MuxChannelConfig(BLK_DMAMUX_CHANNEL, BLK_DMA_REQUEST);
    DMA_Cmd(BLK_DMA_CHANNEL, ENABLE);
}

/* Card address of sector lba: sectors on SDHC/SDXC, bytes on SDSC */
static uint32_t card_address(uint32_t lba) {
    return (SDCardInfo.CardType == SDIO_HIGH_CAPACITY_SD_CARD) ? lba : lba * SECTOR_BYTES;
}

/* Stops a transfer: DMA off, data path off, CMD12, flags cleared */
static void stop(void) {
    SDIO->DCTRL = 0;
    DMA_Cmd(BLK_DMA_CHANNEL, DISABLE);
    command(SD_CMD_STOP_TRANSMISSION, 0);
    SDIO_ClearFlag(SDIO_STATIC_FLAGS);
    step = IDLE;
}

/* Ends the transfer with an error */
static int fail(int err) {
    stop();
    status = err;
    return err;
}

static int start_read(uint32_t lba, uint32_t *dest, int count) {
    SDIO->DCTRL = 0;
    SDIO_ClearFlag(SDIO_STATIC_FLAGS);
    data_path(SDIO_TransferDir_ToSDIO, count);
    if (command(SD_CMD_READ_MULT_BLOCK, card_address(lba)) != SD_OK) {
        SDIO->DCTRL = 0;
        return ERR_BLK_READ_FAIL;
    }
    dma_start(dest, DMA_DIR_PeripheralSRC, count);
    SDIO->DCTRL |= SDIO_DCTRL_DMAEN;
    status = 1;
    return 0;
}

static int start_write(uint32_t lba, uint32_t *src, int count) {
    SDIO->DCTRL = 0;
    SDIO_ClearFlag(SDIO_STATIC_FLAGS);
    if (command(SD_CMD_WRITE_MULT_BLOCK, card_address(lba)) != SD_OK) {
        return ERR_BLK_WRITE_FAIL;
    }
    dma_start(src, DMA_DIR_PeripheralDST, count);
    data_path(SDIO_TransferDir_ToCard, count);
    SDIO->DCTRL |= SDIO_DCTRL_DMAEN;
    status = 2;
    return 0;
}

/* Fails the transfer if this step has taken longer than BLK_TIMEOUT_US.
   Called only when the step isn't finished yet, so a caller that comes
   back late to a transfer that went through still gets 0. Signed: if the
   microsecond count ever reads a little lower than at the start, that
   mustn't look like a huge time. */
static void check_timeout(void) {
    int64_t elapsed = (int64_t)(lfGetTimeMicroSec() - started);
    if (elapsed > (int64_t)BLK_TIMEOUT_US) {
        printf("V5F blk: %s timed out, STA %lX DCOUNT %lX\r\n", status == 1 ? "read" : "write",
               (unsigned long)SDIO->STA, (unsigned long)SDIO->DCOUNT);
        fail(ERR_BLK_TIMEOUT);
    }
}

/* One step of the transfer in progress; updates status */
static void advance(void) {
    if (step == IDLE) {
        return;
    }
    int err = (status == 1) ? ERR_BLK_READ_FAIL : ERR_BLK_WRITE_FAIL;
    if (step == MOVING) {
        uint32_t sta = SDIO->STA;
        if (sta & SDIO_DATA_ERRORS) {
            printf("V5F blk: %s failed, STA %lX DCOUNT %lX\r\n", status == 1 ? "read" : "write",
                   (unsigned long)sta, (unsigned long)SDIO->DCOUNT);
            fail(err);
            return;
        }
        if (!(SDIO->STA & SDIO_FLAG_DATAEND) || !DMA_GetFlagStatus(DMA2, BLK_DMA_FLAG_TC)) {
            check_timeout();            /* still moving */
            return;
        }
        SDIO->DCTRL = 0;
        DMA_Cmd(BLK_DMA_CHANNEL, DISABLE);
        SD_Error e = command(SD_CMD_STOP_TRANSMISSION, 0);
        SDIO_ClearFlag(SDIO_STATIC_FLAGS);
        if (e != SD_OK) {
            step = IDLE;
            status = err;
            return;
        }
        step = SETTLING;                /* the card is busy for a while */
        started = lfGetTimeMicroSec();  /* from now, however late this is */
    }
    uint8_t state = 0;                  /* SETTLING: ask the card (CMD13) */
    if (IsCardProgramming(&state) != SD_OK) {
        step = IDLE;
        status = err;
    } else if (state == SD_CARD_TRANSFER) {
        step = IDLE;
        status = 0;
    } else {
        check_timeout();                /* still programming */
    }
}

/* Starts a checked transfer of count sectors from lba */
static int start(uint32_t lba, uint32_t *buf, int count, int write) {
    advance();                          /* a finished transfer isn't busy */
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    started = lfGetTimeMicroSec();
    int err = write ? start_write(lba, buf, count) : start_read(lba, buf, count);
    if (err) {
        return err;
    }
    step = MOVING;
    return 0;
}

int blk_status(void) {
    advance();
    return status;
}

#else /* BLK_DMA */

/* Does a checked transfer of count sectors from lba */
static int start(uint32_t lba, uint32_t *buf, int count, int write) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    status = write ? write_now(lba, buf, count)     /* WCH's polling transfer: */
                   : read_now(lba, buf, count);     /* done at once */
    return 0;
}

int blk_status(void) {
    return status;
}

#endif /* BLK_DMA */

int blk_sector_read_imm(uint32_t lba, uint32_t *dest, int count) {
    if (!map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    int err = check_sectors(lba, count);
    return err ? err : start(lba, dest, count, 0);
}

int blk_sector_write_imm(uint32_t lba, uint32_t *src, int count) {
    if (!map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    int err = check_sectors(lba, count);
    if (!err) {
        err = blk_write_fence(lba, count);
    }
    return err ? err : start(lba, src, count, 1);
}

int blk_read_imm(uint32_t blk, uint32_t *dest, int length) {
    if (!map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    uint32_t lba;
    int err = check_run(blk, length, &lba);
    return err ? err : start(lba, dest, length * SECTORS_PER_BLOCK, 0);
}

int blk_write_imm(uint32_t blk, uint32_t *src, int length) {
    if (!map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    if (!writable) {
        return ERR_BLK_WRITE_PROTECTED;
    }
    uint32_t lba;
    int err = check_run(blk, length, &lba);
    return err ? err : start(lba, src, length * SECTORS_PER_BLOCK, 1);
}

/* Waits for the transfer in progress, if any; returns its blk_status */
static int finish(void) {
    int s;
    while ((s = blk_status()) > 0) {
    }
    return s;
}

int blk_read(uint32_t blk, uint32_t *dest) {
    finish();
    int err = blk_read_imm(blk, dest, 1);
    return err ? err : finish();
}

int blk_write(uint32_t blk, uint32_t *src) {
    finish();
    int err = blk_write_imm(blk, src, 1);
    return err ? err : finish();
}

int blk_sector_read(uint32_t lba, uint32_t *dest, int count) {
    finish();
    int err = blk_sector_read_imm(lba, dest, count);
    return err ? err : finish();
}

/* card.c's sector reader. map is still being filled in, so it bypasses
   map_ok: card_find_blocks writes to it as it goes. */
static int card_read(uint32_t sector, uint8_t *buf) {
    finish();
    int err = check_sectors(sector, 1);
    if (!err) {
        err = start(sector, (uint32_t *)buf, 1, 0);
    }
    return err ? err : finish();
}

int blk_init(char *filename, uint32_t *capacity) {
    char msg[120];
    (void)filename;
    memset(&map, 0, sizeof map);
    memset(&map_copy, 0, sizeof map_copy);
    map_bad = 0;
    card_sectors = 0;
    writable = 0;
    status = 0;
    if (capacity) *capacity = 0;

    SD_Error err = SD_Init();
    NVIC_DisableIRQ(SDIO_IRQn);         /* polling: no SDIO interrupts on this core */
#if BLK_DMA
    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1 | RCC_HBPeriph_DMA2, ENABLE);  /* DMA1 as WCH's driver has it for DMAMUX */
#endif
    if (err != SD_OK) {
        printf("V5F blocks: no SD card (error %d)\r\n", (int)err);
        return 0;
    }
#if BLK_DMA
    /* SD_Init leaves the block length at 8 bytes (CMD16 for the SCR),
       which an SDSC card keeps; WCH's own reads send CMD16 512 first, our
       DMA transfers don't, so set it once here. SDHC/SDXC ignore it. */
    if (command(SD_CMD_SET_BLOCKLEN, SECTOR_BYTES) != SD_OK) {
        printf("V5F blocks: the SD card refused a 512-byte block length\r\n");
        return 0;
    }
#endif
    uint64_t n = (uint64_t)SDCardInfo.CardCapacity / SECTOR_BYTES;
    card_sectors = (n > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)n;

    int ior = card_find_blocks(card_read, card_sectors, &map, msg, sizeof msg);
    if (ior) {
        printf("V5F blocks: can't read the SD card (error %d)\r\n", ior);
        memset(&map, 0, sizeof map);
        return 0;
    }
    writable = map.writable;
    map_copy = map;
    printf("V5F blocks: %s\r\n", msg);
    printf("V5F blocks: map at %08lX, %u bytes\r\n", (unsigned long)(uintptr_t)&map,
           (unsigned)sizeof map);
    if (capacity) *capacity = map.blocks;
    return 0;
}
