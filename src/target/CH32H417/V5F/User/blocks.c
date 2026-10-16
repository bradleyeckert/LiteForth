/*
 * Block mass storage for the CH32H417 (V5F): a raw partition on the
 * microSD card. See blocks.h, and doc/sdcard.md for the card layout.
 *
 * The card is reached through the SDIO peripheral. WCH's driver (sdio.c,
 * from EVT/EXAM/SDIO/SDIO_SD, unmodified) brings it up and reads the MBR
 * in polling mode. Block transfers use DMA (BLK_DMA, below), driven from
 * here: WCH's DMA mode waits for the transfer and uses DMA1 channel 1,
 * which the V3F's UART code also uses (USART4 TX; channel 2 is its RX).
 * This file uses DMA2 channel 1 instead, routed to the SDIO request
 * through DMAMUX channel 9. A 4 KB block is 8 sectors, moved with one
 * multiple-block command (CMD18 or CMD25) and ended with CMD12. Block
 * buffers are cell arrays, so they meet the 4-byte alignment DMA needs.
 *
 * The partition is the first MBR entry of type 0xDA. It must start on a
 * 64 KB boundary (a multiple of 128 sectors), and its block 0 must start
 * with BLK_SIGNATURE; otherwise only block 0 is readable.
 */
#include <string.h>
#include "debug.h"
#include "sdio.h"
#include "blocks.h"
#include "lftime.h"
#include "../../../../errcodes.h"   /* src/errcodes.h */

#ifndef BLK_DMA
#define BLK_DMA             1           /* 0: WCH's polling transfers, which wait */
#endif
#define BLK_DMA_CHANNEL     DMA2_Channel1
#define BLK_DMA_FLAG_TC     DMA2_FLAG_TC1
#define BLK_DMAMUX_CHANNEL  DMA_MuxChannel9 /* feeds DMA2 channel 1 */
#define BLK_DMA_REQUEST     111         /* SDIO, in the RM's DMAMUX table (1-based) */
#define BLK_TIMEOUT_US      1000000u    /* a transfer that takes longer fails */
#define SDIO_DCTRL_DMAEN    (1u << 3)   /* SDIO_DCTRL: DMA enable (RM, SDIO chapter) */

#define SECTOR_BYTES        512u
#define SECTORS_PER_BLOCK   (BLK_SIZE_BYTES / SECTOR_BYTES)   /* 8 */
#define ALIGN_SECTORS       128u        /* 64 KB */
#define MBR_TABLE           446         /* offset of the 4 partition entries */
#define MBR_ENTRY_SIZE      16
#define MBR_TYPE            4           /* offsets within an entry */
#define MBR_START           8
#define MBR_SECTORS         12
#define PART_TYPE_BLOCKS    0xDA        /* "non-FS data" */

_Static_assert(BLK_SIZE_BYTES % SECTOR_BYTES == 0, "a block must be whole sectors");

static uint32_t first_sector;           /* the partition's first sector */
static uint32_t blocks;                 /* blocks that can be read */
static int writable;                    /* the partition has the signature */

