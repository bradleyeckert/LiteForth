#ifndef XMODEM_H
#define XMODEM_H

/**
 * @brief Forth word `xmodem`  ( -- )
 * Receives a source file over the terminal with XMODEM (8-bit checksum;
 * 128-byte packets, or 1K packets as XMODEM-1K senders use) and writes it
 * into blocks the way scripts/putblocks.py does:
 *   - `( BLOCK n )` starts block n, `( BLOCK++ )` the block after the last
 *     one started, and `( BLOCK IGNORE )` skips the lines up to the next
 *     marker, as does text before the first marker. Marker lines aren't
 *     stored.
 *   - Each line becomes a 128-column row, padded with spaces, and the rows
 *     after a block's last line are spaces. A UTF-8 BOM at the start, CRs
 *     and the SUB (1Ah) padding of the last packet are dropped.
 * Each block is filled in a block buffer (`buffer`, no read) and written
 * when the next marker or the end of the file comes (`update
 * save-buffers`).
 *
 * After `xmodem`, start the send in the terminal program (in RealTerm:
 * Send tab, XMODEM with checksum). `xmodem` sends NAK every 3 seconds and
 * gives up after XMODEM_START_SECONDS (default 60). When the transfer
 * ends, it lists the blocks it wrote and any problem with the source.
 *
 * Unlike putblocks.py, which checks the whole file first, `xmodem` writes
 * blocks as they arrive. At the first problem (a line longer than 128
 * bytes, more than 32 lines in a block, block 0, a block past the end of
 * the storage, `( BLOCK++ )` before any numbered block, or a write that
 * fails) it stops writing, keeps receiving to the end of the transfer, and
 * reports the problem with its line number. The blocks written before it
 * stay written.
 *
 * @return 0 on success; ERR_XMODEM_TRANSFER if the sender never started,
 *         cancelled, or the transfer failed after XMODEM_RETRIES tries;
 *         ERR_XMODEM_SOURCE for a problem with the source; or the error
 *         from writing a block.
 */
int lfAPI_xmodem(void);

#endif /* XMODEM_H */
