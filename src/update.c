/* Installing firmware updates: stage 1's logic. See update.h and
   doc/firmware-updates.md sections 3, 4 and 6. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "update.h"
#include "blake2s.h"
#include "ctr.h"
#include "errcodes.h"
#include "monocypher.h"

#define RECORD_MAGIC    0x4C465243u     /* "CRFL" */
#define RECORD_FORMAT   1u
#define BACKUP_MAGIC    "LFBACKUP"

static uint32_t buf_cells[UPD_BLOCK / 4] __attribute__((aligned(32)));
#define buf ((uint8_t *)buf_cells)
static uint32_t chk_cells[UPD_BLOCK / 4] __attribute__((aligned(32)));
#define chk ((uint8_t *)chk_cells)

/* ------------------------------------------------------------------------
 * Hashes and keys
 */
static void person8(uint8_t p[8], const char *s) {
    memset(p, 0, 8);
    memcpy(p, s, strlen(s) < 8 ? strlen(s) : 8);
}

static void b2s_start(blake2s_state *S, int size, const uint8_t *key, int klen,
                      const char *person) {
    uint8_t p[8];
    person8(p, person);
    b2s_init(S, size, key, klen, NULL, p);
}

/* The token's keys, one per use (section 4) */
static void derive(uint8_t *out, int size, const uint8_t token[32], const char *person) {
    blake2s_state S;
    b2s_start(&S, size, token, 32, person);
    b2s_final(&S, out);
    crypto_wipe(&S, sizeof S);
}

static void put32(uint8_t *p, uint32_t x) {
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(x >> (8 * i));
    }
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

/* The MAC of `length` bytes of flash at addr, as image `version`, under
   K_img (keyed BLAKE2s over the version, the length, then the bytes) */
static int image_mac(const upd_io *io, const upd_record *r, uint32_t addr,
                     uint32_t version, uint32_t length, uint8_t out[32]) {
    uint8_t k[32], vl[8];
    blake2s_state S;
    derive(k, 32, r->token, "LFimage");
    b2s_start(&S, 32, k, 32, "LFimgmac");
    crypto_wipe(k, sizeof k);
    put32(vl, version);
    put32(vl + 4, length);
    b2s_puts(&S, vl, 8);
    for (uint32_t at = 0; at < length; at += UPD_BLOCK) {
        uint32_t n = (length - at < UPD_BLOCK) ? length - at : UPD_BLOCK;
        int err = io->flash_read(addr + at, buf, n);
        if (err) {
            crypto_wipe(&S, sizeof S);
            return err;
        }
        b2s_puts(&S, buf, (int)n);
    }
    b2s_final(&S, out);
    crypto_wipe(&S, sizeof S);
    return 0;
}

/* The plaintext hash of `length` bytes of flash at addr, as a package's */
static int image_hash(const upd_io *io, uint32_t addr, uint32_t length, uint8_t out[32]) {
    blake2s_state S;
    b2s_start(&S, 32, NULL, 0, "LFimage");
    for (uint32_t at = 0; at < length; at += UPD_BLOCK) {
        uint32_t n = (length - at < UPD_BLOCK) ? length - at : UPD_BLOCK;
        int err = io->flash_read(addr + at, buf, n);
        if (err) {
            return err;
        }
        b2s_puts(&S, buf, (int)n);
    }
    b2s_final(&S, out);
    return 0;
}

/* ------------------------------------------------------------------------
 * The record
 */
static void record_hash(const upd_record *r, uint8_t out[32]) {
    blake2s_state S;
    b2s_start(&S, 32, NULL, 0, "LFrecord");
    b2s_puts(&S, (const uint8_t *)r, (int)offsetof(upd_record, hash));
    b2s_final(&S, out);
}

static int record_valid(const upd_record *r) {
    uint8_t h[32];
    if (r->magic != RECORD_MAGIC || r->format != RECORD_FORMAT) {
        return 0;
    }
    record_hash(r, h);
    return memcmp(h, r->hash, 32) == 0;
}

