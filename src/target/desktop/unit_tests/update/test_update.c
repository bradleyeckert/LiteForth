/* Tests src/update.c, stage 1's install logic (doc/admin/firmware-updates.md),
 * with the board's flash, lfstage.bin and lfbackup.bin simulated in RAM,
 * and packages made by gen.py with scripts/admin/sealblocks.py.
 *
 * Power cuts: every erase and write (flash or card) counts as one step,
 * and a cut at step k stops the run there, leaving that step half done (a
 * torn write, or half a sector erased). Then the board "resets": upd_boot
 * runs again on what's in flash and on the card. For every k, the board
 * must end up running the old image or the new one, exactly, and never
 * anything else. */
#include <setjmp.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "update.h"
#include "errcodes.h"
#include "monocypher.h"
#include "blake2s.h"

#define FLASH_SIZE      (UPD_V5F_ADDR + UPD_APP_SPAN)   /* the slot, then the flash pages */
#define STAGE_BLOCKS    128             /* an app package is about 105 */
#define BACKUP_BLOCKS   128
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

/* The board's key at level 2 (upd_board_key): BLAKE2s(key=token, person
   "LFboard"), the same each time, and another for another token */
static void test_board_key(void) {
    upd_record r;
    uint8_t k1[32], k2[32], want[32], p[8] = { 'L', 'F', 'b', 'o', 'a', 'r', 'd', 0 };
    blake2s_state S;
    memset(&r, 0, sizeof r);
    for (int i = 0; i < 32; i++) {
        r.token[i] = (uint8_t)(i * 7 + 1);
    }
    upd_board_key(&r, k1);
    upd_board_key(&r, k2);
    b2s_init(&S, 32, r.token, 32, NULL, p);
    b2s_final(&S, want);
    checks += 2;
    if (memcmp(k1, k2, 32) != 0 || memcmp(k1, want, 32) != 0) {
        failures++;
        printf("FAIL: upd_board_key isn't BLAKE2s(key=token, LFboard)\n");
    }
    r.token[0] ^= 1;
    upd_board_key(&r, k2);
    if (memcmp(k1, k2, 32) == 0) {
        failures++;
        printf("FAIL: upd_board_key gave the same key for another token\n");
    }
}

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
    rng, 1, 1, 0, 0,
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

/* One boot as a production stage 1 (SECURITY_LEVEL 2) has it: no
   adopt_blank, so a changed image is rolled back or sent to recovery, not
   adopted */
static int boot_production(int waiting) {
    int adopt = io.adopt_blank;
    io.adopt_blank = 0;
    int r = boot(waiting);
    io.adopt_blank = adopt;
    return r;
}

static int is_image(const uint8_t *img, long len) {
    return memcmp(flash + UPD_V5F_ADDR, img, (size_t)len) == 0;
}

