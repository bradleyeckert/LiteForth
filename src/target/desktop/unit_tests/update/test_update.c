/* Tests src/update.c, stage 1's install logic (doc/firmware-updates.md),
 * with the board's flash, lfstage.bin and lfbackup.bin simulated in RAM,
 * and packages made by gen.py with scripts/sealblocks.py.
 *
 * Power cuts: every erase and write (flash or card) counts as one step,
 * and a cut at step k stops the run there, leaving that step half done (a
 * torn write, or half a sector erased). Then the board "resets": upd_boot
 * runs again on what's in flash and on the card. For every k, the board
 * must end up running the old image or the new one, exactly, and never
 * anything else. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update.h"
#include "errcodes.h"

#define FLASH_SIZE      (UPD_V5F_ADDR + UPD_V5F_SLOT)
#define STAGE_BLOCKS    64
#define BACKUP_BLOCKS   64
#define CUT             (-1000)         /* upd_boot stopped by a power cut */

static uint8_t flash[FLASH_SIZE];
static uint8_t stage[STAGE_BLOCKS * UPD_BLOCK];
static uint8_t backup[BACKUP_BLOCKS * UPD_BLOCK];
static uint32_t backup_blocks = BACKUP_BLOCKS;

static long steps;                      /* erases and writes so far */
static long cut_at = -1;                /* stop at this step, or never */
static jmp_buf cut_env;
static int tamper_block = -1;           /* flip a byte of this stage block */
static int tamper_reads;                /* on this read of it (1 = the first) */
static int reads_of_tampered;

static uint8_t *old_img, *new_img, *v3_img;
static long old_len, new_len, v3_len;
static sealed_board keys;
static int failures, checks;

static void fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    failures++;
}

#define CHECK(cond, ...) do { checks++; if (!(cond)) fail(__VA_ARGS__); } while (0)

/* ------------------------------------------------------------------------
 * The callbacks
 */
static void step(void) {
    if (steps++ == cut_at) {
        longjmp(cut_env, 1);
    }
}

static int f_read(uint32_t addr, void *buf, uint32_t len) {
    if (addr + len > FLASH_SIZE) return ERR_BLK_BOUNDS;
    memcpy(buf, flash + addr, len);
    return 0;
}

static int f_erase(uint32_t addr) {
    if (addr % UPD_SECTOR || addr + UPD_SECTOR > FLASH_SIZE) return ERR_BLK_BOUNDS;
    if (steps == cut_at) {              /* half erased */
        memset(flash + addr, 0xFF, UPD_SECTOR / 2);
    }
    step();
    memset(flash + addr, 0xFF, UPD_SECTOR);
    return 0;
}

static int f_write(uint32_t addr, const void *buf, uint32_t len) {
    if (addr + len > FLASH_SIZE) return ERR_BLK_BOUNDS;
    const uint8_t *b = buf;
    if (steps == cut_at) {              /* torn: the first half only */
        for (uint32_t i = 0; i < len / 2; i++) flash[addr + i] &= b[i];
    }
    step();
    for (uint32_t i = 0; i < len; i++) {
        if ((flash[addr + i] & b[i]) != b[i]) {
            fail("flash write at %#x over unerased bytes", addr + i);
            return ERR_FLASH_WRITE_SECTOR;
        }
        flash[addr + i] = b[i];
    }
    return 0;
}

static int s_read(uint32_t blk, uint8_t *buf) {
    if (blk >= STAGE_BLOCKS) return ERR_BLK_BOUNDS;
    memcpy(buf, stage + blk * UPD_BLOCK, UPD_BLOCK);
    if ((int)blk == tamper_block && ++reads_of_tampered == tamper_reads) {
        buf[100] ^= 1;
    }
    return 0;
}

static int b_read(uint32_t blk, uint8_t *buf) {
    if (blk >= backup_blocks) return ERR_BLK_BOUNDS;
    memcpy(buf, backup + blk * UPD_BLOCK, UPD_BLOCK);
    return 0;
}

