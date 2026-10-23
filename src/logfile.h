#ifndef LOGFILE_H
#define LOGFILE_H

/*
 * Data log files on the card's exFAT volume (doc/admin/data-logs.md
 * sections 5 and 6): making one, closing it, and closing any that a power
 * cut left open. The file format and keys are datalog.h's; the volume is
 * exfat.h's. Shared by the board and the desktop tests (unit_tests/datalog).
 */

#include <stdint.h>
#include "exfat.h"

/**
 * Makes the log file `name` in the root directory: `bytes` long (4 GB on
 * the board), contiguous, ValidDataLength 0 (exfat_create). The log is
 * then written block by block from file->first_sector, block 0 first.
 * Returns 0 or exfat_create's error.
 */
int log_make(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
             const char *name, uint64_t bytes, uint32_t timestamp, exfat_file *file);

/**
 * Closes a log of `blocks` blocks (block 0 and the records written):
 * trims its file to blocks * DL_RECORD bytes (exfat_trim).
 * Returns 0 or exfat_trim's error.
 */
int log_close(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
              exfat_file *file, uint32_t blocks, uint32_t timestamp);

/* What log_recover found, for each file it looked at: its name, the
   blocks of the log it closed (0: deleted, or not a log and left alone),
   and an error, or 0 */
typedef void (*log_found_fn)(void *ctx, const char *name, uint32_t blocks, int err);

/**
 * Closes every log a power cut left open: the root directory's files
 * `bytes` long (as log_make made them) with ValidDataLength 0 and
 * NoFatChain. One whose block 0 is a whole log header: its end found by
 * its records' frames (dl_find_end), with no key, and trimmed there. One
 * whose first sector is clear or a torn header (the power went before or
 * while block 0 was written): deleted. Anything else: left alone. Calls
 * found (if not NULL) for each file looked at, with the blocks kept (0
 * for a file deleted or left alone).
 * Returns the number of logs closed, or a negative error.
 */
int log_recover(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
                uint64_t bytes, uint32_t timestamp, log_found_fn found, void *ctx);

#endif /* LOGFILE_H */