/* After the adopt: the slot as installed is all 160 KB, version 0 */
static void adopt(void) {
    fresh();
    int r = boot(0);
    CHECK(r == UPD_START, "first boot: %d (%s)", r, msg);
    CHECK(rec.v5f_length == (io.check_pages ? UPD_APP_SPAN : UPD_V5F_SLOT) && rec.v5f_version == 0,
          "first boot: adopted");
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
    r = boot_production(0);
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
    r = boot_production(0);
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

/* Development (adopt_blank, SECURITY_LEVEL 0 and 1): an image the
   debugger changed is adopted as version 0, not restored or sent to
   recovery, so developers can re-flash at will */
static void test_debugger_adopt(void) {
    adopt();
    put_package("new.lfu");
    int r = boot(1);
    CHECK(r == UPD_START && upd_confirm(&io) == 0, "debugger: v2 installed and confirmed");
    uint32_t backup_len = rec.backup_length;
    flash[UPD_V5F_ADDR + 1234] ^= 0x55;  /* the debugger writes a new V5F */
    uint8_t mark = flash[UPD_V5F_ADDR + 1234];
    r = boot(0);
    CHECK(r == UPD_START && strstr(msg, "adopted as version 0") != NULL,
          "debugger: a changed image adopted: %d %s", r, msg);
    CHECK(rec.v5f_version == 0 && rec.state == UPD_IDLE && flash[UPD_V5F_ADDR + 1234] == mark,
          "debugger: version 0, the debugger's image kept, not restored");
    CHECK(rec.backup_length == backup_len, "debugger: the backup on the card kept");
    r = boot(0);
    CHECK(r == UPD_START && strcmp(msg, "update: nothing to do") == 0,
          "debugger: the next boot has nothing to do: %s", msg);

    adopt();                            /* changed during a trial: the trial ends */
    put_package("new0.lfu");
    r = boot(1);
    CHECK(r == UPD_START && rec.state == UPD_TRIAL, "debugger: on trial");
    flash[UPD_V5F_ADDR + 77] ^= 1;
    r = boot(0);
    CHECK(r == UPD_START && rec.state == UPD_IDLE && rec.v5f_version == 0
          && strstr(msg, "adopted"), "debugger: adopted during a trial: %s", msg);
}

/* ------------------------------------------------------------------------
 * App packages: the V5F slot and LiteForth's flash pages
 */
static uint8_t *app_img;                /* what app.lfu puts in flash */
static long app_len;
#define PAGE_BYTE   (UPD_V5F_ADDR + UPD_V5F_SLOT + 70000)   /* somewhere in page 1 */

static int is_region(const uint8_t *img) {
    return memcmp(flash + UPD_V5F_ADDR, img, UPD_APP_SPAN) == 0;
}

/* Development (no check_pages): the pages install, but the MAC covers the
   slot only, so a developer may change them afterwards */
static void test_app_development(void) {
    io.check_pages = 0;
    adopt();
    put_package("app.lfu");
    int r = boot(1);
    CHECK(r == UPD_START && strstr(msg, "version 4 installed"), "app: %d (%s)", r, msg);
    CHECK(app_len == UPD_APP_SPAN && is_region(app_img), "app: the slot and the pages in flash");
    CHECK(rec.v5f_length == UPD_APP_SPAN && rec.backup_length == UPD_V5F_SLOT,
          "app: installed as the whole span, the adopted slot backed up");
    CHECK(upd_confirm(&io) == 0, "app: confirm");
    flash[PAGE_BYTE] ^= 0x40;           /* close-flash after the install */
    r = boot(0);
    CHECK(r == UPD_START && rec.result == UPD_R_INSTALLED, "app: a changed page is fine: %s", msg);
    flash[UPD_V5F_ADDR + 99] ^= 1;      /* the V5F image itself still counts */
    r = boot_production(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && rec.v5f_version == 0,
          "app: a changed slot rolls back: %d %s", r, msg);

    adopt();
    put_package("appshort.lfu");
    r = boot(1);
    CHECK(r == UPD_START && rec.result == UPD_R_REFUSED && strstr(msg, "every flash page"),
          "app: too few pages refused: %s", msg);
}

/* SECURITY_LEVEL 2 (check_pages): every installed image is the whole
   span, its MAC covers the pages, and a changed page is caught */
static void test_app_locked(void) {
    static uint8_t before[UPD_APP_SPAN];
    io.check_pages = 1;
    adopt();
    CHECK(rec.v5f_length == UPD_APP_SPAN, "locked: adopted as the whole span");
    memcpy(before, flash + UPD_V5F_ADDR, UPD_APP_SPAN);
    put_package("app.lfu");
    int r = boot(1);
    CHECK(r == UPD_START && is_region(app_img), "locked: app installed: %d %s", r, msg);
    CHECK(rec.v5f_length == UPD_APP_SPAN && rec.backup_length == UPD_APP_SPAN,
          "locked: the old span backed up, pages and all");
    CHECK(upd_confirm(&io) == 0, "locked: confirm");
    flash[PAGE_BYTE] ^= 0x40;           /* a page changed behind stage 1's back */
    r = boot_production(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_region(before),
          "locked: a changed page is caught and the old span restored: %d %s", r, msg);

    adopt();                            /* a V5F-only package: still the whole span */
    put_package("new0.lfu");
    r = boot(1);
    CHECK(r == UPD_START && rec.v5f_length == UPD_APP_SPAN, "locked: V5F-only: %d %s", r, msg);
    CHECK(upd_confirm(&io) == 0, "locked: confirm V5F-only");
    flash[PAGE_BYTE] ^= 0x40;
    r = boot_production(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK, "locked: pages checked after a "
          "V5F-only package too: %s", msg);

    /* A record from before check_pages (its MAC over the slot only) isn't
       trusted, and a slot-only backup isn't restored */
    io.check_pages = 0;
    adopt();
    io.check_pages = 1;
    r = boot_production(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN,
          "locked: a slot-only record goes to recovery: %d %s", r, msg);

    /* Power cuts through an app install */
    io.check_pages = 1;
    adopt();
    put_package("app.lfu");
    long start = steps;
    boot(1);
    long total = steps - start, installed = 0;
    for (long k = 0; k < total; k++) {
        adopt();
        put_package("app.lfu");
        cut_at = steps + k;
        r = boot(1);
        int boots = 0;
        do {
            r = boot(1);
        } while (r < 0 && ++boots < 3);
        if (r == UPD_START && is_region(app_img) && rec.v5f_version == 4) {
            installed++;
        } else {
            fail("app cut at step %ld of %ld: %d, %s", k, total, r, msg);
        }
    }
    CHECK(installed == total, "locked: every cut resumed the app install (%ld of %ld)",
          installed, total);
    io.check_pages = 0;
}

/* ------------------------------------------------------------------------
 * SECURITY_LEVEL 2 with check_signature: the boot check needs the release
 * key's signature, kept with the image at UPD_HEADER
 */
static uint8_t *app5_img;               /* what app5.lfu puts in flash */
static long app5_len;

static void b2s_person(blake2s_state *S, const uint8_t *key, int klen, const char *person) {
    uint8_t p[8] = {0};
    memcpy(p, person, strlen(person));
    b2s_init(S, 32, key, klen, NULL, p);
}

/* What someone who has read the token can do: a record whose MAC matches
   the image in flash now, as version `version`, stored as the newest copy.
   (update.c's image_mac and record_hash, repeated here.) */
static void forge_record(uint32_t version) {
    upd_record r;
    blake2s_state S;
    uint8_t k[32], vl[8];
    CHECK(upd_record_load(&io, &r) == 0, "forge: a record to start from");
    b2s_person(&S, r.token, 32, "LFimage");
    b2s_final(&S, k);
    b2s_person(&S, k, 32, "LFimgmac");
    for (int i = 0; i < 4; i++) {
        vl[i] = (uint8_t)(version >> (8 * i));
        vl[4 + i] = (uint8_t)(UPD_APP_SPAN >> (8 * i));
    }
    b2s_puts(&S, vl, 8);
    b2s_puts(&S, flash + UPD_V5F_ADDR, UPD_APP_SPAN);
    b2s_final(&S, r.v5f_mac);
    r.v5f_version = version;
    r.v5f_length = UPD_APP_SPAN;
    r.state = UPD_IDLE;
    r.progress = 0;
    r.seq++;
    b2s_person(&S, NULL, 0, "LFrecord");
    b2s_puts(&S, (const uint8_t *)&r, (int)offsetof(upd_record, hash));
    b2s_final(&S, r.hash);
    memcpy(flash + ((r.seq & 1) ? UPD_RECORD_B : UPD_RECORD_A), &r, sizeof r);
}

static int header_is(const char *package) {
    long len;
    uint8_t *p = load(package, &len);
    int same = memcmp(flash + UPD_HEADER, p, UPD_BLOCK) == 0;
    free(p);
    return same;
}

/* A production board: no adopt, the pages and the signature checked;
   app.lfu installed from recovery (a blank record) and confirmed */
static void signed_board(void) {
    io.check_pages = 1;
    io.check_signature = 1;
    io.adopt_blank = 0;
    fresh();
    put_package("app.lfu");
    int r = boot(1);
    CHECK(r == UPD_START && is_region(app_img) && rec.state == UPD_TRIAL && header_is("app.lfu"),
          "signed: app installed from recovery, its block 0 kept: %d %s", r, msg);
    CHECK(upd_confirm(&io) == 0 && boot(0) == UPD_START && rec.result == UPD_R_INSTALLED,
          "signed: confirmed: %s", msg);
}

static void test_signed(void) {
    static uint8_t flash0[FLASH_SIZE], backup0[sizeof backup];
    signed_board();

    put_package("new0.lfu");            /* the pages wouldn't be signed */
    int r = boot(1);
    CHECK(r == UPD_START && rec.result == UPD_R_REFUSED && strstr(msg, "only app packages")
          && is_region(app_img), "signed: a V5F-only package refused: %s", msg);

    /* the token leaked: a changed image with a matching MAC still fails */
    signed_board();
    flash[PAGE_BYTE] ^= 0x40;
    forge_record(4);
    io.check_signature = 0;
    r = boot(0);
    CHECK(r == UPD_START, "unsigned check: the forged MAC passes (%s)", msg);
    io.check_signature = 1;
    r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN,
          "signed: a changed page with a forged MAC goes to recovery: %d %s", r, msg);
    signed_board();
    flash[UPD_V5F_ADDR + 100] ^= 1;     /* the V5F's code */
    forge_record(4);
    CHECK(boot(0) == UPD_RECOVERY, "signed: changed code with a forged MAC: %s", msg);

    /* the kept block 0 changed, or another package's */
    signed_board();
    flash[UPD_HEADER + 200] ^= 1;
    CHECK(boot(0) == UPD_RECOVERY, "signed: a changed block 0: %s", msg);
    signed_board();
    long len;
    uint8_t *b5 = load("app5.lfu", &len);
    memcpy(flash + UPD_HEADER, b5, UPD_BLOCK);
    CHECK(boot(0) == UPD_RECOVERY, "signed: another package's block 0: %s", msg);
    signed_board();                     /* that package's image and block 0, an old version */
    memcpy(flash + UPD_V5F_ADDR, app5_img, UPD_APP_SPAN);
    memcpy(flash + UPD_HEADER, b5, UPD_BLOCK);
    forge_record(5);
    CHECK(boot(0) == UPD_START, "signed: another genuine release starts (%s)", msg);
    free(b5);

    /* an update backs up the block 0 with the image; a failed trial restores both */
    signed_board();
    put_package("app5.lfu");
    r = boot(1);
    CHECK(r == UPD_START && is_region(app5_img) && header_is("app5.lfu")
          && rec.v5f_version == 5, "signed: app5 installed: %d %s", r, msg);
    memcpy(flash0, flash, sizeof flash);
    memcpy(backup0, backup, sizeof backup);
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_region(app_img)
          && header_is("app.lfu"), "signed: rolled back with its block 0: %d %s", r, msg);
    /* a changed image restored from the backup, block 0 and all */
    flash[PAGE_BYTE] ^= 0x40;
    r = boot(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_region(app_img)
          && header_is("app.lfu"), "signed: a changed image restored: %d %s", r, msg);

    /* a backup made without the signature check isn't trusted */
    io.check_signature = 0;
    signed_board();
    io.check_signature = 0;
    put_package("app5.lfu");
    boot(1);
    io.check_signature = 1;
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN,
          "signed: a backup without its block 0 isn't restored: %d %s", r, msg);

    /* power cuts through an install over a signed image */
    signed_board();
    put_package("app5.lfu");
    long start = steps;
    boot(1);
    long total = steps - start, done = 0;
    for (long k = 0; k < total; k++) {
        signed_board();
        put_package("app5.lfu");
        cut_at = steps + k;
        boot(1);
        int boots = 0;
        do {
            r = boot(1);
        } while (r < 0 && ++boots < 3);
        if (r == UPD_START && is_region(app5_img) && header_is("app5.lfu")
            && rec.v5f_version == 5) {
            done++;
        } else {
            fail("signed install cut at step %ld of %ld: %d, %s", k, total, r, msg);
        }
    }
    CHECK(done == total, "signed: every cut resumed the install (%ld of %ld)", done, total);

    /* and through the rollback */
    memcpy(flash, flash0, sizeof flash);
    memcpy(backup, backup0, sizeof backup);
    for (unsigned i = 0; i < UPD_TRIAL_BOOTS; i++) boot(0);
    memcpy(flash0, flash, sizeof flash);
    start = steps;
    boot(0);
    total = steps - start;
    CHECK(rec.result == UPD_R_ROLLED_BACK, "signed rollback reference run: %s", msg);
    done = 0;
    for (long k = 0; k < total; k++) {
        memcpy(flash, flash0, sizeof flash);
        memcpy(backup, backup0, sizeof backup);
        cut_at = steps + k;
        boot(0);
        r = boot(0);
        if (r == UPD_START && is_region(app_img) && header_is("app.lfu")
            && rec.result == UPD_R_ROLLED_BACK) {
            done++;
        } else {
            fail("signed rollback cut at step %ld of %ld: %d, %s", k, total, r, msg);
        }
    }
    CHECK(done == total, "signed: every cut resumed the rollback (%ld of %ld)", done, total);

    io.check_pages = 0;
    io.check_signature = 0;
    io.adopt_blank = 1;
}