static int b_write(uint32_t blk, const uint8_t *buf) {
    if (blk >= backup_blocks) return ERR_BLK_BOUNDS;
    if (steps == cut_at) {
        memcpy(backup + blk * UPD_BLOCK, buf, UPD_BLOCK / 2);
    }
    step();
    memcpy(backup + blk * UPD_BLOCK, buf, UPD_BLOCK);
    return 0;
}

static uint32_t rng_state = 1;
static int rng(uint8_t *out, int len) {
    for (int i = 0; i < len; i++) {
        rng_state = rng_state * 1103515245u + 12345u;
        out[i] = (uint8_t)(rng_state >> 16);
    }
    return 0;
}

static upd_io io = {
    f_read, f_erase, f_write, s_read, STAGE_BLOCKS, b_read, b_write, BACKUP_BLOCKS,
    rng, 1, 1,
};

/* ------------------------------------------------------------------------
 * Helpers
 */
static uint8_t *load(const char *name, long *len) {
    char path[256];
    snprintf(path, sizeof path, "work/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("test_update: can't read %s (run gen.py)\n", path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    *len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *p = malloc((size_t)*len + 1);
    if (fread(p, 1, (size_t)*len, f) != (size_t)*len) exit(2);
    fclose(f);
    return p;
}

static void put_package(const char *name) {
    long len;
    uint8_t *p = load(name, &len);
    memset(stage, 0, sizeof stage);
    memcpy(stage, p, (size_t)len);
    free(p);
}

/* A board fresh from the debugger: blank record, old image in flash */
static void fresh(void) {
    memset(flash, 0xFF, sizeof flash);
    memcpy(flash + UPD_V5F_ADDR, old_img, (size_t)old_len);
    memset(backup, 0, sizeof backup);
    backup_blocks = BACKUP_BLOCKS;
    io.backup_blocks = BACKUP_BLOCKS;
    tamper_block = -1;
    cut_at = -1;
}

static upd_record rec;
static char msg[160];

/* One boot; CUT if the power went */
static int boot(int waiting) {
    if (setjmp(cut_env)) {
        cut_at = -1;
        return CUT;
    }
    return upd_boot(&io, &keys, waiting, &rec, msg, sizeof msg);
}

static int is_image(const uint8_t *img, long len) {
    return memcmp(flash + UPD_V5F_ADDR, img, (size_t)len) == 0;
}

/* After the adopt: the slot as installed is all 160 KB, version 0 */
static void adopt(void) {
    fresh();
    int r = boot(0);
    CHECK(r == UPD_START, "first boot: %d (%s)", r, msg);
    CHECK(rec.v5f_length == UPD_V5F_SLOT && rec.v5f_version == 0, "first boot: adopted");
}

/* ------------------------------------------------------------------------
 * The tests
 */
static void test_install_and_confirm(void) {
    adopt();
    put_package("new.lfu");
    int r = boot(1);
    CHECK(r == UPD_START, "install: %d (%s)", r, msg);
    CHECK(strstr(msg, "version 2 installed, on trial") != NULL, "install: %s", msg);
    CHECK(is_image(new_img, new_len), "install: the new image is in flash");
    CHECK(rec.state == UPD_TRIAL && rec.v5f_version == 2 && rec.v5f_length == (uint32_t)new_len,
          "install: on trial as version 2");
    CHECK(rec.backup_length == UPD_V5F_SLOT, "install: the old slot backed up");
    r = boot(1);                        /* the flag still set: ignored on trial */
    CHECK(r == UPD_START && rec.state == UPD_TRIAL && rec.progress == 1,
          "trial boot 1: %d %s", r, msg);
    CHECK(upd_confirm(&io) == 0, "confirm");
    r = boot(0);
    CHECK(r == UPD_START && rec.state == UPD_IDLE && rec.result == UPD_R_INSTALLED
          && rec.min_v5f == 2, "confirmed: minimum 2 (%s)", msg);

    put_package("v1.lfu");              /* below the minimum now */
    r = boot(1);
    CHECK(r == UPD_START && rec.result == UPD_R_REFUSED && strstr(msg, "older revision"),
          "v1 refused below the minimum: %s", msg);
    CHECK(is_image(new_img, new_len), "v1 refused: image unchanged");

    flash[UPD_V5F_ADDR + 5000] ^= 1;    /* changed in flash; the backup is below 2 */
    r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN, "changed image, old backup: %d %s",
          r, msg);
}