int upd_record_load(const upd_io *io, upd_record *rec) {
    static upd_record a, b;
    int err = io->flash_read(UPD_RECORD_A, &a, sizeof a);
    if (!err) {
        err = io->flash_read(UPD_RECORD_B, &b, sizeof b);
    }
    if (err) {
        return err;
    }
    int va = record_valid(&a), vb = record_valid(&b);
    if (!va && !vb) {
        return 1;
    }
    *rec = (va && (!vb || a.seq >= b.seq)) ? a : b;
    crypto_wipe(&a, sizeof a);
    crypto_wipe(&b, sizeof b);
    return 0;
}

/* Stores rec as its next sequence number, in the copy the last one isn't
   in (even numbers in A, odd in B), and reads it back */
static int record_store(const upd_io *io, upd_record *rec) {
    static upd_record back;
    rec->seq++;
    record_hash(rec, rec->hash);
    uint32_t at = (rec->seq & 1) ? UPD_RECORD_B : UPD_RECORD_A;
    int err = io->flash_erase(at);
    if (!err) {
        err = io->flash_write(at, rec, sizeof *rec);
    }
    if (!err) {
        err = io->flash_read(at, &back, sizeof back);
    }
    if (!err && memcmp(&back, rec, sizeof back) != 0) {
        err = ERR_FLASH_WRITE_SECTOR;
    }
    crypto_wipe(&back, sizeof back);
    return err;
}

/* ------------------------------------------------------------------------
 * Programming the V5F image's slot, one 4 KB block at a time
 */
static int program_block(const upd_io *io, uint32_t blk, const uint8_t *data) {
    uint32_t at = UPD_V5F_ADDR + blk * UPD_BLOCK;
    int err = 0;
    if (at % UPD_SECTOR == 0) {
        err = io->flash_erase(at);
    }
    if (!err) {
        err = io->flash_write(at, data, UPD_BLOCK);
    }
    if (!err) {
        err = io->flash_read(at, chk, UPD_BLOCK);
    }
    if (!err && memcmp(chk, data, UPD_BLOCK) != 0) {
        err = ERR_FLASH_WRITE_SECTOR;
    }
    return err;
}

/* After block blk is programmed: whether progress is stored now (at the
   end of a sector, or the last block), so a resume restarts at a sector */
static int sector_done(uint32_t blk, uint32_t blocks) {
    return ((blk + 1) * UPD_BLOCK) % UPD_SECTOR == 0 || blk + 1 == blocks;
}

static uint32_t blocks_of(uint32_t length) {
    return (length + UPD_BLOCK - 1) / UPD_BLOCK;
}

/* ------------------------------------------------------------------------
 * The package in lfstage.bin
 */
static const upd_io *current;           /* for stage_read, a sealed_read_fn */

static int stage_read(uint32_t blk, uint8_t *b) {
    return current->stage_read(blk, b);
}

/* Mounts the package and, with check_all, reads every content block
   against its hash and the image against the hash in block 0. Returns 0
   with pkg filled, or nonzero with msg saying why. */
static int mount_package(const upd_io *io, const sealed_board *keys, const upd_record *r,
                         sealed_package *pkg, int check_all, char *msg, int len) {
    sealed_board b = *keys;
    b.min_revision = r->min_v5f;
    b.min_serial = r->min_serial;
    current = io;
    int err = io->stage_read(0, buf);
    if (err) {
        snprintf(msg, len, "update: can't read lfstage.bin (%d)", err);
        return err;
    }
    err = sealed_mount_package(buf, io->stage_blocks, &b, stage_read, msg, len, pkg);
    crypto_wipe(&b, sizeof b);
    if (err) {
        return err;
    }
    if (pkg->component != SEALED_V5F) {
        snprintf(msg, len, "update refused: not a V5F image");
        return ERR_BLK_PARSE_FAIL;
    }
    if (pkg->hw_rev && pkg->hw_rev != io->hw_rev) {
        snprintf(msg, len, "update refused: made for hardware revision %lu",
                 (unsigned long)pkg->hw_rev);
        return ERR_BLK_PARSE_FAIL;
    }
    if (pkg->length > UPD_V5F_SLOT) {
        snprintf(msg, len, "update refused: larger than the V5F slot");
        return ERR_BLK_PARSE_FAIL;
    }
    if (!check_all) {
        return 0;
    }
    blake2s_state S;
    uint8_t h[32];
    b2s_start(&S, 32, NULL, 0, "LFimage");
    for (uint32_t n = 1; n < sealed_blocks(); n++) {
        err = io->stage_read(n, buf);
        if (!err) {
            err = sealed_open_block(n, buf);
        }
        if (err) {
            snprintf(msg, len, "update refused: block %lu of the package fails its hash",
                     (unsigned long)n);
            return err;
        }
        uint32_t at = (n - 1) * UPD_BLOCK;
        uint32_t take = (pkg->length - at < UPD_BLOCK) ? pkg->length - at : UPD_BLOCK;
        b2s_puts(&S, buf, (int)take);
    }
    b2s_final(&S, h);
    if (memcmp(h, pkg->image_hash, 32) != 0) {
        snprintf(msg, len, "update refused: the image doesn't match its hash");
        return ERR_BLK_PARSE_FAIL;
    }
    return 0;
}

