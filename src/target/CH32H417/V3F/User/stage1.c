/*
 * Stage 1 on the V3F, first step: see stage1.h. The callbacks give
 * src/update.c the code flash (FLASH_ROM_ERASE and FLASH_ROM_WRITE, fenced
 * to the V5F's slot and the two record sectors, so no mistake here can
 * erase the V3F itself), the card's lfstage.bin and lfbackup.bin (WCH's
 * polling SD driver, through src/card.c to find them), and the RNG.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "debug.h"
#include "stage1_layout.h"
#include "stage1.h"
#include "../../V5F/User/sdio.h"
#include "cdc_shared.h"
#include "../../../../update.h"
#include "../../../../card.h"
#include "../../../../blake2s.h"
#include "../../../../monocypher.h"
#include "../../../../errcodes.h"
#if __has_include("../../V5F/User/sealed_root.h")
#include "../../V5F/User/sealed_root.h"     /* SEALED_ROOT_KEY, SEALED_PRODUCT, INSECURE */
#endif

#define SECTOR_BYTES        512u
#define SECTORS_PER_BLOCK   (UPD_BLOCK / SECTOR_BYTES)
#define PROGRAM_PAGE        256u            /* FLASH_ROM_WRITE's unit */

/* The V5F's board record (V5F/User/board.c): 256 bytes at 0x08070000 */
#define BOARD_RECORD_ADDR   0x08070000u
#define BOARD_RECORD_MAGIC  0x314B464Cu     /* "LFK1" */
#define UID_ADDR            0x1FFFF7E8u
typedef struct {
    uint32_t magic;
    uint32_t min_revision;
    uint32_t min_serial;
    uint32_t reserved;
    uint8_t private_key[32];
    uint8_t check[16];                      /* BLAKE2s-128 of the first 48 bytes, LFrecord */
} board_record;

static card_map map;
static uint32_t card_sectors;
static uint32_t stage_base, backup_base;
static uint32_t io_buf[UPD_BLOCK / 4] __attribute__((aligned(32)));

/* ------------------------------------------------------------------------
 * The callbacks
 */
static int in_range(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len;
    return (addr >= UPD_V5F_ADDR && end <= UPD_V5F_ADDR + UPD_V5F_SLOT && end > addr)
        || (addr >= UPD_RECORD_A && end <= UPD_RECORD_A + UPD_SECTOR)
        || (addr >= UPD_RECORD_B && end <= UPD_RECORD_B + UPD_SECTOR);
}

static int f_read(uint32_t addr, void *buf, uint32_t len) {
    if (addr + len > 0xF0000u) {
        return ERR_BLK_BOUNDS;
    }
    memcpy(buf, (const void *)(STAGE1_FLASH_BASE + addr), len);
    return 0;
}

static int f_erase(uint32_t addr) {
    if (addr % UPD_SECTOR || !in_range(addr, UPD_SECTOR)) {
        return ERR_BLK_BOUNDS;
    }
    return FLASH_ROM_ERASE(STAGE1_FLASH_BASE + addr, UPD_SECTOR) == FLASH_COMPLETE
           ? 0 : ERR_FLASH_WRITE_SECTOR;
}

/* Programs len bytes at addr in 256-byte pages; a short last page is
   padded with 0xFF (erased), so a record of any size can be written */
static int f_write(uint32_t addr, const void *buf, uint32_t len) {
    static uint32_t page[PROGRAM_PAGE / 4];
    if (addr % PROGRAM_PAGE || !in_range(addr, (len + PROGRAM_PAGE - 1) & ~(PROGRAM_PAGE - 1))) {
        return ERR_BLK_BOUNDS;
    }
    const uint8_t *p = buf;
    for (uint32_t at = 0; at < len; at += PROGRAM_PAGE) {
        uint32_t n = (len - at < PROGRAM_PAGE) ? len - at : PROGRAM_PAGE;
        memset(page, 0xFF, sizeof page);
        memcpy(page, p + at, n);
        if (FLASH_ROM_WRITE(STAGE1_FLASH_BASE + addr + at, page, PROGRAM_PAGE) != FLASH_COMPLETE) {
            return ERR_FLASH_WRITE_SECTOR;
        }
    }
    return 0;
}