static void test_rollback(void) {
    adopt();
    put_package("new0.lfu");
    CHECK(boot(1) == UPD_START && rec.state == UPD_TRIAL, "new0: on trial");
    for (unsigned i = 1; i <= UPD_TRIAL_BOOTS; i++) {
        int r = boot(0);
        CHECK(r == UPD_START && rec.state == UPD_TRIAL && rec.progress == i,
              "trial boot %u: %s", i, msg);
    }
    int r = boot(0);                    /* one too many: roll back */
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && rec.state == UPD_IDLE,
          "rollback: %d %s", r, msg);
    CHECK(is_image(old_img, old_len), "rollback: the old image is back");
    CHECK(rec.v5f_version == 0, "rollback: version 0 again");

    adopt();                            /* a changed image with a good backup */
    put_package("new0.lfu");
    boot(1);
    CHECK(upd_confirm(&io) == 0, "new0 confirmed");
    flash[UPD_V5F_ADDR + 7] ^= 0x10;
    r = boot(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_image(old_img, old_len),
          "changed image restored from the backup: %d %s", r, msg);
}

static void test_refusals(void) {
    static const struct { const char *pkg, *why; } bad[] = {
        { "product.lfu", "another product" },
        { "other.lfu", "isn't one of its recipients" },
        { "stage1.lfu", "not a V5F image" },
        { "hwrev.lfu", "hardware revision 7" },
    };
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        adopt();
        put_package(bad[i].pkg);
        long before = steps;
        int r = boot(1);
        CHECK(r == UPD_START && rec.result == UPD_R_REFUSED && strstr(msg, bad[i].why),
              "%s refused: %d %s", bad[i].pkg, r, msg);
        CHECK(is_image(old_img, old_len) && steps - before == 2,   /* an erase, a write */
              "%s: only the record written (%ld steps)", bad[i].pkg, steps - before);
    }
    adopt();                            /* a damaged block, found by the check */
    put_package("new.lfu");
    stage[2 * UPD_BLOCK + 77] ^= 4;
    int r = boot(1);
    CHECK(r == UPD_START && rec.result == UPD_R_REFUSED && strstr(msg, "block 2"),
          "damaged block refused: %s", msg);
    CHECK(is_image(old_img, old_len), "damaged block: image unchanged");

    adopt();                            /* no room for the backup */
    io.backup_blocks = 10;
    put_package("new.lfu");
    r = boot(1);
    CHECK(r == UPD_START && rec.result == UPD_R_NO_BACKUP && is_image(old_img, old_len),
          "no room for the backup: %s", msg);
    io.backup_blocks = BACKUP_BLOCKS;

    adopt();                            /* lfstage.bin isn't a package */
    memset(stage, 0, sizeof stage);
    r = boot(1);
    CHECK(r == UPD_START && rec.result == UPD_R_REFUSED, "no package: %s", msg);
}

/* A block that changes between the check and its programming */
static void test_changed_while_installing(void) {
    adopt();
    put_package("new.lfu");
    tamper_block = 3;
    tamper_reads = 2;                   /* the check reads it first, then the install */
    reads_of_tampered = 0;
    int r = boot(1);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_image(old_img, old_len),
          "a block changed after the check: rolled back (%d, %s)", r, msg);
    tamper_block = -1;
}