/* ------------------------------------------------------------------------
 * The backup in lfbackup.bin: block 0 a header (magic, version, length,
 * nonce), then the image encrypted with K_encard; its MAC (K_maccard, over
 * every block as written) is kept in the record
 */
static void backup_keys(const upd_record *r, ctr_key *ck, uint8_t mk[32]) {
    uint8_t k[16];
    derive(k, 16, r->token, "LFcardenc");
    ctr_setkey(ck, CTR_AES128, k);
    crypto_wipe(k, sizeof k);
    derive(mk, 32, r->token, "LFcardmac");
}

/* The MAC of the backup as it is on the card; reads it all */
static int backup_mac(const upd_io *io, const upd_record *r, uint32_t length, uint8_t out[32]) {
    ctr_key ck;
    uint8_t mk[32];
    blake2s_state S;
    backup_keys(r, &ck, mk);
    ctr_wipe(&ck);
    b2s_start(&S, 32, mk, 32, "LFbackup");
    crypto_wipe(mk, sizeof mk);
    int err = 0;
    for (uint32_t n = 0; n <= blocks_of(length) && !err; n++) {
        err = io->backup_read(n, buf);
        if (!err) {
            b2s_puts(&S, buf, UPD_BLOCK);
        }
    }
    b2s_final(&S, out);
    crypto_wipe(&S, sizeof S);
    return err;
}

/* Copies the installed image to the card; sets the record's backup_* */
static int make_backup(const upd_io *io, upd_record *r) {
    uint32_t length = r->v5f_length, blocks = blocks_of(length);
    ctr_key ck;
    uint8_t mk[32], nonce[8];
    int err = io->random(nonce, sizeof nonce);
    if (err) {
        return err;
    }
    memset(buf, 0, UPD_BLOCK);
    memcpy(buf, BACKUP_MAGIC, 8);
    put32(buf + 8, r->v5f_version);
    put32(buf + 12, length);
    memcpy(buf + 16, nonce, 8);
    err = io->backup_write(0, buf);
    backup_keys(r, &ck, mk);
    crypto_wipe(mk, sizeof mk);
    for (uint32_t n = 1; n <= blocks && !err; n++) {
        memset(buf, 0xFF, UPD_BLOCK);
        uint32_t at = (n - 1) * UPD_BLOCK;
        uint32_t take = (length - at < UPD_BLOCK) ? length - at : UPD_BLOCK;
        err = io->flash_read(UPD_V5F_ADDR + at, buf, take);
        if (!err && ctr_crypt_block(&ck, nonce, n, buf, UPD_BLOCK) != 0) {
            err = ERR_BLOCK_READ_ERROR;
        }
        if (!err) {
            err = io->backup_write(n, buf);
        }
    }
    ctr_wipe(&ck);
    if (!err) {
        err = backup_mac(io, r, length, r->backup_mac);     /* as read back */
    }
    if (!err) {                         /* and the read back decrypts to the image */
        backup_keys(r, &ck, mk);
        crypto_wipe(mk, sizeof mk);
        for (uint32_t n = 1; n <= blocks && !err; n++) {
            uint32_t at = (n - 1) * UPD_BLOCK;
            uint32_t take = (length - at < UPD_BLOCK) ? length - at : UPD_BLOCK;
            err = io->backup_read(n, buf);
            if (!err && ctr_crypt_block(&ck, nonce, n, buf, UPD_BLOCK) != 0) {
                err = ERR_BLOCK_READ_ERROR;
            }
            if (!err) {
                err = io->flash_read(UPD_V5F_ADDR + at, chk, take);
            }
            if (!err && memcmp(buf, chk, take) != 0) {
                err = ERR_BLOCK_READ_ERROR;
            }
        }
        ctr_wipe(&ck);
    }
    if (!err) {
        r->backup_version = r->v5f_version;
        r->backup_length = length;
    }
    return err;
}