static int card_read(uint32_t sector, uint8_t *buf) {
    return SD_ReadDisk(buf, sector, 1) == SD_OK ? 0 : ERR_BLK_READ_FAIL;
}

static int block_io(uint32_t base, uint32_t blocks, uint32_t blk, uint8_t *buf, int write) {
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    uint32_t lba = base + blk * SECTORS_PER_BLOCK;
    SD_Error e = write ? SD_WriteDisk(buf, lba, SECTORS_PER_BLOCK)
                       : SD_ReadDisk(buf, lba, SECTORS_PER_BLOCK);
    return e == SD_OK ? 0 : (write ? ERR_BLK_WRITE_FAIL : ERR_BLK_READ_FAIL);
}

static upd_io io;                           /* filled in by stage1_boot */

static int s_read(uint32_t blk, uint8_t *buf) {
    return block_io(stage_base, io.stage_blocks, blk, buf, 0);
}

static int b_read(uint32_t blk, uint8_t *buf) {
    return block_io(backup_base, io.backup_blocks, blk, buf, 0);
}

static int b_write(uint32_t blk, const uint8_t *buf) {
    memcpy(io_buf, buf, UPD_BLOCK);         /* WCH's driver takes a non-const, aligned buffer */
    return block_io(backup_base, io.backup_blocks, blk, (uint8_t *)io_buf, 1);
}

/* 32 bytes from the RNG, through BLAKE2s, after the checks board.c makes */
static int rng(uint8_t *out, int len) {
    static const uint8_t person[8] = "LFstage1";
    blake2s_state S;
    uint32_t last = 0;
    uint8_t h[32];
    RCC_HBPeriphClockCmd(RCC_HBPeriph_RNG, ENABLE);
    RNG_Cmd(ENABLE);
    b2s_init(&S, 32, NULL, 0, NULL, person);
    for (int i = 0; i < 64; i++) {
        int wait = 100000;
        while (RNG_GetFlagStatus(RNG_FLAG_DRDY) == RESET && --wait) {
        }
        if (!wait || RNG_GetFlagStatus(RNG_FLAG_SECS) || RNG_GetFlagStatus(RNG_FLAG_CECS)) {
            return ERR_UNSUPPORTED_OPERATION;
        }
        uint32_t x = RNG_GetRandomNumber();
        if (i && x == last) {
            return ERR_UNSUPPORTED_OPERATION;   /* stuck */
        }
        last = x;
        b2s_puts(&S, (const uint8_t *)&x, 4);
    }
    b2s_final(&S, h);
    memcpy(out, h, len < 32 ? (size_t)len : 32);
    crypto_wipe(h, sizeof h);
    return 0;
}

/* ------------------------------------------------------------------------
 * The board's keys, as V5F/User/board.c's board_get has them
 */
static int keys_get(sealed_board *b, const char **why) {
    memset(b, 0, sizeof *b);
#ifndef SEALED_ROOT_KEY
    *why = "no root key built in (V5F/User/sealed_root.h)";
    return -1;
#else
    static const uint8_t root[32] = SEALED_ROOT_KEY;
    memcpy(b->uid, (const void *)UID_ADDR, 8);
    memcpy(b->root, root, 32);
    b->product = SEALED_PRODUCT;
#ifdef INSECURE
    static const uint8_t person[8] = "LFinsec1";
    blake2s_state S;
    b2s_init(&S, 32, NULL, 0, NULL, person);
    b2s_puts(&S, b->uid, 8);
    b2s_final(&S, b->private_key);
    crypto_wipe(&S, sizeof S);
#else
    board_record r;
    uint8_t check[16];
    static const uint8_t person[8] = "LFrecord";
    blake2s_state S;
    memcpy(&r, (const void *)BOARD_RECORD_ADDR, sizeof r);
    b2s_init(&S, 16, NULL, 0, NULL, person);
    b2s_puts(&S, (const uint8_t *)&r, 48);
    b2s_final(&S, check);
    if (r.magic != BOARD_RECORD_MAGIC || memcmp(check, r.check, 16) != 0) {
        crypto_wipe(&r, sizeof r);
        *why = "this board has no key: run enroll";
        return -1;
    }
    memcpy(b->private_key, r.private_key, 32);
    b->min_serial = r.min_serial;
    crypto_wipe(&r, sizeof r);
#endif
    return 0;
#endif
}

