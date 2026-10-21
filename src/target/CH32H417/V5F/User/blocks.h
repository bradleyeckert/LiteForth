#ifndef BLOCKS_H
#define BLOCKS_H

#include <stdint.h>
#include "options.h"
#include "../../../../sealed.h"     /* sealed_package */
#include "../../../../exfat.h"      /* exfat_list_fn, for blk_dir */

#define BLK_SIZE_BYTES           (BLOCK_SIZE_CELLS * sizeof(uint32_t))
#define BLK_MAX_LENGTH           8   /* most blocks one blk_*_imm call moves */
#define BLK_SECTOR_BYTES         512
#define BLK_MAX_SECTORS          64  /* most sectors one blk_sector_*_imm call moves */

/* The first bytes of block 0 of every LiteForth block device. Without
   them, the device isn't LiteForth's: it reports 1 block, which can be
   read but not written, so its block 0 can be looked at but nothing on
   it is changed. */
#define BLK_SIGNATURE            "LITEFORTH"
#define BLK_SIGNATURE_LEN        9

/*
 * Block mass storage for the CH32H417 (V5F), on the microSD card, reached
 * through the SDIO peripheral (WCH's driver in sdio.c; nanoCH32H417 pins
 * CLK PB11, CMD PB10, D0-D3 PE8-PE11). Same interface as
 * src/target/desktop/blocks.h. doc/exfat.md and doc/sdcard.md explain how
 * to prepare a card.
 */

/**
 * Starts the SD card and finds the block space (card.h): lfblocks.fb4 in
 * the root directory of the card's exFAT volume, if it is contiguous. If
 * its block 0 starts with BLK_SIGNATURE, all its blocks can be read and
 * written: the capacity is the file's ValidDataLength / 4096. If not, the
 * capacity is 1: block 0 can be read, to see what's there, and nothing
 * can be written. A sealed file is mounted read-only (sealed.c); with
 * SECURITY_LEVEL 2 (Common/security.h), a plain one offers no blocks. With no card, no exFAT volume or no lfblocks.fb4, the
 * capacity is 0. What was found goes to the debug UART (USART1).
 * filename: ignored.
 * capacity: if not NULL, receives the number of blocks.
 * Returns 0: a missing or unusable card isn't an error, just no blocks.
 */
int blk_init(char *filename, uint32_t *capacity);

/**
 * Reads a single 4KB block payload into the destination buffer, and waits
 * until it's done (blk_read_imm, then blk_status). A transfer still in
 * progress is waited for first.
 * Returns 0 if okay, ERR_BLK_BOUNDS if blk is past the capacity, or
 * ERR_BLK_READ_FAIL if the card reports an error.
 */
int blk_read(uint32_t blk, uint32_t *dest);

/**
 * Writes a single 4KB block payload from the source buffer, and waits
 * until it's done (blk_write_imm, then blk_status). A transfer still in
 * progress is waited for first.
 * Returns 0 if okay, ERR_BLK_WRITE_PROTECTED if block 0 doesn't start
 * with BLK_SIGNATURE, ERR_BLK_BOUNDS if blk
 * is past the capacity, or ERR_BLK_WRITE_FAIL if the card reports an
 * error. Overwriting the signature in block 0 takes effect at the next
 * blk_init.
 */
int blk_write(uint32_t blk, uint32_t *src);

/**
 * Starts reading length blocks (1 to BLK_MAX_LENGTH), from block blk on,
 * into dest (length * BLOCK_SIZE_CELLS cells) and returns
 * without waiting for it to finish; blk_status says when it has. dest must
 * stay valid and untouched until then.
 * The transfer runs by DMA (DMA2 channel 1); blk_status moves it along
 * (BLK_DMA 0 in blocks.c: WCH's polling transfer, done before returning).
 * Returns 0 if the read started, ERR_BLK_BUSY if a transfer is still in
 * progress, ERR_INVALID_NUMERIC if length is out of range, ERR_BLK_BOUNDS
 * if the blocks go past the capacity, or another negative error code that
 * shows up at once.
 */