/* Whether lfbackup.bin still holds what the record says; reads its nonce */
static int backup_good(const upd_io *io, const upd_record *r, uint8_t nonce[8]) {
    uint8_t mac[32];
    if (r->backup_length == 0 || io->backup_read == NULL
        || backup_mac(io, r, r->backup_length, mac) != 0
        || crypto_verify32(mac, r->backup_mac) != 0
        || io->backup_read(0, buf) != 0
        || memcmp(buf, BACKUP_MAGIC, 8) != 0
        || get32(buf + 8) != r->backup_version || get32(buf + 12) != r->backup_length) {
        return 0;
    }
    memcpy(nonce, buf + 16, 8);
    return 1;
}

/* ------------------------------------------------------------------------
 * The steps
 */

/* UPD_ROLLING_BACK: restores the backup from block r->progress on */
static int roll_back(const upd_io *io, upd_record *r, char *msg, int len) {
    uint8_t nonce[8], mk[32];
    ctr_key ck;
    if (!backup_good(io, r, nonce) || r->backup_version < r->min_v5f) {
        r->state = UPD_IDLE;
        r->result = UPD_R_BROKEN;
        r->v5f_length = 0;              /* no working image: recovery */
        snprintf(msg, len, r->backup_length && r->backup_version < r->min_v5f
                 ? "update: the backup is older than this board's minimum; can't roll back"
                 : "update: the backup is missing or damaged; can't roll back");
        return record_store(io, r);
    }
    uint32_t blocks = blocks_of(r->backup_length);
    backup_keys(r, &ck, mk);
    crypto_wipe(mk, sizeof mk);
    int err = 0;
    for (uint32_t blk = r->progress; blk < blocks && !err; blk++) {
        err = io->backup_read(blk + 1, buf);
        if (!err && ctr_crypt_block(&ck, nonce, blk + 1, buf, UPD_BLOCK) != 0) {
            err = ERR_BLOCK_READ_ERROR;
        }
        if (!err) {
            err = program_block(io, blk, buf);
        }
        if (!err && sector_done(blk, blocks)) {
            r->progress = blk + 1;
            err = record_store(io, r);
        }
    }
    ctr_wipe(&ck);
    if (err) {
        return err;
    }
    r->v5f_version = r->backup_version;
    r->v5f_length = r->backup_length;
    err = image_mac(io, r, UPD_V5F_ADDR, r->v5f_version, r->v5f_length, r->v5f_mac);
    if (err) {
        return err;
    }
    r->state = UPD_IDLE;
    r->progress = 0;
    r->result = UPD_R_ROLLED_BACK;
    snprintf(msg, len, "update: rolled back to version %lu", (unsigned long)r->v5f_version);
    return record_store(io, r);
}

/* UPD_INSTALLING: programs the package from block r->progress on */
static int install(const upd_io *io, const sealed_board *keys, upd_record *r,
                   char *msg, int len) {
    sealed_package pkg;
    char why[120];
    if (mount_package(io, keys, r, &pkg, 0, why, sizeof why) != 0
        || pkg.version != r->pkg_version || pkg.length != r->pkg_length
        || memcmp(pkg.image_hash, r->pkg_hash, 32) != 0) {
        sealed_unmount();
        r->state = UPD_ROLLING_BACK;    /* the package changed under us */
        r->progress = 0;
        int err = record_store(io, r);
        return err ? err : roll_back(io, r, msg, len);
    }
    uint32_t blocks = blocks_of(pkg.length);
    int err = 0;
    for (uint32_t blk = r->progress; blk < blocks && !err; blk++) {
        err = io->stage_read(blk + 1, buf);
        if (!err) {
            err = sealed_open_block(blk + 1, buf);   /* checked again, just before */
        }
        if (err) {
            break;
        }
        err = program_block(io, blk, buf);
        if (!err && sector_done(blk, blocks)) {
            r->progress = blk + 1;
            err = record_store(io, r);
        }
    }
    sealed_unmount();
    uint8_t h[32];
    if (!err) {
        err = image_hash(io, UPD_V5F_ADDR, pkg.length, h);
    }
    if (!err && memcmp(h, pkg.image_hash, 32) != 0) {
        err = ERR_BLOCK_READ_ERROR;
    }
    if (err == ERR_BLOCK_READ_ERROR) {  /* a block changed: back to the old image */
        r->state = UPD_ROLLING_BACK;
        r->progress = 0;
        err = record_store(io, r);
        return err ? err : roll_back(io, r, msg, len);
    }
    if (err) {
        return err;
    }
    r->v5f_version = pkg.version;
    r->v5f_length = pkg.length;
    err = image_mac(io, r, UPD_V5F_ADDR, r->v5f_version, r->v5f_length, r->v5f_mac);
    if (err) {
        return err;
    }
    r->state = UPD_TRIAL;
    r->progress = 0;
    snprintf(msg, len, "update: version %lu installed, on trial", (unsigned long)pkg.version);
    return record_store(io, r);
}

