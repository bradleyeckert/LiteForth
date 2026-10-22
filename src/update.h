#ifndef UPDATE_H
#define UPDATE_H

/*
 * Installing firmware updates: stage 1's logic (doc/admin/firmware-updates.md
 * sections 3, 4 and 6), in plain C. It reaches the board's flash and the
 * card's two files only through the callbacks in upd_io, so the desktop
 * tests it (unit_tests/update) with simulated flash and power cuts, and
 * the V3F gives it the real ones (V3F/User/stage1.c).
 *
 * So far: the V5F image (packages with component SEALED_V5F), and the V5F
 * image with LiteForth's flash pages after it (SEALED_V5F_APP: the slot,
 * padded, then the pages, one region in flash). A new stage 1 into its
 * other slot comes later.
 *
 * Every step records its progress in the security sector before the
 * next one starts, so a reset at any point resumes or rolls back at the
 * next upd_boot, and never leaves an image that upd_boot starts but
 * didn't check.
 */

#include <stdint.h>
#include "sealed.h"

/* The layout (doc/admin/firmware-updates.md section 2): addresses in the code
   flash, from 0 */
#ifndef UPD_SECTOR
#define UPD_SECTOR          8192u       /* erase unit */
#endif
#ifndef UPD_RECORD_A
#define UPD_RECORD_A        0x06000u    /* security sector, copy A */
#endif
#ifndef UPD_RECORD_B
#define UPD_RECORD_B        0x08000u    /* copy B */
#endif
#ifndef UPD_V5F_ADDR
#define UPD_V5F_ADDR        0x30000u    /* the V5F image */
#endif
#ifndef UPD_V5F_SLOT
#define UPD_V5F_SLOT        (160u * 1024u)
#endif
#ifndef UPD_PAGES_LEN
#define UPD_PAGES_LEN       (256u * 1024u)  /* LiteForth's flash pages, right after the slot */
#endif
#ifndef UPD_HEADER
#define UPD_HEADER          0x0A000u    /* the installed image's package block 0, one sector */
#endif
#define UPD_APP_SPAN        (UPD_V5F_SLOT + UPD_PAGES_LEN)  /* a SEALED_V5F_APP image */
#ifndef UPD_TRIAL_BOOTS
#define UPD_TRIAL_BOOTS     3u          /* boots on trial before a rollback */
#endif

#define UPD_BLOCK           4096u       /* a card block, and a program unit */

/* What stage 1 reaches. Every function returns 0 or an error code. */
typedef struct {
    int (*flash_read)(uint32_t addr, void *buf, uint32_t len);
    int (*flash_erase)(uint32_t addr);                  /* the UPD_SECTOR at addr */
    int (*flash_write)(uint32_t addr, const void *buf, uint32_t len);
    int (*stage_read)(uint32_t blk, uint8_t *buf);      /* lfstage.bin, 4 KB blocks */
    uint32_t stage_blocks;
    int (*backup_read)(uint32_t blk, uint8_t *buf);     /* lfbackup.bin */
    int (*backup_write)(uint32_t blk, const uint8_t *buf);
    uint32_t backup_blocks;
    int (*random)(uint8_t *out, int len);               /* the RNG, checked */
    uint32_t hw_rev;                                    /* this board's */
    int adopt_blank;    /* development (SECURITY_LEVEL 0 and 1): a blank
                           record takes the V5F image already in flash (all
                           of its slot) as installed, and so does a record
                           whose image fails its check (the debugger
                           reprogrammed it): adopted as version 0, ending
                           any trial, instead of rolled back or sent to
                           recovery, so developers can re-flash at will */
    int check_pages;    /* the flash pages are part of the installed image:
                           every installed image is the whole UPD_APP_SPAN
                           (backed up, restored and MACed), and the boot
                           check covers the pages (SECURITY_LEVEL 2, where
                           the V5F can't write them). Else the MAC covers
                           the V5F slot only, so a developer can still
                           change the pages after an app package */
    int check_signature;/* the boot check also needs the release key's
                           signature (SECURITY_LEVEL 2, with check_pages):
                           the installed package's block 0 is kept at
                           UPD_HEADER (and in the backup), and every boot
                           checks it with the root key and the image against
                           its hash, so a changed image fails even if the
                           token, and so the MAC key, leaked. Only app
                           packages (SEALED_V5F_APP) install, since the
                           signature must cover the pages too */
} upd_io;

/* The state of an update, in the record */
enum {
    UPD_IDLE = 0,       /* nothing under way */
    UPD_BACKING_UP,     /* package checked; copying the old image to the card */
    UPD_INSTALLING,     /* backup made; programming, `progress` blocks done */
    UPD_TRIAL,          /* installed; `progress` boots without a confirm */
    UPD_ROLLING_BACK,   /* restoring the backup, `progress` blocks done */
};

