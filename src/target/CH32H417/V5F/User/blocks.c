/*
 * Block mass storage for the CH32H417 (V5F), on the microSD card. See
 * blocks.h, doc/exfat.md and doc/sdcard.md for the card layout.
 *
 * The card is reached through the SDIO peripheral. WCH's driver (sdio.c,
 * from EVT/EXAM/SDIO/SDIO_SD, unmodified) brings it up. Transfers use DMA
 * (BLK_DMA, below), driven from here: WCH's DMA mode waits for the
 * transfer and uses DMA1 channel 1, which the V3F's UART code also uses
 * (USART4 TX; channel 2 is its RX). This file uses DMA2 channel 1
 * instead, routed to the SDIO request through DMAMUX channel 9, and only
 * through Guarded DMA (gdma.h, channel GDMA_SDIO): user mode can't write
 * the DMA registers (PMP entry 2, pmp.h). Machine mode moves the data
 * through its own buffer, copies a read out to dest when it ends, and
 * burns the buffer and the channel after every transfer. The buffer
 * holds CHUNK_SECTORS (32, 16 KB), so a longer run is moved as several
 * multiple-block commands, one after another, each ended with CMD12.
 *
 * Everything is a run of 1 to BLK_MAX_SECTORS 512-byte card sectors,
 * moved with one multiple-block command (CMD18 or CMD25) and ended with
 * CMD12. A 4 KB block is 8 sectors from the block space's base sector
 * (card.c finds it: lfblocks.fb4 on the exFAT volume). Buffers
 * are cell arrays, so they meet the 4-byte alignment DMA needs.
 *
 * A sealed lfblocks.fb4 (block 0 starts with SEALED_MAGIC,
 * doc/admin/encrypted-blocks.md) is mounted at blk_init with what board.c holds,
 * and offered read-only: when a read of its blocks finishes, blk_status
 * checks each block against its hash and decrypts it (sealed.c), reading
 * the hash table block it needs first, before it reports the read done.
 */
#include <stdio.h>
#include <string.h>
#include "debug.h"
#include "sdio.h"
#include "blocks.h"
#include "lftime.h"
#include "../../../../card.h"       /* src/card.h */
#include "../../../../sealed.h"     /* src/sealed.h */
#include "board.h"
#include "v5f_crumbs.h"                /* reset debugging trail */
#include "cdc_shared.h"                /* update: a package waits for stage 1 */
#include "gdma.h"                       /* Guarded DMA: the card's channel */

extern uint32_t g_block_capacity;       /* api0.c: what `capacity` shows */
#include "../../../../errcodes.h"   /* src/errcodes.h */

#ifndef BLK_DMA
#define BLK_DMA             1           /* 0: WCH's polling transfers, which wait */
#endif
#define BLK_TIMEOUT_US      1000000u    /* a step that takes longer fails: ERR_BLK_TIMEOUT */
#define SDIO_DCTRL_DMAEN    (1u << 3)   /* SDIO_DCTRL: DMA enable (RM, SDIO chapter) */

#define SECTOR_BYTES        512u
#define SECTORS_PER_BLOCK   (BLK_SIZE_BYTES / SECTOR_BYTES)   /* 8 */
#define SECTOR_CELLS        (SECTOR_BYTES / 4)

_Static_assert(BLK_SIZE_BYTES % SECTOR_BYTES == 0, "a block must be whole sectors");
_Static_assert(BLK_MAX_SECTORS == BLK_MAX_LENGTH * SECTORS_PER_BLOCK, "runs of sectors and of blocks");
#define CHUNK_SECTORS       (GDMA_BUF_BYTES / SECTOR_BYTES)  /* one card command's worth: 32 */
_Static_assert(CHUNK_SECTORS >= 1 && CHUNK_SECTORS <= BLK_MAX_SECTORS, "Guarded DMA's buffer");

static card_map map;                    /* where the block space is */
static card_map map_copy;               /* what blk_init put there: see map_ok */
static int map_bad;                     /* map changed: no more transfers */
static uint32_t card_sectors;           /* the card's size */
static int writable;                    /* block 0 has the signature */
static uint32_t limit;                  /* blocks offered: map.blocks, or a sealed file's */
static int sealed;                      /* a sealed file is mounted */
static uint32_t post_blk;               /* a read of sealed blocks to finish: */
static uint8_t *post_buf;               /* check and decrypt them when it's done */
static int post_len;

