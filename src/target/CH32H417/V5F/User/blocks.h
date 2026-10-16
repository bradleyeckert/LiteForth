#ifndef BLOCKS_H
#define BLOCKS_H

#include <stdint.h>
#include "options.h"

#define BLK_SIZE_BYTES           (BLOCK_SIZE_CELLS * sizeof(uint32_t))

/* The first bytes of block 0 of every LiteForth block device. Without
   them, the device isn't LiteForth's: it reports 1 block, which can be
   read but not written, so its block 0 can be looked at but nothing on
   it is changed. */
#define BLK_SIGNATURE            "LITEFORTH"
#define BLK_SIGNATURE_LEN        9

/*
 * Block mass storage for the CH32H417 (V5F): a raw partition on the
 * microSD card, reached through the SDIO peripheral (WCH's driver in
 * sdio.c; nanoCH32H417 pins CLK PB11, CMD PB10, D0-D3 PE8-PE11).
 * Same interface as src/target/desktop/blocks.h. doc/sdcard.md explains
 * how to prepare a card.
 */

/**
 * Starts the SD card and finds the block partition: the first entry of
 * type 0xDA in the card's MBR. If the partition starts on a 64 KB
 * boundary and its block 0 starts with BLK_SIGNATURE, all its blocks can
 * be read and written (capacity = its size in 4 KB blocks). If not, the
 * capacity is 1: block 0 can be read, to see what's there, and nothing
 * can be written. With no card, no MBR or no type 0xDA partition, the
 * capacity is 0. The reason goes to the debug UART (USART1).
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
 * Returns 0 if okay, ERR_BLK_WRITE_PROTECTED if the partition doesn't
 * start with BLK_SIGNATURE (or isn't 64 KB aligned), ERR_BLK_BOUNDS if blk
 * is past the capacity, or ERR_BLK_WRITE_FAIL if the card reports an
 * error. Overwriting the signature in block 0 takes effect at the next
 * blk_init.
 */
int blk_write(uint32_t blk, uint32_t *src);

/**
 * Starts reading block blk into dest (BLOCK_SIZE_CELLS cells) and returns
 * without waiting for it to finish; blk_status says when it has. dest must
 * stay valid and untouched until then.
 * For now the transfer is done before this returns (polling); DMA is
 * planned.
 * Returns 0 if the read started, ERR_BLK_BUSY if a transfer is still in
 * progress, ERR_BLK_BOUNDS if blk is past the capacity, or another
 * negative error code that shows up at once.
 */
int blk_read_imm(uint32_t blk, uint32_t *dest);

/**
 * Starts writing block blk from src (BLOCK_SIZE_CELLS cells) and returns
 * without waiting for it to finish; blk_status says when it has. src must
 * stay valid and unchanged until then.
 * For now the transfer is done before this returns (polling); DMA is
 * planned.
 * Returns 0 if the write started, ERR_BLK_BUSY if a transfer is still in
 * progress, ERR_BLK_WRITE_PROTECTED, ERR_BLK_BOUNDS, or another negative
 * error code that shows up at once.
 */
int blk_write_imm(uint32_t blk, uint32_t *src);

/**
 * The state of the last transfer started by blk_read_imm or blk_write_imm
 * (blk_read and blk_write use them too).
 * Returns 0 if it finished without error (or none was started), a negative
 * error code if it failed, 1 while a read is in progress, or 2 while a
 * write is in progress. The result stays until the next transfer starts.
 */
int blk_status(void);

#endif /* BLOCKS_H */
