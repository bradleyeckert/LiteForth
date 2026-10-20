#ifndef YMODEM_H
#define YMODEM_H

#define YMODEM_INSTALL  0xFFFFFFFFu     /* -1 ymodem: a whole block file */
#define YMODEM_STAGE    0xFFFFFFFEu     /* -2 ymodem: a firmware update package */

/**
 * @brief Forth word `ymodem`  ( n -- )
 * Receives files over the terminal by YMODEM (batch; CRC-16; 128-byte and
 * 1K packets) and writes them into blocks. The header packet of each file
 * gives its name and length, so exactly that many bytes are kept: the
 * padding of the last packet never reaches the blocks.
 *
 * n = 0: each file is Forth source, written the way scripts/putblocks.py
 * does:
 *   - `( BLOCK n )` starts block n, `( BLOCK++ )` the block after the last
 *     one started, and `( BLOCK IGNORE )` skips the lines up to the next
 *     marker, as does text before the first marker. Marker lines aren't
 *     stored.
 *   - Each line becomes a 128-column row, padded with spaces, and the rows
 *     after a block's last line are spaces. A UTF-8 BOM at the start and
 *     CRs are dropped.
 *   Each block is filled in a block buffer (`buffer`, no read) and written
 *   when the next marker or the end of the file comes (`update
 *   save-buffers`). At the first problem (a line longer than 128 bytes,
 *   more than 32 lines in a block, block 0, a block past the end of the
 *   storage, `( BLOCK++ )` before any numbered block, or a write that
 *   fails) it stops writing that file, keeps receiving, and reports the
 *   problem with its line number. The blocks written before it stay
 *   written.
 *
 * n > 0: each file is binary, written the way scripts/putbin.py does: one
 * stream from byte 32 of block n (bytes 0-31 of block n are zeros, kept
 * for an HMAC) on through the blocks after it, the rest of the last block
 * zeros. The next file of the batch starts in the block after the last
 * one. A file that wouldn't fit, or whose header has no length, isn't
 * written.
 *
 * n = -1: one whole block file (an lfblocks.fb4, plain or sealed) replaces
 * the one there, block 0 included, so the card needn't come out of the
 * board. It goes into the space the block file has (blk_space: on a card,
 * lfblocks.fb4's size, which the board can't change), so it must be whole
 * 4 KB blocks and fit; any blocks after it are left as they were. Block 0
 * is checked first (blk_check_image): it must start with BLK_SIGNATURE,
 * and a sealed file must be one this board would mount. Nothing is
 * written until it passes. Then block 0 says the install hasn't finished
 * while the other blocks are written, and the real block 0 goes in last,
 * so a transfer cut short leaves a plain block file saying so: send the
 * file again. When the batch ends, the file is read back and compared
 * with what came (BLAKE2s), and mounted again (blk_remount), which sets
 * `capacity`; every content block of a sealed file is read to check its
 * hash. The block buffers are emptied before and after, discarding
 * unsaved changes. One file per batch.
 *
 * n = -2: one firmware update package (from `sealblocks.py package`,
 * doc/admin/firmware-updates.md) into lfstage.bin, a file made beforehand
 * (blk_stage_space: on a card, in the exFAT root beside lfblocks.fb4).
 * It must be whole 4 KB blocks, fit, and start with PACKAGE_MAGIC; block
 * 0 is zeros while the rest go in and is written last, so a transfer cut
 * short never leaves something that looks like a package. Then it's read
 * back and compared (BLAKE2s), and checked as stage 1 will check it
 * (blk_stage_check: signature, certificate, product, recipient, every
 * block's hash and the image hash; not the board's minimum version). If
 * it passes, blk_stage_ready tells stage 1 a package is waiting. The
 * block file is mounted again afterwards. One package per batch.
 *
 * After `ymodem`, start the send in the terminal program (kwai: `~x FILE`
 * types `0 ymodem`, `~x FILE N` types `N ymodem`, and `~x FILE -1` and
 * `~x FILE -2` the same with -1 and -2; in RealTerm: Send tab, YMODEM). `ymodem` asks with `C` every 3 seconds and gives up after
 * YMODEM_START_SECONDS (default 60). It reports each file as it ends: the
 * blocks it wrote and any problem.
 *
 * While it runs, the bytes from the terminal are data, so serial_binary
 * turns off the CH32H417's Ctrl+X x3 escape.
 *
 * @return 0 on success; ERR_YMODEM_TRANSFER if the sender never started,
 *         cancelled, or the transfer failed after YMODEM_RETRIES tries;
 *         ERR_YMODEM_SOURCE for a problem with a file; ERR_BLOCK_READ_ERROR
 *         if an installed sealed file's blocks fail their hashes; or the
 *         error from writing a block. With several files, the first
 *         problem's.
 */
int lfAPI_ymodem(void);

#endif /* YMODEM_H */