static uint32_t sector_buf[SECTOR_BYTES / 4];   /* MBR and signature reads */

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int blk_init(char *filename, uint32_t *capacity) {
    const uint8_t *sec = (const uint8_t *)sector_buf;
    (void)filename;
    first_sector = 0;
    blocks = 0;
    writable = 0;
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

    /* The MBR: find the block partition */
    if (SD_ReadDisk((u8 *)sector_buf, 0, 1) != SD_OK) {
        printf("V5F blocks: can't read the SD card's MBR\r\n");
        return 0;
    }
    if (sec[510] != 0x55 || sec[511] != 0xAA) {
        printf("V5F blocks: the SD card has no MBR partition table\r\n");
        return 0;
    }
    uint32_t start = 0, sectors = 0;
    int found = 0;
    for (int i = 0; i < 4 && !found; i++) {
        const uint8_t *e = &sec[MBR_TABLE + i * MBR_ENTRY_SIZE];
        if (e[MBR_TYPE] == PART_TYPE_BLOCKS) {
            start = le32(&e[MBR_START]);
            sectors = le32(&e[MBR_SECTORS]);
            found = 1;
        }
    }
    if (!found || sectors < SECTORS_PER_BLOCK) {
        printf("V5F blocks: no LiteForth block partition (type DA) on the SD card\r\n");
        return 0;
    }
    first_sector = start;
    blocks = 1;                         /* block 0, read-only, until proven */

    /* Block 0 must start with the signature, on a 64 KB boundary */
    if (start % ALIGN_SECTORS) {
        printf("V5F blocks: the block partition at sector %lu isn't 64 KB aligned: read-only\r\n",
               (unsigned long)start);
    } else if (SD_ReadDisk((u8 *)sector_buf, start, 1) != SD_OK) {
        printf("V5F blocks: can't read block 0\r\n");
    } else if (memcmp(sec, BLK_SIGNATURE, BLK_SIGNATURE_LEN) != 0) {
        printf("V5F blocks: block 0 doesn't start with " BLK_SIGNATURE ": read-only\r\n");
    } else {
        blocks = sectors / SECTORS_PER_BLOCK;
        writable = 1;
        printf("V5F blocks: %lu blocks at sector %lu\r\n",
               (unsigned long)blocks, (unsigned long)start);
    }
    if (capacity) *capacity = blocks;
    return 0;
}

#if !BLK_DMA
/* Reads block blk now; bounds already checked */
static int read_now(uint32_t blk, uint32_t *dest) {
    if (SD_ReadDisk((u8 *)dest, first_sector + blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK) != SD_OK) {
        return ERR_BLK_READ_FAIL;
    }
    return 0;
}

/* Writes block blk now; protection and bounds already checked */
static int write_now(uint32_t blk, uint32_t *src) {
    if (SD_WriteDisk((u8 *)src, first_sector + blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK) != SD_OK) {
        return ERR_BLK_WRITE_FAIL;
    }
    return 0;
}
#endif

/*=========================================================================
* Unwaited transfers: blk_read_imm and blk_write_imm start a transfer,
* blk_status tells how it went. blk_read and blk_write are built on them.
*
* With DMA the transfer is a little state machine that blk_status steps
* (no interrupts, so nothing to share with the V3F's handlers):
*   start: data path, CMD18/CMD25, DMA          -> 1 reading / 2 writing
*   SDIO DATAEND and DMA done: CMD12             -> 0, or for a write
*   card still programming (CMD13 each call)     -> 2 until it's done
* An SDIO error flag or BLK_TIMEOUT_US ends it with an error.
=========================================================================*/

static int status;                      /* blk_status: 0, an error, 1 reading, 2 writing */

#if BLK_DMA

/* In sdio.c but not in sdio.h, which stays as WCH ships it */
SD_Error CmdResp1Error(u8 cmd);         /* waits for and checks an R1 response */
SD_Error IsCardProgramming(u8 *pstatus);    /* CMD13: the card's state */

enum { IDLE, MOVING, PROGRAMMING };     /* DMA transfer steps */
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

/* Sets up the data path for one block in direction dir */
static void data_path(uint32_t dir) {
    SDIO_DataInitTypeDef data;
    data.SDIO_DataTimeOut = SD_DATATIMEOUT;
    data.SDIO_DataLength = BLK_SIZE_BYTES;
    data.SDIO_DataBlockSize = SDIO_DataBlockSize_512b;
    data.SDIO_TransferDir = dir;
    data.SDIO_TransferMode = SDIO_TransferMode_Block;
    data.SDIO_DPSM = SDIO_DPSM_Enable;
    SDIO_DataConfig(&data);
}

/* Points DMA2 channel 1 at buf and the SDIO FIFO and starts it; dir is
   DMA_DIR_PeripheralSRC (card to memory) or DMA_DIR_PeripheralDST */