/* ------------------------------------------------------------------------
 * The card: lfstage.bin and lfbackup.bin in the exFAT root
 */
static void card_open(void) {
    char msg[100];
    io.stage_blocks = io.backup_blocks = 0;
    if (SD_Init() != SD_OK) {
        NVIC_DisableIRQ(SDIO_IRQn);
        printf("V3F update: no SD card\r\n");
        return;
    }
    NVIC_DisableIRQ(SDIO_IRQn);             /* polling, as the V5F does */
    uint64_t n = (uint64_t)SDCardInfo.CardCapacity / SECTOR_BYTES;
    card_sectors = (n > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)n;
    if (card_find_blocks(card_read, card_sectors, &map, msg, sizeof msg) != 0 || !map.mounted) {
        printf("V3F update: no exFAT volume on the card\r\n");
        return;
    }
    if (card_find_file(card_read, card_sectors, &map, CARD_STAGE_FILE, &stage_base,
                       &io.stage_blocks) != 0) {
        io.stage_blocks = 0;
    }
    if (card_find_file(card_read, card_sectors, &map, CARD_BACKUP_FILE, &backup_base,
                       &io.backup_blocks) != 0) {
        io.backup_blocks = 0;
    }
}

/* ------------------------------------------------------------------------
 * Stage 1
 */
static void io_init(void) {
    io.flash_read = f_read;
    io.flash_erase = f_erase;
    io.flash_write = f_write;
    io.stage_read = s_read;
    io.backup_read = b_read;
    io.backup_write = b_write;
    io.random = rng;
    io.hw_rev = 0;
    io.adopt_blank = 1;                     /* development: see stage1.h */
}

void stage1_boot(void) {
    static upd_record rec;
    static char msg[160];
    sealed_board keys;
    const char *why = "";
    io_init();
    int waiting = (CDC_SHARED->update == CDC_UPDATE_MAGIC);
    int r = upd_record_load(&io, &rec);
    if (r == 1 && !waiting) {
        return;                             /* never updated: boot as before */
    }
    if (r < 0) {
        printf("V3F update: can't read the security record (%d)\r\n", r);
        return;
    }
    if (keys_get(&keys, &why) != 0) {
        printf("V3F update: %s; nothing done\r\n", why);
        CDC_SHARED->update = 0;
        return;
    }
    int busy = (r == 0 && rec.state != UPD_IDLE);
    card_open();                            /* a record exists, or a package waits */
    int result = upd_boot(&io, &keys, waiting, &rec, msg, sizeof msg);
    crypto_wipe(&keys, sizeof keys);
    CDC_SHARED->update = 0;                 /* the package has been dealt with */
    CDC_FENCE();
    if (result < 0) {
        printf("V3F update: stopped by error %d (%s); it carries on at the next reset\r\n",
               result, msg);
    } else if (waiting || busy || result != UPD_START) {
        printf("V3F %s\r\n", msg);
    }
    if (result == UPD_RECOVERY) {
        printf("V3F update: the V5F image fails its check; starting it anyway "
               "(no recovery mode yet)\r\n");
    }
}

void stage1_poll(void) {
    static upd_record rec;
    if (CDC_SHARED->update != CDC_CONFIRM_MAGIC) {
        return;
    }
    CDC_SHARED->update = 0;
    CDC_FENCE();
    if (upd_record_load(&io, &rec) == 0 && rec.state == UPD_TRIAL) {
        int err = upd_confirm(&io);
        if (err) {
            printf("V3F update: the confirm failed (%d)\r\n", err);
        } else {
            printf("V3F update: version %lu confirmed\r\n", (unsigned long)rec.v5f_version);
        }
    }
}