int blk_read_imm(uint32_t blk, uint32_t *dest, int length);

/**
 * Starts writing length blocks (1 to BLK_MAX_LENGTH), from block blk on,
 * from src (length * BLOCK_SIZE_CELLS cells) and returns
 * without waiting for it to finish; blk_status says when it has. src must
 * stay valid and unchanged until then.
 * The transfer runs by DMA (DMA2 channel 1); blk_status moves it along
 * (BLK_DMA 0 in blocks.c: WCH's polling transfer, done before returning).
 * Returns 0 if the write started, ERR_BLK_BUSY if a transfer is still in
 * progress, ERR_BLK_WRITE_PROTECTED, ERR_INVALID_NUMERIC, ERR_BLK_BOUNDS,
 * or another negative error code that shows up at once.
 */
int blk_write_imm(uint32_t blk, uint32_t *src, int length);

/**
 * The state of the last transfer started by blk_read_imm or blk_write_imm
 * (blk_read and blk_write use them too).
 * Returns 0 if it finished without error (or none was started), a negative
 * error code if it failed, 1 while a read is in progress, or 2 while a
 * write is in progress. The result stays until the next transfer starts.
 */
int blk_status(void);

/**
 * Starts reading count card sectors (512 bytes each, 1 to BLK_MAX_SECTORS)
 * from sector lba into dest (count * 128 cells), without waiting;
 * blk_status says when it's done. These reach the whole card, not just the
 * block space, for code that reads the file system (exfat.c, Forth).
 * Returns 0 if the read started, ERR_BLK_BUSY, ERR_INVALID_NUMERIC for a
 * count out of range, ERR_BLK_BOUNDS past the end of the card,
 * or another negative error code that shows up at once.
 */
int blk_sector_read_imm(uint32_t lba, uint32_t *dest, int count);

/**
 * Starts writing count card sectors from src, like blk_sector_read_imm.
 * Writes are fenced to the card's exFAT volume (blk_write_fence), so they
 * can't reach the partition table or anything outside the volume.
 * Returns 0 if the write started, ERR_BLK_WRITE_PROTECTED outside the
 * fence, or the errors of blk_sector_read_imm.
 */
int blk_sector_write_imm(uint32_t lba, uint32_t *src, int count);

/**
 * Reads count card sectors and waits until it's done: blk_sector_read_imm,
 * then blk_status, after waiting for a transfer still in progress.
 * Returns 0 or a negative error code.
 */
int blk_sector_read(uint32_t lba, uint32_t *dest, int count);

/**
 * Tells whether count sectors from lba may be written by
 * blk_sector_write_imm: they must all be inside the exFAT volume that
 * blk_init found.
 * Returns 0 if they may, else ERR_BLK_WRITE_PROTECTED.
 */
int blk_write_fence(uint32_t lba, int count);

/*
 * Installing a whole block file (`-1 ymodem`, ymodem.h): the image is
 * written into the space the block file already has, block 0 included,
 * whatever the file holds now (plain, sealed, or unsigned on a card).
 */

/**
 * The blocks an installed image may fill: lfblocks.fb4's
 * ValidDataLength / 4096, whatever is in it, or 0 without one.
 */
uint32_t blk_space(void);

/**
 * Reads length blocks (1 to BLK_MAX_LENGTH) of the space from block blk
 * into dest as they are on the device (no decryption), and waits.
 * Returns 0, ERR_INVALID_NUMERIC, ERR_BLK_BOUNDS past blk_space, or
 * another negative error code.
 */
int blk_raw_read(uint32_t blk, uint32_t *dest, int length);

/**
 * Writes length blocks (1 to BLK_MAX_LENGTH) from src into the space from
 * block blk as they are, whatever the signature or a sealed file says,
 * and waits. Only for installing an image; blk_remount afterwards.
 * Returns 0, ERR_INVALID_NUMERIC, ERR_BLK_BOUNDS past blk_space, or
 * another negative error code.
 */