/* A power cut at every step of an install, then of a rollback */
static void test_power_cuts(void) {
    long total;
    adopt();
    put_package("new.lfu");
    long start = steps;
    boot(1);
    total = steps - start;
    CHECK(total > 40, "an install takes %ld steps", total);
    int installed = 0, old = 0;
    for (long k = 0; k < total; k++) {
        adopt();
        put_package("new.lfu");
        cut_at = steps + k;
        int r = boot(1);
        CHECK(r == CUT, "cut %ld: the cut happened", k);
        int boots = 0;
        do {
            r = boot(1);
        } while (r < 0 && ++boots < 3);
        if (r == UPD_START && is_image(new_img, new_len) && rec.v5f_version == 2) {
            installed++;
        } else if (r == UPD_START && is_image(old_img, old_len) && rec.v5f_version == 0) {
            old++;
        } else {
            fail("cut at step %ld of %ld: %d, %s", k, total, r, msg);
        }
    }
    CHECK(installed == total, "every cut resumed the install (%d of %ld; %d left the old)",
          installed, total, old);

    /* rollback, cut at every step */
    adopt();
    put_package("new0.lfu");
    boot(1);
    for (unsigned i = 0; i < UPD_TRIAL_BOOTS; i++) boot(0);
    static uint8_t flash0[FLASH_SIZE], backup0[sizeof backup];
    memcpy(flash0, flash, sizeof flash);
    memcpy(backup0, backup, sizeof backup);
    start = steps;
    boot(0);
    total = steps - start;
    CHECK(rec.result == UPD_R_ROLLED_BACK, "rollback reference run");
    int back = 0;
    for (long k = 0; k < total; k++) {
        memcpy(flash, flash0, sizeof flash);
        memcpy(backup, backup0, sizeof backup);
        cut_at = steps + k;
        int r = boot(0);
        CHECK(r == CUT, "rollback cut %ld", k);
        r = boot(0);
        if (r == UPD_START && is_image(old_img, old_len) && rec.result == UPD_R_ROLLED_BACK) {
            back++;
        } else {
            fail("rollback cut at step %ld of %ld: %d, %s", k, total, r, msg);
        }
    }
    CHECK(back == total, "every cut resumed the rollback (%d of %ld)", back, total);
}

/* The package replaced while an install was cut short. (Cut during the
   backup instead, the update simply starts over, with whatever package is
   there then: that's right, since nothing has been programmed yet.) */
static void test_package_swapped(void) {
    int tried = 0;
    for (long k = 0; k < 200 && !tried; k++) {
        adopt();
        put_package("new.lfu");
        cut_at = steps + k;
        if (boot(1) != CUT) {
            break;
        }
        upd_record now;
        if (upd_record_load(&io, &now) != 0 || now.state != UPD_INSTALLING || now.progress == 0) {
            continue;
        }
        tried = 1;
        put_package("v3.lfu");
        int r = boot(1);
        CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_image(old_img, old_len),
              "package swapped mid-install (cut at step %ld): rolled back (%d, %s)", k, r, msg);
    }
    CHECK(tried, "a cut part way through programming was found");
}

static void test_blank_without_adopt(void) {
    fresh();
    io.adopt_blank = 0;
    int r = boot(0);
    CHECK(r == UPD_RECOVERY, "no adopt, no image: recovery (%s)", msg);
    put_package("new.lfu");
    r = boot(1);
    CHECK(r == UPD_START && is_image(new_img, new_len) && rec.backup_length == 0,
          "first install with nothing to back up: %d %s", r, msg);
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN && rec.v5f_length == 0
          && strstr(msg, "never confirmed"), "no confirm, no backup: recovery (%s)", msg);
    r = boot(0);
    CHECK(r == UPD_RECOVERY && strstr(msg, "no working V5F image"),
          "and it stays in recovery (%s)", msg);
    io.adopt_blank = 1;
}

/* Recovery: no working image, then a package from the USB terminal (here
   just put in lfstage.bin) installs without a backup */