/* UPD_IDLE with a package waiting: checks it, then backs up and installs */
static int start_update(const upd_io *io, const sealed_board *keys, upd_record *r,
                        char *msg, int len) {
    sealed_package pkg;
    int err = mount_package(io, keys, r, &pkg, 1, msg, len);
    sealed_unmount();
    if (err) {
        r->result = UPD_R_REFUSED;
        return record_store(io, r);
    }
    /* Back up the installed image only if it checks out: one that doesn't
       (changed in flash since its MAC was made) mustn't get a backup that
       a rollback would then trust. Without one, the backup on the card,
       if any, stays the one to roll back to. */
    int keep = 0;
    if (r->v5f_length) {
        uint8_t mac[32];
        err = image_mac(io, r, UPD_V5F_ADDR, r->v5f_version, r->v5f_length, mac);
        if (err) {
            return err;
        }
        keep = crypto_verify32(mac, r->v5f_mac) == 0;
    }
    uint32_t need = 1 + blocks_of(r->v5f_length);
    if (keep && (io->backup_write == NULL || io->backup_blocks < need)) {
        r->result = UPD_R_NO_BACKUP;
        snprintf(msg, len, "update refused: lfbackup.bin needs %lu blocks",
                 (unsigned long)need);
        return record_store(io, r);
    }
    r->pkg_version = pkg.version;
    r->pkg_length = pkg.length;
    r->pkg_min = pkg.min_version;
    memcpy(r->pkg_hash, pkg.image_hash, 32);
    r->state = UPD_BACKING_UP;
    r->progress = 0;
    r->result = UPD_R_NONE;
    if (keep) {
        r->backup_length = 0;           /* the card's backup is about to be overwritten */
    }
    err = record_store(io, r);
    if (err) {
        return err;
    }
    if (keep) {
        err = make_backup(io, r);
        if (err) {
            return err;
        }
    }
    r->state = UPD_INSTALLING;
    err = record_store(io, r);
    return err ? err : install(io, keys, r, msg, len);
}