static int status;                      /* blk_status: 0, an error, 1 reading, 2 writing */
static uint32_t stage_base;             /* lfstage.bin: its first sector, */
static uint32_t stage_blocks;           /* and its blocks, or 0 */

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
        crumb(TAG('M','A','P','!'), map.base, map.blocks);
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
    if (blk >= limit || (uint32_t)length > limit - blk) {
        crumb(TAG('B','N','D','!'), blk, limit);
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
static uint32_t run_lba;                /* a run of more than CHUNK_SECTORS: */
static uint32_t *run_buf;               /* where the next chunk goes or comes from, */
static int run_left;                    /* the sectors after the current chunk, */
static int run_write;                   /* and which way */
static uint32_t *xfer_buf;              /* the transfer's buffer, */
static uint32_t xfer_bytes;             /* its length, */
static int xfer_read;                   /* and whether it's a read (copied out at the end) */

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

/* Starts the card's Guarded DMA channel for count sectors: dir is
   GDMA_FROM_PERIPH (card to memory, into buf when it ends) or
   GDMA_TO_PERIPH (from buf, copied now). Returns gdma_start's ior. */
static int dma_start(uint32_t *buf, int dir, int count) {
    xfer_buf = buf;
    xfer_bytes = SECTOR_BYTES * (uint32_t)count;
    xfer_read = (dir == GDMA_FROM_PERIPH);
    return gdma_start(GDMA_SDIO, dir, buf, xfer_bytes);
}

/* Card address of sector lba: sectors on SDHC/SDXC, bytes on SDSC */
static uint32_t card_address(uint32_t lba) {
    return (SDCardInfo.CardType == SDIO_HIGH_CAPACITY_SD_CARD) ? lba : lba * SECTOR_BYTES;
}

/* Stops a transfer: data path off, DMA stopped and burned, CMD12, flags
   cleared */
static void stop(void) {
    SDIO->DCTRL = 0;
    gdma_finish(GDMA_SDIO, NULL, 0);
    command(SD_CMD_STOP_TRANSMISSION, 0);
    SDIO_ClearFlag(SDIO_STATIC_FLAGS);
    step = IDLE;
    run_left = 0;                       /* the rest of a run isn't started */
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
    int err = dma_start(dest, GDMA_FROM_PERIPH, count);
    if (err) {
        stop();                         /* the card has started sending: CMD12 */
        return err;
    }
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
    int err = dma_start(src, GDMA_TO_PERIPH, count);
    if (err) {
        stop();                         /* the card waits for data: CMD12 */
        return err;
    }
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
        crumb(TAG('T','O','U','T'), SDIO->STA, SDIO->DCOUNT);
        printf("V5F blk: %s timed out, STA %lX DCOUNT %lX\r\n", status == 1 ? "read" : "write",
               (unsigned long)SDIO->STA, (unsigned long)SDIO->DCOUNT);
        fail(ERR_BLK_TIMEOUT);
    }
}

/* Starts the run's next chunk of up to CHUNK_SECTORS; returns an ior */
static int next_chunk(void) {
    int count = run_left < (int)CHUNK_SECTORS ? run_left : (int)CHUNK_SECTORS;
    int err = run_write ? start_write(run_lba, run_buf, count) : start_read(run_lba, run_buf, count);
    crumb(TAG('C','H','N','K'), run_lba, (uint32_t)count);
    if (err) {
        run_left = 0;
        return err;
    }
    run_lba += (uint32_t)count;
    run_buf += SECTOR_CELLS * (uint32_t)count;
    run_left -= count;
    step = MOVING;
    started = lfGetTimeMicroSec();
    return 0;
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
            crumb(TAG('F','A','I','L'), sta, SDIO->DCOUNT);
            printf("V5F blk: %s failed, STA %lX DCOUNT %lX\r\n", status == 1 ? "read" : "write",
                   (unsigned long)sta, (unsigned long)SDIO->DCOUNT);
            fail(err);
            return;
        }
        int dma = gdma_poll(GDMA_SDIO);
        if (dma < 0) {
            crumb(TAG('D','M','A','!'), (uint32_t)dma, gdma_left(GDMA_SDIO));
            printf("V5F blk: %s DMA error, left %lu\r\n", status == 1 ? "read" : "write",
                   (unsigned long)gdma_left(GDMA_SDIO));
            fail(err);
            return;
        }
        if (!(SDIO->STA & SDIO_FLAG_DATAEND) || dma != 0) {
            /* debugging: a crumb each time another sector's worth has arrived */
            static uint32_t last_left;
            uint32_t left = SDIO->DCOUNT;
            if (left + SECTOR_BYTES <= last_left || left > last_left) {
                crumb(TAG('P','R','O','G'), left, gdma_left(GDMA_SDIO));
                last_left = left;
            }
            check_timeout();            /* still moving */
            return;
        }
        crumb(TAG('D','E','N','D'), SDIO->STA, gdma_left(GDMA_SDIO));
        SDIO->DCTRL = 0;
        /* A read's data goes to its buffer now; either way the channel and
           machine mode's buffer are burned */
        int g = gdma_finish(GDMA_SDIO, xfer_read ? xfer_buf : NULL, xfer_bytes);
        SD_Error e = command(SD_CMD_STOP_TRANSMISSION, 0);
        crumb(TAG('C','M','1','2'), (uint32_t)e, SDIO->STA);
        SDIO_ClearFlag(SDIO_STATIC_FLAGS);
        if (g) {
            crumb(TAG('G','D','M','A'), (uint32_t)g, (uint32_t)xfer_buf);
            printf("V5F blk: Guarded DMA refused the %s's buffer %08lX (%d)\r\n",
                   status == 1 ? "read" : "write", (unsigned long)(uintptr_t)xfer_buf, g);
        }
        if (e != SD_OK || g) {
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
    } else if (state == SD_CARD_TRANSFER && run_left > 0) {
        int e = next_chunk();           /* the card is ready for the next command */
        if (e) {
            step = IDLE;
            status = e;
        }
    } else if (state == SD_CARD_TRANSFER) {
        step = IDLE;
        status = 0;
        crumb(TAG('D','O','N','E'), 0, 0);
    } else {
        check_timeout();                /* still programming */
    }
}