/* The outcome of the last update, in the record, for the terminal */
enum {
    UPD_R_NONE = 0,
    UPD_R_INSTALLED,    /* confirmed */
    UPD_R_REFUSED,      /* the package failed a check; nothing changed */
    UPD_R_NO_BACKUP,    /* lfbackup.bin missing or too small; nothing changed */
    UPD_R_ROLLED_BACK,  /* the new image wasn't confirmed; the old one is back */
    UPD_R_BROKEN,       /* no working image (one that failed its check or its
                           trial) and no good backup: v5f_length is 0, so
                           upd_boot says UPD_RECOVERY until a package installs */
};

/* The security sector's record. Kept twice (UPD_RECORD_A and _B): the
   valid copy with the higher seq wins, and a store goes to the other one,
   so a reset during a store leaves the previous record. Its hash only
   catches a torn write: the token is in the record, so it can't be a
   secret MAC key for the record itself. What keeps the record safe is
   that only the boot stages can write the sector (section 8). */
typedef struct {
    uint32_t magic;
    uint32_t format;
    uint32_t seq;
    uint8_t token[32];          /* the device token (section 4) */
    uint32_t v5f_version;       /* the installed V5F image */
    uint32_t v5f_length;
    uint8_t v5f_mac[32];        /* its MAC under K_img */
    uint32_t min_v5f;           /* the lowest V5F version it takes */
    uint32_t min_serial;        /* the lowest release key serial */
    uint32_t state;             /* UPD_IDLE ... */
    uint32_t progress;
    uint32_t pkg_version;       /* the update under way */
    uint32_t pkg_length;
    uint32_t pkg_min;
    uint8_t pkg_hash[32];
    uint32_t backup_version;    /* what lfbackup.bin holds, if backup_length */
    uint32_t backup_length;
    uint8_t backup_mac[32];
    uint32_t result;            /* UPD_R_ ... */
    uint8_t hash[32];           /* of everything above */
} upd_record;

/* What upd_boot says to do */
enum {
    UPD_START = 0,      /* start the V5F: its image checks out */
    UPD_RECOVERY = 1,   /* don't: wait for a package over USB */
};

/**
 * Stage 1 at every boot: reads the record (making the token and a new
 * record if both copies are blank), carries on with any update under way
 * or starts one if package_waiting says lfstage.bin holds a package,
 * then checks the V5F image against its MAC (and with check_signature
 * against the signed block 0 kept at UPD_HEADER). keys holds the root key,
 * the product, this board's UID and X25519 private key (min_revision and
 * min_serial are taken from the record). rec receives the record as it
 * ends up; msg one line saying what happened.
 * Returns UPD_START or UPD_RECOVERY, or a negative error code if the
 * flash couldn't be read or written (then neither: try again).
 * UPD_RECOVERY means no working image: none installed, one that fails its
 * MAC, or one that wasn't confirmed, each with no backup to go back to.
 * Then a package (package_waiting) installs without a backup, since
 * there's nothing worth keeping; a rollback after it goes to the backup on
 * the card, if there's still a good one.
 */
int upd_boot(const upd_io *io, const sealed_board *keys, int package_waiting,
             upd_record *rec, char *msg, int len);

/**
 * Development only (stage 1 offers it in recovery when built for
 * development): takes the V5F image in flash, its whole slot, as installed,
 * version 0, as a blank record does with upd_io.adopt_blank. For a board
 * whose V5F was reprogrammed by the debugger, so it fails its MAC. Refused
 * while an update is under way. Returns 0 or an error code, msg saying
 * what happened.
 */
int upd_adopt(const upd_io *io, char *msg, int len);

/**
 * Marks the installed V5F image as not working (v5f_length 0, result
 * UPD_R_BROKEN), as a failed trial with no backup does, so the next
 * package installs without backing it up and upd_boot says UPD_RECOVERY
 * until one does. For stage 1's recovery when the V5F got stuck, just
 * before a package from the terminal. Leaves an update under way alone.
 * Returns 0 or an error code, msg saying what happened.
 */
int upd_mark_broken(const upd_io *io, char *msg, int len);

/**
 * The V5F says the new image works: ends its trial, raising the minimum
 * version if the package asked to. Does nothing unless an image is on
 * trial. Returns 0 or an error code.
 */
int upd_confirm(const upd_io *io);

/**
 * The board's X25519 private key at SECURITY_LEVEL 2, one of the device
 * token's keys (section 4): BLAKE2s(key=token, person "LFboard"). So it's
 * made with the record (from the RNG), lives under the boot stages'
 * protection with it, and changes only if the record is erased.
 */
void upd_board_key(const upd_record *r, uint8_t key[32]);

/**
 * Reads the record: the valid copy with the higher seq.
 * Returns 0, or 1 if neither copy is valid (blank), or an error code.
 */
int upd_record_load(const upd_io *io, upd_record *rec);

#endif /* UPDATE_H */