static void test_recovery(void) {
    adopt();                            /* the backup lost during a trial */
    put_package("new0.lfu");
    boot(1);
    memset(backup, 0, sizeof backup);
    int r = 0;
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN && strstr(msg, "can't roll back"),
          "trial failed, backup lost: recovery (%d, %s)", r, msg);
    r = boot(0);
    CHECK(r == UPD_RECOVERY, "still in recovery at the next boot (%s)", msg);
    io.backup_blocks = 0;               /* and no lfbackup.bin at all */
    put_package("v3.lfu");
    r = boot(1);
    CHECK(r == UPD_START && is_image(v3_img, v3_len) && rec.state == UPD_TRIAL
          && rec.v5f_version == 3, "recovery install without a backup: %d %s", r, msg);
    CHECK(upd_confirm(&io) == 0 && boot(0) == UPD_START && rec.result == UPD_R_INSTALLED,
          "recovered: confirmed (%s)", msg);
    io.backup_blocks = BACKUP_BLOCKS;

    adopt();                            /* a changed image isn't backed up */
    put_package("new0.lfu");
    boot(1);
    upd_confirm(&io);                   /* the card: the old slot, version 0 */
    static uint8_t backup0[sizeof backup];
    memcpy(backup0, backup, sizeof backup);
    flash[UPD_V5F_ADDR + 99] ^= 2;
    put_package("v3.lfu");
    r = boot(1);
    CHECK(r == UPD_START && is_image(v3_img, v3_len) && rec.backup_version == 0
          && rec.backup_length == UPD_V5F_SLOT && memcmp(backup, backup0, sizeof backup) == 0,
          "changed image: v3 installed, the old backup kept (%d, %s)", r, msg);
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_image(old_img, old_len),
          "and v3's failed trial goes back to that backup (%s)", msg);

    adopt();                            /* the debugger's image, adopted */
    flash[UPD_V5F_ADDR + 3] ^= 1;
    r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN, "reprogrammed: recovery (%s)", msg);
    CHECK(upd_adopt(&io, msg, sizeof msg) == 0 && strstr(msg, "adopted"), "adopt: %s", msg);
    r = boot(0);
    CHECK(r == UPD_START && rec.v5f_version == 0 && rec.v5f_length == UPD_V5F_SLOT,
          "adopted: it starts (%d, %s)", r, msg);
    put_package("new0.lfu");
    boot(1);
    CHECK(upd_adopt(&io, msg, sizeof msg) != 0 && strstr(msg, "under way"),
          "no adopt during a trial: %s", msg);
    memset(flash + UPD_RECORD_A, 0xFF, UPD_SECTOR);
    memset(flash + UPD_RECORD_B, 0xFF, UPD_SECTOR);
    CHECK(upd_adopt(&io, msg, sizeof msg) != 0, "no adopt without a record: %s", msg);
}

static void test_record_copies(void) {
    adopt();
    uint32_t seq = rec.seq;
    put_package("new.lfu");
    boot(1);
    CHECK(rec.seq > seq + 5, "records stored as it went (%u -> %u)", seq, rec.seq);
    upd_record a;
    memcpy(&a, flash + ((rec.seq & 1) ? UPD_RECORD_B : UPD_RECORD_A), sizeof a);
    CHECK(a.seq == rec.seq, "the newest copy is where its seq says");
    memset(flash + ((rec.seq & 1) ? UPD_RECORD_B : UPD_RECORD_A), 0xFF, UPD_SECTOR);
    upd_record b;
    CHECK(upd_record_load(&io, &b) == 0 && b.seq == rec.seq - 1,
          "one copy lost: the other one is read");
    memset(flash + UPD_RECORD_A, 0xFF, UPD_SECTOR);
    memset(flash + UPD_RECORD_B, 0xFF, UPD_SECTOR);
    CHECK(upd_record_load(&io, &b) == 1, "both lost: blank");
}

int main(void) {
    old_img = load("old.bin", &old_len);
    new_img = load("new.bin", &new_len);
    v3_img = load("v3.bin", &v3_len);
    long n;
    uint8_t *root = load("root.pub.bin", &n), *board = load("board.bin", &n);
    memcpy(keys.root, root, 32);
    memcpy(keys.uid, board, 8);
    memcpy(keys.private_key, board + 8, 32);
    keys.product = 0x4C46;
    free(root);
    free(board);

    test_install_and_confirm();
    test_rollback();
    test_refusals();
    test_changed_while_installing();
    test_package_swapped();
    test_blank_without_adopt();
    test_recovery();
    test_record_copies();
    test_power_cuts();

    if (failures) {
        printf("%d of %d update checks failed\n", failures, checks);
        return 1;
    }
    printf("update tests passed (%d checks)\n", checks);
    return 0;
}