static void finish_sealed(void);

/* Starts a checked transfer of count sectors from lba */
static int start(uint32_t lba, uint32_t *buf, int count, int write) {
    advance();                          /* a finished transfer isn't busy */
    if (post_len && status == 0) {
        finish_sealed();                /* a sealed read nobody asked about yet */
    }
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    crumb(write ? TAG('W','R','I','T') : TAG('R','E','A','D'), lba, (uint32_t)buf);
    run_lba = lba;
    run_buf = buf;
    run_left = count;
    run_write = write;
    int err = next_chunk();             /* the first chunk; advance starts the rest */
    crumb(TAG('S','T','R','T'), (uint32_t)err, (uint32_t)count);
    return err;
}

/* Steps the transfer in progress; returns its status */
static int poll(void) {
    advance();
    return status;
}

#else /* BLK_DMA */

static void finish_sealed(void);

/* Does a checked transfer of count sectors from lba */
static int start(uint32_t lba, uint32_t *buf, int count, int write) {
    if (post_len) {
        finish_sealed();                /* a sealed read nobody asked about yet */
    }
    if (status > 0) {
        return ERR_BLK_BUSY;
    }
    status = write ? write_now(lba, buf, count)     /* WCH's polling transfer: */
                   : read_now(lba, buf, count);     /* done at once */
    return 0;
}

static int poll(void) {
    return status;
}

#endif /* BLK_DMA */

/* sealed.c's raw block reader, for block 0 and the hash table blocks: a
   transfer of its own, waited for, with no post-processing */
static int raw_block(uint32_t blk, uint8_t *buf) {
    if (blk >= map.blocks) {
        return ERR_BLK_BOUNDS;
    }
    while (poll() > 0) {
    }
    int err = start(map.base + blk * SECTORS_PER_BLOCK, (uint32_t *)buf, SECTORS_PER_BLOCK, 0);
    if (err) {
        return err;
    }
    int s;
    while ((s = poll()) > 0) {
    }
    return s;
}

/* A read of sealed blocks has finished: check and decrypt them. On a
   failure the rest of the run is zeroed and the read fails. */
static void finish_sealed(void) {
    int n = post_len;
    post_len = 0;                       /* raw_block below doesn't come back here */
    for (int i = 0; i < n; i++) {
        int err = sealed_open_block(post_blk + (uint32_t)i, post_buf + i * BLK_SIZE_BYTES);
        if (err) {
            memset(post_buf + i * BLK_SIZE_BYTES, 0, (size_t)(n - i) * BLK_SIZE_BYTES);
            status = err;
            return;
        }
    }
    status = 0;
}

