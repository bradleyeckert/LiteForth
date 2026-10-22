/* Data log files on the card's exFAT volume (logfile.h) */
#include <string.h>
#include "logfile.h"
#include "datalog.h"
#include "errcodes.h"

int log_make(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
             const char *name, uint64_t bytes, uint32_t timestamp, exfat_file *file) {
    return exfat_create(read, write, vol, name, bytes, timestamp, file);
}

int log_close(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
              exfat_file *file, uint32_t blocks, uint32_t timestamp) {
    return exfat_trim(read, write, vol, file, (uint64_t)blocks * DL_RECORD, timestamp);
}

/* The files to look at, gathered a few at a time: exfat_list's walk and
   the reads of the logs can't share exfat.c's sector buffer */
#define MAX_OPEN 8

typedef struct {
    uint64_t bytes;
    int skip;                           /* candidates already dealt with and kept */
    int seen, n;
    char names[MAX_OPEN][16];
    exfat_file files[MAX_OPEN];
} open_logs;

static int gather(void *ctx, const char *name, const exfat_file *f) {
    open_logs *o = ctx;
    if (f->length == o->bytes && f->valid == 0 && (f->flags & EXFAT_NOFATCHAIN)
        && !(f->flags & EXFAT_DIRECTORY) && strlen(name) < sizeof o->names[0]
        && o->seen++ >= o->skip) {
        strcpy(o->names[o->n], name);
        o->files[o->n++] = *f;
        return o->n == MAX_OPEN;        /* enough for this round */
    }
    return 0;
}

/* dl_find_end's reader: sectors of the file at first_sector */
typedef struct {
    exfat_read_fn read;
    uint32_t first;
} file_reader;

static int read_file(void *ctx, uint32_t s, uint8_t *buf) {
    const file_reader *r = ctx;
    return r->read(r->first + s, buf);
}

/* What happens to a candidate: 1 closed or deleted (it's no longer one), 0
   kept (not a log), or a negative error */
static int recover_one(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
                       exfat_file *f, uint64_t bytes, uint32_t timestamp, uint32_t *blocks) {
    static uint32_t sector[DL_SECTOR / 4];
    *blocks = 0;
    f->first_sector = exfat_cluster_sector(vol, f->first_cluster);
    if (f->first_sector == 0) {
        return 0;
    }
    file_reader r = { read, f->first_sector };
    int32_t n = dl_find_end(read_file, &r, (uint32_t)(bytes / DL_RECORD), (uint8_t *)sector);
    if (n < 0) {
        return n;
    }
    if (n > 0) {
        *blocks = (uint32_t)n;
        int err = log_close(read, write, vol, f, (uint32_t)n, timestamp);
        return err ? err : 1;
    }
    /* No whole header: the power went before block 0 was written (log_make
       clears the first sector) or while it was. Deleted then; anything
       else is left alone. */
    int err = read(f->first_sector, (uint8_t *)sector);
    if (err) {
        return err;
    }
    const uint8_t *b = (const uint8_t *)sector;
    int empty = 1;
    for (uint32_t i = 0; i < DL_SECTOR; i++) {
        empty &= b[i] == 0;
    }
    if (!empty && memcmp(b, DL_MAGIC, 8) != 0) {
        return 0;
    }
    err = exfat_delete(read, write, vol, f);
    return err ? err : 1;
}

int log_recover(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
                uint64_t bytes, uint32_t timestamp, log_found_fn found, void *ctx) {
    static open_logs o;
    int closed = 0, skip = 0;
    for (;;) {
        memset(&o, 0, sizeof o);
        o.bytes = bytes;
        o.skip = skip;
        int err = exfat_list(read, vol, vol->root, gather, &o);
        if (err < 0) {
            return err;
        }
        for (int i = 0; i < o.n; i++) {
            uint32_t blocks;
            int r = recover_one(read, write, vol, &o.files[i], bytes, timestamp, &blocks);
            if (r == 0 || r < 0) {
                skip++;                 /* still a candidate next round: pass over it */
            } else if (blocks) {
                closed++;
            }
            if (found) {
                found(ctx, o.names[i], blocks, r < 0 ? r : 0);
            }
        }
        if (o.n < MAX_OPEN) {
            return closed;
        }
    }
}