int upd_boot(const upd_io *io, const sealed_board *keys, int package_waiting,
             upd_record *r, char *msg, int len) {
    snprintf(msg, len, "update: nothing to do");
    int err = upd_record_load(io, r);
    if (err < 0) {
        return err;
    }
    if (err == 1) {                     /* blank: the first boot after an erase */
        memset(r, 0, sizeof *r);
        r->magic = RECORD_MAGIC;
        r->format = RECORD_FORMAT;
        err = io->random(r->token, sizeof r->token);
        if (err) {
            return err;
        }
        if (io->adopt_blank) {
            r->v5f_length = UPD_V5F_SLOT;
            err = image_mac(io, r, UPD_V5F_ADDR, 0, UPD_V5F_SLOT, r->v5f_mac);
            if (err) {
                return err;
            }
        }
        err = record_store(io, r);
        if (err) {
            return err;
        }
        snprintf(msg, len, "update: a new record and device token%s",
                 io->adopt_blank ? "; the V5F image in flash adopted" : "");
    }

    switch (r->state) {
    case UPD_IDLE:
        if (package_waiting) {
            err = start_update(io, keys, r, msg, len);
        }
        break;
    case UPD_BACKING_UP:                /* cut short: back up again */
        r->state = UPD_IDLE;
        err = start_update(io, keys, r, msg, len);
        break;
    case UPD_INSTALLING:
        err = install(io, keys, r, msg, len);
        break;
    case UPD_TRIAL:
        if (++r->progress > UPD_TRIAL_BOOTS) {
            if (r->backup_length == 0) {    /* nothing to go back to: recovery */
                r->state = UPD_IDLE;
                r->result = UPD_R_BROKEN;
                snprintf(msg, len, "update: version %lu was never confirmed and there's "
                         "no backup", (unsigned long)r->v5f_version);
                r->v5f_length = 0;
                err = record_store(io, r);
            } else {
                r->state = UPD_ROLLING_BACK;
                r->progress = 0;
                err = record_store(io, r);
                if (!err) {
                    err = roll_back(io, r, msg, len);
                }
            }
        } else {
            snprintf(msg, len, "update: version %lu on trial, boot %lu of %u",
                     (unsigned long)r->v5f_version, (unsigned long)r->progress,
                     (unsigned)UPD_TRIAL_BOOTS);
            err = record_store(io, r);
        }
        break;
    case UPD_ROLLING_BACK:
        err = roll_back(io, r, msg, len);
        break;
    default:
        snprintf(msg, len, "update: unknown state %lu", (unsigned long)r->state);
        return ERR_BLK_PARSE_FAIL;
    }
    if (err < 0) {
        return err;
    }

    /* the image the V5F would run */
    if (r->v5f_length == 0) {
        if (strcmp(msg, "update: nothing to do") == 0) {
            snprintf(msg, len, "update: no working V5F image installed");
        }
        return UPD_RECOVERY;
    }
    uint8_t mac[32];
    err = image_mac(io, r, UPD_V5F_ADDR, r->v5f_version, r->v5f_length, mac);
    if (err) {
        return err;
    }
    if (crypto_verify32(mac, r->v5f_mac) == 0) {
        return UPD_START;
    }
    if (r->state == UPD_IDLE && r->backup_length) {     /* changed: restore the backup */
        r->state = UPD_ROLLING_BACK;
        r->progress = 0;
        err = record_store(io, r);
        if (!err) {
            err = roll_back(io, r, msg, len);
        }
        if (err) {
            return err;
        }
        err = image_mac(io, r, UPD_V5F_ADDR, r->v5f_version, r->v5f_length, mac);
        if (!err && crypto_verify32(mac, r->v5f_mac) == 0 && r->result == UPD_R_ROLLED_BACK) {
            return UPD_START;
        }
    }
    r->result = UPD_R_BROKEN;
    r->v5f_length = 0;                  /* so it stays in recovery until a package */
    snprintf(msg, len, "update: the V5F image fails its check");
    err = record_store(io, r);
    return err ? err : UPD_RECOVERY;
}

int upd_confirm(const upd_io *io) {
    upd_record r;
    int err = upd_record_load(io, &r);
    if (err) {
        return err < 0 ? err : 0;
    }
    if (r.state != UPD_TRIAL) {
        return 0;
    }
    r.state = UPD_IDLE;
    r.progress = 0;
    r.result = UPD_R_INSTALLED;
    if (r.pkg_min > r.min_v5f) {
        r.min_v5f = r.pkg_min;
    }
    err = record_store(io, &r);
    crypto_wipe(&r, sizeof r);
    return err;
}

int upd_adopt(const upd_io *io, char *msg, int len) {
    upd_record r;
    int err = upd_record_load(io, &r);
    if (err) {
        snprintf(msg, len, "update: no record to adopt into");
        return err < 0 ? err : ERR_BLK_PARSE_FAIL;
    }
    if (r.state != UPD_IDLE) {
        snprintf(msg, len, "update: an update is under way; not adopted");
        crypto_wipe(&r, sizeof r);
        return ERR_BLK_PARSE_FAIL;
    }
    r.v5f_version = 0;
    r.v5f_length = UPD_V5F_SLOT;
    err = image_mac(io, &r, UPD_V5F_ADDR, 0, UPD_V5F_SLOT, r.v5f_mac);
    if (!err) {
        r.result = UPD_R_NONE;
        err = record_store(io, &r);
    }
    crypto_wipe(&r, sizeof r);
    snprintf(msg, len, err ? "update: the adopt failed (%d)"
                           : "update: the V5F image in flash adopted as version 0", err);
    return err;
}