int blk_status(void) {
    int s = poll();
    if (s == 0 && post_len) {
        finish_sealed();
        s = status;
    } else if (s < 0) {
        post_len = 0;                   /* the read failed: nothing to decrypt */
    }
    return s;
}

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
    crumb(TAG('B','L','K','R'), blk, g_block_capacity);
    if (!map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    uint32_t lba;
    int err = check_run(blk, length, &lba);
    if (!err) {
        err = start(lba, dest, length * SECTORS_PER_BLOCK, 0);
    }
    if (!err && sealed) {               /* blk_status finishes it */
        post_blk = blk;
        post_buf = (uint8_t *)dest;
        post_len = length;
    }
    return err;
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

/* SECURITY_LEVEL 2 (security.h): plain block files are refused */
static int sealed_only(void) {
    sealed_board board;
    char msg[80];
    board_get(&board, msg, sizeof msg);
    int only = board.sealed_only != 0;
    memset(&board, 0, sizeof board);
    return only;
}

/* If the block file is sealed, mounts it with what the board holds and
   offers its content read-only; if it can't, or it's a firmware update
   package, offers nothing */
static void mount_sealed(void) {
    static uint32_t b0[BLK_SIZE_BYTES / 4];
    char msg[120];
    if (raw_block(0, (uint8_t *)b0) != 0) {
        return;
    }
    if (memcmp(b0, PACKAGE_MAGIC, SEALED_MAGIC_LEN) == 0) {
        writable = 0;
        limit = 0;
        printf("V5F blocks: a firmware update package, not a block file\r\n");
        return;
    }
    if (memcmp(b0, SEALED_MAGIC, SEALED_MAGIC_LEN) != 0) {
        return;                         /* a plain block file */
    }
    writable = 0;
    limit = 0;
    sealed_board board;
    uint64_t t0 = lfGetTimeMicroSec();
    int err = board_get(&board, msg, sizeof msg);
    if (!err) {
        err = sealed_mount((const uint8_t *)b0, map.blocks, &board, raw_block, msg, sizeof msg);
    }
    memset(&board, 0, sizeof board);
    if (err) {
        printf("V5F blocks: sealed file refused: %s\r\n", msg);
        return;
    }
    sealed = 1;
    limit = sealed_blocks();
    printf("V5F blocks: %s (checked in %lu ms)\r\n", msg,
           (unsigned long)((lfGetTimeMicroSec() - t0) / 1000));
}

/* Clears what blk_init or blk_remount found */
static void forget(void) {
    memset(&map, 0, sizeof map);
    memset(&map_copy, 0, sizeof map_copy);
    map_bad = 0;
    writable = 0;
    limit = 0;
    sealed = 0;
    post_len = 0;
    stage_base = stage_blocks = 0;
    sealed_unmount();
}

/* Finds the block space on the card and mounts it; returns the capacity */
static uint32_t find(void) {
    char msg[120];
    crumb(TAG('C','A','R','D'), card_sectors, 0);
    int ior = card_find_blocks(card_read, card_sectors, &map, msg, sizeof msg);
    crumb(TAG('M','A','P',' '), map.base, map.blocks);
    if (ior) {
        printf("V5F blocks: can't read the SD card (error %d)\r\n", ior);
        memset(&map, 0, sizeof map);
        return 0;
    }
    writable = map.writable;
    limit = map.blocks;
    map_copy = map;
    printf("V5F blocks: %s\r\n", msg);
    if (card_find_file(card_read, card_sectors, &map, CARD_STAGE_FILE,
                       &stage_base, &stage_blocks) != 0) {
        stage_base = stage_blocks = 0;
    }
    if (writable) {
        mount_sealed();
    }
    if (writable && !sealed && sealed_only()) {
        writable = 0;
        limit = 0;
        printf("V5F blocks: a plain block file, but this build takes only sealed files (security level 2)\r\n");
    }
    return limit;
}

int blk_init(char *filename, uint32_t *capacity) {
    (void)filename;
    forget();
    card_sectors = 0;
    status = 0;
    if (capacity) *capacity = 0;

    SD_Error err = SD_Init();
    NVIC_DisableIRQ(SDIO_IRQn);         /* polling: no SDIO interrupts on this core */
    /* (Guarded DMA turns on the DMA clocks itself, in machine mode) */
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
    uint32_t c = find();
    if (capacity) *capacity = c;
    return 0;
}

int blk_remount(uint32_t *capacity) {
    finish();
    forget();
    uint32_t c = card_sectors ? find() : 0;
    if (capacity) *capacity = c;
    return 0;
}

/*=========================================================================
* Installing a whole block file (`-1 ymodem`): raw transfers of the
* space's blocks, whatever block 0 says, and a check of the new block 0
=========================================================================*/

uint32_t blk_space(void) {
    return map_bad ? 0 : map.space;
}

int blk_dir(exfat_list_fn fn, void *ctx) {
    if (!map.mounted || !map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    return card_list(card_read, &map, fn, ctx);
}

/* A raw transfer of length blocks from blk, waited for */
static int raw(uint32_t blk, uint32_t *buf, int length, int write) {
    if (!map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    if (length < 1 || length > BLK_MAX_LENGTH) {
        return ERR_INVALID_NUMERIC;
    }
    if (blk >= map.space || (uint32_t)length > map.space - blk) {
        return ERR_BLK_BOUNDS;
    }
    uint32_t lba = map.base + blk * SECTORS_PER_BLOCK;
    int count = length * SECTORS_PER_BLOCK;
    int err = write ? blk_write_fence(lba, count) : 0;
    finish();
    post_len = 0;                       /* nothing to decrypt */
    if (!err) {
        err = start(lba, buf, count, write);
    }
    if (err) {
        return err;
    }
    int st;
    while ((st = poll()) > 0) {
    }
    return st;
}

int blk_raw_read(uint32_t blk, uint32_t *dest, int length) {
    return raw(blk, dest, length, 0);
}

int blk_raw_write(uint32_t blk, const uint32_t *src, int length) {
    return raw(blk, (uint32_t *)src, length, 1);
}

int blk_check_image(const uint8_t *b0, uint32_t blocks, char *msg, int len) {
    if (memcmp(b0, PACKAGE_MAGIC, SEALED_MAGIC_LEN) == 0) {
        snprintf(msg, len, "a firmware update package, not a block file");
        return ERR_BLK_PARSE_FAIL;
    }
    if (memcmp(b0, SEALED_MAGIC, SEALED_MAGIC_LEN) != 0) {
        if (memcmp(b0, BLK_SIGNATURE, BLK_SIGNATURE_LEN) != 0) {
            snprintf(msg, len, "block 0 doesn't start with " BLK_SIGNATURE);
            return ERR_BLK_PARSE_FAIL;
        }
        if (sealed_only()) {
            snprintf(msg, len, "a plain block file, but this build takes only sealed files");
            return ERR_BLK_PARSE_FAIL;
        }
        snprintf(msg, len, "a plain block file");
        return 0;
    }
    if (sealed) {                       /* sealed_mount forgets it: offer nothing */
        sealed = 0;
        limit = 0;
    }
    sealed_board board;
    int err = board_get(&board, msg, len);
    if (!err) {
        err = sealed_mount(b0, blocks, &board, raw_block, msg, len);
    }
    memset(&board, 0, sizeof board);
    sealed_unmount();
    return err ? ERR_BLK_PARSE_FAIL : 0;
}

/*=========================================================================
* Staging a firmware update package (`-2 ymodem`): lfstage.bin
=========================================================================*/

uint32_t blk_stage_space(void) {
    return map_bad ? 0 : stage_blocks;
}

/* A transfer of block blk of lfstage.bin, waited for */
static int stage_io(uint32_t blk, uint32_t *buf, int write) {
    if (!map_ok()) {
        return ERR_BLK_OPEN_FAIL;
    }
    if (blk >= stage_blocks) {
        return ERR_BLK_BOUNDS;
    }
    uint32_t lba = stage_base + blk * SECTORS_PER_BLOCK;
    int err = write ? blk_write_fence(lba, SECTORS_PER_BLOCK) : 0;
    finish();
    post_len = 0;                       /* nothing to decrypt */
    if (!err) {
        err = start(lba, buf, SECTORS_PER_BLOCK, write);
    }
    if (err) {
        return err;
    }
    int st;
    while ((st = poll()) > 0) {
    }
    return st;
}

int blk_stage_read(uint32_t blk, uint32_t *dest) {
    return stage_io(blk, dest, 0);
}

int blk_stage_write(uint32_t blk, const uint32_t *src) {
    return stage_io(blk, (uint32_t *)src, 1);
}

static int stage_block(uint32_t blk, uint8_t *buf) {
    return stage_io(blk, (uint32_t *)buf, 0);
}

int blk_stage_check(sealed_package *pkg, char *msg, int len) {
    static uint32_t buf[BLK_SIZE_BYTES / 4] __attribute__((aligned(32)));
    sealed_board board;
    int err = board_get(&board, msg, len);
    if (err) {
        memset(&board, 0, sizeof board);
        return err;
    }
    if (sealed) {                       /* sealed_check_package unmounts it */
        sealed = 0;
        limit = 0;
    }
    err = sealed_check_package(stage_blocks, &board, stage_block, (uint8_t *)buf,
                               pkg, msg, len);
    memset(&board, 0, sizeof board);
    return err;
}

void blk_stage_ready(void) {
    CDC_SHARED->update = CDC_UPDATE_MAGIC;
    CDC_FENCE();
}