/* The backup on the card: anyone can write any file there (security plan
   section 4: the holder of the device), so it must be neither readable nor
   changeable without the token */
static void test_backup_on_card(void) {
    /* not plaintext: no 64-byte run of the old image anywhere in it */
    adopt();
    put_package("new0.lfu");
    boot(1);
    CHECK(rec.backup_length == UPD_V5F_SLOT, "backup made");
    int plain = 0;
    for (long at = 0; at + 64 <= old_len && !plain; at += 64) {
        for (size_t b = 0; b + 64 <= sizeof backup; b += 16) {
            if (memcmp(backup + b, old_img + at, 64) == 0) {
                plain = 1;
                break;
            }
        }
    }
    CHECK(!plain, "the backup isn't the image in the clear");

    /* one byte changed in it: the failed trial goes to recovery, not to it */
    backup[3 * UPD_BLOCK + 100] ^= 1;
    int r = 0;
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN && strstr(msg, "can't roll back")
          && !is_image(old_img, old_len), "a changed backup isn't restored: %d %s", r, msg);

    /* its header changed (another version claimed): not restored either */
    adopt();
    put_package("new0.lfu");
    boot(1);
    backup[8] ^= 1;                     /* the version in block 0 */
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN,
          "a backup claiming another version isn't restored: %d %s", r, msg);
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
    r = boot_production(0);
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

    /* stuck: marked as not working, then a package without lfbackup.bin */
    adopt();
    put_package("new0.lfu");
    boot(1);
    upd_confirm(&io);                   /* new0 installed; the card: the old slot */
    CHECK(upd_mark_broken(&io, msg, sizeof msg) == 0 && strstr(msg, "marked"),
          "mark broken: %s", msg);
    r = boot(0);
    CHECK(r == UPD_RECOVERY && rec.result == UPD_R_BROKEN, "marked: recovery (%s)", msg);
    io.backup_blocks = 0;
    put_package("v3.lfu");
    r = boot(1);
    CHECK(r == UPD_START && is_image(v3_img, v3_len) && rec.backup_length == UPD_V5F_SLOT,
          "marked: v3 installs without a backup, the old one kept (%d, %s)", r, msg);
    io.backup_blocks = BACKUP_BLOCKS;
    CHECK(upd_mark_broken(&io, msg, sizeof msg) == 0 && strstr(msg, "under way"),
          "no mark during a trial: %s", msg);
    for (unsigned i = 0; i <= UPD_TRIAL_BOOTS; i++) r = boot(0);
    CHECK(r == UPD_START && rec.result == UPD_R_ROLLED_BACK && is_image(old_img, old_len),
          "and v3's failed trial still rolls back to the old backup (%s)", msg);
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
    app_img = load("app.bin", &app_len);
    app5_img = load("app5.bin", &app5_len);
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
    test_backup_on_card();
    test_power_cuts();
    test_debugger_adopt();
    test_app_development();
    test_app_locked();
    test_signed();

    test_board_key();

    if (failures) {
        printf("%d of %d update checks failed\n", failures, checks);
        return 1;
    }
    printf("update tests passed (%d checks)\n", checks);
    return 0;
}