int blk_raw_write(uint32_t blk, const uint32_t *src, int length);

/**
 * Checks block 0 of an image of `blocks` blocks before it's installed: a
 * plain one (BLK_SIGNATURE, not SEALED_MAGIC) passes unless sealed_root.h
 * at SECURITY_LEVEL 2; a sealed one must
 * be one this board would mount (board_get, sealed_mount: certificate, signature,
 * product, revision, release key serial, a recipient), so a file the
 * board can't use never replaces one it can. A firmware update package
 * (PACKAGE_MAGIC) is refused. Unmounts a mounted sealed
 * file: call blk_remount afterwards. msg receives the reason it fails.
 * Returns 0, or ERR_BLK_PARSE_FAIL.
 */
int blk_check_image(const uint8_t *b0, uint32_t blocks, char *msg, int len);

/**
 * Finds and mounts the block file again, as blk_init does after starting
 * the card (which it doesn't restart), after an image has been installed.
 * capacity: if not NULL, receives the number of blocks.
 * Returns 0 or a negative error code, as blk_init.
 */
int blk_remount(uint32_t *capacity);

/*
 * Staging a firmware update package (`-2 ymodem`, doc/admin/firmware-updates.md
 * section 6): CARD_STAGE_FILE (lfstage.bin) in the card's exFAT root, made
 * beforehand in a PC and written in place, found by card_find_file when
 * the card is mounted.
 */

/**
 * Returns lfstage.bin's size in 4 KB blocks, or 0 if there isn't one.
 */
uint32_t blk_stage_space(void);

/**
 * Reads or writes block blk of lfstage.bin (4096 bytes; the buffer
 * 4-byte aligned, for the DMA), waiting for the transfer.
 * Returns 0, ERR_BLK_BOUNDS if blk is past its end (or there's none), or
 * a transfer error.
 */
int blk_stage_read(uint32_t blk, uint32_t *dest);
int blk_stage_write(uint32_t blk, const uint32_t *src);

/**
 * Checks the package in lfstage.bin as stage 1 will (sealed_check_package,
 * with board_get's keys). Unmounts a mounted sealed block file: call
 * blk_remount afterwards. msg receives the result.
 * Returns 0 or the error from the check.
 */
int blk_stage_check(sealed_package *pkg, char *msg, int len);

/**
 * Tells stage 1 a checked package is waiting in lfstage.bin: sets
 * `update` in the shared block (cdc_shared.h) to CDC_UPDATE_MAGIC. Stage
 * 1 isn't built yet, so for now nothing acts on it.
 */
void blk_stage_ready(void);

/*
 * Saving LiteForth's flash pages (`save-pages`, doc/admin/firmware-updates.md
 * section 5): lfpages.bin, a file made beforehand that the pages are
 * written into in place, from page 0 on, for the administrator to make an
 * app package from (sealblocks.py package --component app). Found as
 * lfstage.bin is: CARD_PAGES_FILE beside lfblocks.fb4 on a card, or
 * PAGESFILENAME beside a plain block file.
 */

/**
 * Returns lfpages.bin's size in 4 KB blocks, or 0 if there isn't one.
 */
uint32_t blk_pages_space(void);

/**
 * Writes block blk of lfpages.bin (4096 bytes).
 * Returns 0, ERR_BLK_BOUNDS if blk is past its end (or there's none),
 * ERR_WRITE_PROTECTED where the pages are locked (the CH32H417 at
 * SECURITY_LEVEL 2), or a write error.
 */
int blk_pages_write(uint32_t blk, const uint32_t *src);

/**
 * Lists the root directory of the card's exFAT volume (card.c's
 * card_list): fn gets each file's and directory's name and entry.
 * Returns 0, ERR_BLK_OPEN_FAIL if there's no card volume (no card, or no exFAT volume on it), fn's
 * nonzero result, or a read or format error.
 */
int blk_dir(exfat_list_fn fn, void *ctx);

#endif /* BLOCKS_H */
