/*
 * Block mass storage for the CH32H417 (V5F): a raw partition on the
 * microSD card. See blocks.h, and doc/sdcard.md for the card layout.
 *
 * The card is reached through the SDIO peripheral with WCH's driver
 * (sdio.c, from EVT/EXAM/SDIO/SDIO_SD, unmodified) in polling mode. Its
 * DMA mode would use DMA1 channel 1, which the V3F's UART code also uses,
 * so it stays off. SD_ReadDisk / SD_WriteDisk take 512-byte sector numbers
 * and handle both byte-addressed (SDSC) and block-addressed (SDHC/SDXC)
 * cards. A 4 KB block is 8 sectors; LiteForth's block buffers are cell
 * arrays, so they meet the driver's 4-byte alignment for direct transfers.
 *
 * The partition is the first MBR entry of type 0xDA. It must start on a
 * 64 KB boundary (a multiple of 128 sectors), and its block 0 must start
 * with BLK_SIGNATURE; otherwise only block 0 is readable.
 */
#include <string.h>
#include "debug.h"
#include "sdio.h"
#include "blocks.h"
#include "../../../../errcodes.h"   /* src/errcodes.h */

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

int blk_read(uint32_t blk, uint32_t *dest) {
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    if (SD_ReadDisk((u8 *)dest, first_sector + blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK) != SD_OK) {
        return ERR_BLK_READ_FAIL;
    }
    return 0;
}

int blk_write(uint32_t blk, uint32_t *src) {
    if (!writable) {
        return ERR_BLK_WRITE_PROTECTED;
    }
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    if (SD_WriteDisk((u8 *)src, first_sector + blk * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK) != SD_OK) {
        return ERR_BLK_WRITE_FAIL;
    }
    return 0;
}
