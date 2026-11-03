/*
 * Block storage for the ESP32-C5 -- milestone 1 stub: no card yet.
 *
 * The real backend is milestone 3: a microSD over SPI (the C5 has no SD host)
 * driving the shared src/card.c + src/exfat.c, as on the CH32H417. Until then
 * every block op reports "no card" and capacity is 0, so the REPL and flash
 * (colon definitions) work without storage. See doc/ESP32C5/esp32c5-notes.md.
 */
#include "blocks.h"
#include "../../../errcodes.h"   /* src/errcodes.h */

int  blk_init(char *filename, uint32_t *capacity) { (void)filename; if (capacity) *capacity = 0; return 0; }
void blk_set_board(const sealed_board *board)      { (void)board; }

int blk_read(uint32_t blk, uint32_t *dest)                 { (void)blk; (void)dest; return ERR_BLK_BOUNDS; }
int blk_write(uint32_t blk, uint32_t *src)                 { (void)blk; (void)src;  return ERR_BLK_BOUNDS; }
int blk_read_imm(uint32_t blk, uint32_t *dest, int length) { (void)blk; (void)dest; (void)length; return ERR_BLK_BOUNDS; }
int blk_write_imm(uint32_t blk, uint32_t *src, int length) { (void)blk; (void)src;  (void)length; return ERR_BLK_BOUNDS; }
int blk_status(void)                                       { return 0; }

int blk_sector_read_imm(uint32_t lba, uint32_t *dest, int count)  { (void)lba; (void)dest; (void)count; return ERR_BLK_READ_FAIL; }
int blk_sector_write_imm(uint32_t lba, uint32_t *src, int count)  { (void)lba; (void)src;  (void)count; return ERR_BLK_WRITE_FAIL; }
int blk_sector_read(uint32_t lba, uint32_t *dest, int count)      { (void)lba; (void)dest; (void)count; return ERR_BLK_READ_FAIL; }
int blk_write_fence(uint32_t lba, int count)                      { (void)lba; (void)count; return 0; }

uint32_t blk_space(void) { return 0; }

int blk_raw_read(uint32_t blk, uint32_t *dest, int length)        { (void)blk; (void)dest; (void)length; return ERR_BLK_READ_FAIL; }
int blk_raw_write(uint32_t blk, const uint32_t *src, int length)  { (void)blk; (void)src;  (void)length; return ERR_BLK_WRITE_FAIL; }

int blk_check_image(const uint8_t *b0, uint32_t blocks, char *msg, int len) {
    (void)b0; (void)blocks; (void)msg; (void)len; return ERR_BLK_PARSE_FAIL;
}
int blk_remount(uint32_t *capacity) { if (capacity) *capacity = 0; return 0; }

uint32_t blk_stage_space(void) { return 0; }
int blk_stage_read(uint32_t blk, uint32_t *dest)        { (void)blk; (void)dest; return ERR_BLK_READ_FAIL; }
int blk_stage_write(uint32_t blk, const uint32_t *src)  { (void)blk; (void)src;  return ERR_BLK_WRITE_FAIL; }
int blk_stage_check(sealed_package *pkg, char *msg, int len) { (void)pkg; (void)msg; (void)len; return ERR_BLK_PARSE_FAIL; }
void blk_stage_ready(void) { }

uint32_t blk_pages_space(void) { return 0; }
int blk_pages_write(uint32_t blk, const uint32_t *src)  { (void)blk; (void)src; return ERR_BLK_WRITE_FAIL; }

int blk_dir(exfat_list_fn fn, void *ctx) { (void)fn; (void)ctx; return 0; }