static void dma_start(uint32_t *buf, uint32_t dir) {
    DMA_InitTypeDef d = {0};
    DMA_Cmd(BLK_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA2, BLK_DMA_FLAG_TC);
    d.DMA_PeripheralBaseAddr = (uint32_t)&SDIO->FIFO;
    d.DMA_Memory0BaseAddr = (uint32_t)buf;
    d.DMA_DIR = dir;
    d.DMA_BufferSize = BLOCK_SIZE_CELLS;
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

/* Card address of block blk: sectors on SDHC/SDXC, bytes on SDSC */
static uint32_t card_address(uint32_t blk) {
    uint32_t sector = first_sector + blk * SECTORS_PER_BLOCK;
    return (SDCardInfo.CardType == SDIO_HIGH_CAPACITY_SD_CARD) ? sector : sector * SECTOR_BYTES;
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

static int start_read(uint32_t blk, uint32_t *dest) {
    SDIO->DCTRL = 0;
    SDIO_ClearFlag(SDIO_STATIC_FLAGS);
    data_path(SDIO_TransferDir_ToSDIO);
    if (command(SD_CMD_READ_MULT_BLOCK, card_address(blk)) != SD_OK) {
        SDIO->DCTRL = 0;
        return ERR_BLK_READ_FAIL;
    }
    dma_start(dest, DMA_DIR_PeripheralSRC);
    SDIO->DCTRL |= SDIO_DCTRL_DMAEN;
    status = 1;
    return 0;
}

static int start_write(uint32_t blk, uint32_t *src) {
    SDIO->DCTRL = 0;
    SDIO_ClearFlag(SDIO_STATIC_FLAGS);
    if (command(SD_CMD_WRITE_MULT_BLOCK, card_address(blk)) != SD_OK) {
        return ERR_BLK_WRITE_FAIL;
    }
    dma_start(src, DMA_DIR_PeripheralDST);
    data_path(SDIO_TransferDir_ToCard);
    SDIO->DCTRL |= SDIO_DCTRL_DMAEN;
    status = 2;
    return 0;
}

/* One step of the transfer in progress; updates status */
static void advance(void) {
    if (step == IDLE) {
        return;
    }
    int err = (status == 1) ? ERR_BLK_READ_FAIL : ERR_BLK_WRITE_FAIL;
    if (lfGetTimeMicroSec() - started > BLK_TIMEOUT_US) {
        fail(err);
        return;
    }
    if (step == MOVING) {
        if (SDIO->STA & SDIO_DATA_ERRORS) {
            fail(err);
            return;
        }
        if (!(SDIO->STA & SDIO_FLAG_DATAEND) || !DMA_GetFlagStatus(DMA2, BLK_DMA_FLAG_TC)) {
            return;                     /* still moving */
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
        if (status == 1) {
            step = IDLE;
            status = 0;
            return;
        }
        step = PROGRAMMING;             /* a write: the card is busy for a while */
    }
    uint8_t state = 0;                  /* PROGRAMMING: ask the card (CMD13) */
    if (IsCardProgramming(&state) != SD_OK) {
        step = IDLE;
        status = err;
    } else if (state != SD_CARD_PROGRAMMING && state != SD_CARD_RECEIVING) {
        step = IDLE;
        status = 0;
    }
}

int blk_read_imm(uint32_t blk, uint32_t *dest) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    started = lfGetTimeMicroSec();
    int err = start_read(blk, dest);
    if (err) {
        return err;
    }
    step = MOVING;
    return 0;
}

int blk_write_imm(uint32_t blk, uint32_t *src) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    if (!writable) {
        return ERR_BLK_WRITE_PROTECTED;
    }
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    started = lfGetTimeMicroSec();
    int err = start_write(blk, src);
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

int blk_read_imm(uint32_t blk, uint32_t *dest) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    status = read_now(blk, dest);       /* WCH's polling transfer: done at once */
    return 0;
}

int blk_write_imm(uint32_t blk, uint32_t *src) {
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    if (!writable) {
        return ERR_BLK_WRITE_PROTECTED;
    }
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    status = write_now(blk, src);
    return 0;
}

int blk_status(void) {
    return status;
}

#endif /* BLK_DMA */

/* Waits for the transfer in progress, if any; returns its blk_status */
static int finish(void) {
    int s;
    while ((s = blk_status()) > 0) {
    }
    return s;
}

int blk_read(uint32_t blk, uint32_t *dest) {
    finish();
    int err = blk_read_imm(blk, dest);
    return err ? err : finish();
}

int blk_write(uint32_t blk, uint32_t *src) {
    finish();
    int err = blk_write_imm(blk, src);
    return err ? err : finish();
}
