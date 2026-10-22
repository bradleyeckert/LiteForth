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

/* The files to look at, gathered first: exfat_list's walk and the reads
   of the logs can't share exfat.c's sector buffer */
#define MAX_OPEN 8

typedef struct {
    uint64_t bytes;
    int n;
    char names[MAX_OPEN][16];
    exfat_file files[MAX_OPEN];
} open_logs;

static int gather(void *ctx, const char *name, const exfat_file *f) {
    open_logs *o = ctx;
    if (o->n < MAX_OPEN && f->length == o->bytes && f->valid == 0
        && (f->flags & EXFAT_NOFATCHAIN) && !(f->flags & EXFAT_DIRECTORY)
        && strlen(name) < sizeof o->names[0]) {
        strcpy(o->names[o->n], name);
        o->files[o->n++] = *f;
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

int log_recover(exfat_read_fn read, exfat_write_fn write, const exfat_vol *vol,
                uint64_t bytes, uint32_t timestamp, log_found_fn found, void *ctx) {
    static open_logs o;
    static uint32_t sector[DL_SECTOR / 4];
    memset(&o, 0, sizeof o);
    o.bytes = bytes;
    int err = exfat_list(read, vol, vol->root, gather, &o);
    if (err) {
        return err;
    }
    int closed = 0;
    for (int i = 0; i < o.n; i++) {
        exfat_file *f = &o.files[i];
        f->first_sector = exfat_cluster_sector(vol, f->first_cluster);
        file_reader r = { read, f->first_sector };
        int32_t blocks = f->first_sector
            ? dl_find_end(read_file, &r, (uint32_t)(bytes / DL_RECORD), (uint8_t *)sector)
            : 0;
        int e = blocks < 0 ? (int)blocks : 0;
        if (blocks > 0) {
            e = log_close(read, write, vol, f, (uint32_t)blocks, timestamp);
            if (e == 0) {
                closed++;
            }
        }
        if (found) {
            found(ctx, o.names[i], blocks > 0 ? (uint32_t)blocks : 0, e);
        }
    }
    return closed;
}
