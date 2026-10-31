/*
 * Stage 1 on the V3F, first step: see stage1.h. The callbacks give
 * src/update.c the code flash (FLASH_ROM_ERASE and FLASH_ROM_WRITE, fenced
 * to the V5F's slot and the two record sectors, so no mistake here can
 * erase the V3F itself), the card's lfstage.bin and lfbackup.bin (WCH's
 * polling SD driver, through src/card.c to find them), and the RNG. It
 * holds the board's private key (keys_get): the V5F gets only the public
 * key, for Forth's `enroll` (serve_enroll).
 *
 * Recovery (stage1.h): the V5F stays off, and stage 1 itself is the other
 * end of the USB rings: it reads what the host sends from the rx ring and
 * writes its replies into the tx ring, as the V5F would, while
 * cdc_bridge_poll moves them to and from USB as usual.
 */
#include <stdio.h>
#include <string.h>
#include "ch32h417.h"
#include "debug.h"
#include "stage1_layout.h"
#include "stage1.h"
#include "../../V5F/User/sdio.h"
#include "cdc_shared.h"
#include "cdc_bridge.h"
#include "../../../../update.h"
#include "../../../../ymrecv.h"
#include "../../../../card.h"
#include "../../../../blake2s.h"
#include "../../../../monocypher.h"
#include "../../../../errcodes.h"
#if __has_include("../../V5F/User/sealed_root.h")
#include "../../V5F/User/sealed_root.h"     /* SEALED_ROOT_KEY, SEALED_PRODUCT */
#include "security.h"                       /* again: no old flags in sealed_root.h */
#endif

#define SECTOR_BYTES        512u
#define SECTORS_PER_BLOCK   (UPD_BLOCK / SECTOR_BYTES)
#define PROGRAM_PAGE        256u            /* FLASH_ROM_WRITE's unit */

#define UID_ADDR            0x1FFFF7E8u     /* ESIG UNIID1, then UNIID2 */

static card_map map;
static uint32_t card_sectors;
static uint32_t stage_base, backup_base;
static uint32_t update_base, update_blocks;     /* update.lfu on the card, if present */
static uint32_t io_buf[UPD_BLOCK / 4] __attribute__((aligned(32)));

/* ------------------------------------------------------------------------
 * The callbacks
 */
static int in_range(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len;
    return (addr >= UPD_V5F_ADDR && end <= UPD_V5F_ADDR + UPD_APP_SPAN && end > addr)
        || (addr >= UPD_RECORD_A && end <= UPD_RECORD_A + UPD_SECTOR)
        || (addr >= UPD_RECORD_B && end <= UPD_RECORD_B + UPD_SECTOR)
        || (addr >= UPD_HEADER && end <= UPD_HEADER + UPD_SECTOR);
}

static int f_read(uint32_t addr, void *buf, uint32_t len) {
    if (addr + len > 0xF0000u) {
        return ERR_BLK_BOUNDS;
    }
    memcpy(buf, (const void *)(STAGE1_FLASH_BASE + addr), len);
    return 0;
}

static int f_erase(uint32_t addr) {
    if (addr % UPD_SECTOR || !in_range(addr, UPD_SECTOR)) {
        return ERR_BLK_BOUNDS;
    }
    return FLASH_ROM_ERASE(STAGE1_FLASH_BASE + addr, UPD_SECTOR) == FLASH_COMPLETE
           ? 0 : ERR_FLASH_WRITE_SECTOR;
}

/* Programs len bytes at addr in 256-byte pages; a short last page is
   padded with 0xFF (erased), so a record of any size can be written */
static int f_write(uint32_t addr, const void *buf, uint32_t len) {
    static uint32_t page[PROGRAM_PAGE / 4];
    if (addr % PROGRAM_PAGE || !in_range(addr, (len + PROGRAM_PAGE - 1) & ~(PROGRAM_PAGE - 1))) {
        return ERR_BLK_BOUNDS;
    }
    const uint8_t *p = buf;
    for (uint32_t at = 0; at < len; at += PROGRAM_PAGE) {
        uint32_t n = (len - at < PROGRAM_PAGE) ? len - at : PROGRAM_PAGE;
        memset(page, 0xFF, sizeof page);
        memcpy(page, p + at, n);
        if (FLASH_ROM_WRITE(STAGE1_FLASH_BASE + addr + at, page, PROGRAM_PAGE) != FLASH_COMPLETE) {
            return ERR_FLASH_WRITE_SECTOR;
        }
    }
    return 0;
}

static int card_read(uint32_t sector, uint8_t *buf) {
    return SD_ReadDisk(buf, sector, 1) == SD_OK ? 0 : ERR_BLK_READ_FAIL;
}

static int block_io(uint32_t base, uint32_t blocks, uint32_t blk, uint8_t *buf, int write) {
    if (blk >= blocks) {
        return ERR_BLK_BOUNDS;
    }
    uint32_t lba = base + blk * SECTORS_PER_BLOCK;
    SD_Error e = write ? SD_WriteDisk(buf, lba, SECTORS_PER_BLOCK)
                       : SD_ReadDisk(buf, lba, SECTORS_PER_BLOCK);
    return e == SD_OK ? 0 : (write ? ERR_BLK_WRITE_FAIL : ERR_BLK_READ_FAIL);
}

static upd_io io;                           /* filled in by stage1_boot */

/* Recovery with no lfstage.bin (or no card): the package goes into RAM
   instead, as many blocks as the largest V5F package needs, and is
   installed from there. A reset part way through loses it, so recovery
   asks for it again. It lies outside the V3F's own RAM, which the V5F's
   PMP fences (Link_v3f.ld's RAM_STAGE): it's used only in recovery, with
   the V5F off, and stage 1 checks every block it reads from it. */
#define RAM_STAGE_BLOCKS    (2u + UPD_V5F_SLOT / UPD_BLOCK + 2u)
static uint8_t ram_stage[RAM_STAGE_BLOCKS * UPD_BLOCK]
    __attribute__((section(".ram_stage"), aligned(32)));
static int use_ram;

static int s_read(uint32_t blk, uint8_t *buf) {
    if (use_ram) {
        if (blk >= RAM_STAGE_BLOCKS) {
            return ERR_BLK_BOUNDS;
        }
        memcpy(buf, ram_stage + blk * UPD_BLOCK, UPD_BLOCK);
        return 0;
    }
    return block_io(stage_base, io.stage_blocks, blk, buf, 0);
}

static int b_read(uint32_t blk, uint8_t *buf) {
    return block_io(backup_base, io.backup_blocks, blk, buf, 0);
}

static int u_read(uint32_t blk, uint8_t *buf) {     /* update.lfu, for the card auto-update */
    return block_io(update_base, update_blocks, blk, buf, 0);
}

static int s_write(uint32_t blk, const uint8_t *buf) {
    if (use_ram) {
        if (blk >= RAM_STAGE_BLOCKS) {
            return ERR_BLK_BOUNDS;
        }
        memcpy(ram_stage + blk * UPD_BLOCK, buf, UPD_BLOCK);
        return 0;
    }
    memcpy(io_buf, buf, UPD_BLOCK);
    return block_io(stage_base, io.stage_blocks, blk, (uint8_t *)io_buf, 1);
}

static int b_write(uint32_t blk, const uint8_t *buf) {
    memcpy(io_buf, buf, UPD_BLOCK);         /* WCH's driver takes a non-const, aligned buffer */
    return block_io(backup_base, io.backup_blocks, blk, (uint8_t *)io_buf, 1);
}

/* 32 bytes from the RNG, through BLAKE2s, after the checks board.c makes */
static int rng(uint8_t *out, int len) {
    static const uint8_t person[8] = "LFstage1";
    blake2s_state S;
    uint32_t last = 0;
    uint8_t h[32];
    RCC_HBPeriphClockCmd(RCC_HBPeriph_RNG, ENABLE);
    RNG_Cmd(ENABLE);
    b2s_init(&S, 32, NULL, 0, NULL, person);
    for (int i = 0; i < 64; i++) {
        int wait = 100000;
        while (RNG_GetFlagStatus(RNG_FLAG_DRDY) == RESET && --wait) {
        }
        if (!wait || RNG_GetFlagStatus(RNG_FLAG_SECS) || RNG_GetFlagStatus(RNG_FLAG_CECS)) {
            return ERR_UNSUPPORTED_OPERATION;
        }
        uint32_t x = RNG_GetRandomNumber();
        if (i && x == last) {
            return ERR_UNSUPPORTED_OPERATION;   /* stuck */
        }
        last = x;
        b2s_puts(&S, (const uint8_t *)&x, 4);
    }
    b2s_final(&S, h);
    memcpy(out, h, len < 32 ? (size_t)len : 32);
    crypto_wipe(h, sizeof h);
    return 0;
}

/* ------------------------------------------------------------------------
 * The board's keys. Only stage 1 holds the private key: below
 * SECURITY_LEVEL 2 it's BLAKE2s of the UID (no secret: development), at
 * level 2 one of the device token's keys (upd_board_key), so it comes
 * with the security record. rec: the record, or NULL if it's blank.
 */
static int keys_get(sealed_board *b, const upd_record *rec, const char **why) {
    memset(b, 0, sizeof *b);
#ifndef SEALED_ROOT_KEY
    (void)rec;
    *why = "no root key built in (V5F/User/sealed_root.h)";
    return -1;
#else
    static const uint8_t root[32] = SEALED_ROOT_KEY;
    memcpy(b->uid, (const void *)UID_ADDR, 8);
    memcpy(b->root, root, 32);
    b->product = SEALED_PRODUCT;
#if SEC_UID_KEY
    (void)why;
    (void)rec;
    static const uint8_t person[8] = "LFinsec1";
    blake2s_state S;
    b2s_init(&S, 32, NULL, 0, NULL, person);
    b2s_puts(&S, b->uid, 8);
    b2s_final(&S, b->private_key);
    crypto_wipe(&S, sizeof S);
#else
    if (rec == NULL) {
        *why = "this board has no key yet: type enroll in recovery";
        return -1;
    }
    upd_board_key(rec, b->private_key);
#endif
    return 0;
#endif
}

/* The board's X25519 public key, for enroll; 0, or -1 (and why) */
static int board_public(uint8_t pub[32], const upd_record *rec, const char **why) {
    sealed_board keys;
    if (keys_get(&keys, rec, why) != 0) {
        return -1;
    }
    crypto_x25519_public_key(pub, keys.private_key);
    crypto_wipe(&keys, sizeof keys);
    return 0;
}

/* ------------------------------------------------------------------------
 * The card: lfstage.bin and lfbackup.bin in the exFAT root
 */
static int card_opened;

static void card_open(void) {
    char msg[100];
    card_opened = 1;
    io.stage_blocks = io.backup_blocks = 0;
    if (SD_Init() != SD_OK) {
        NVIC_DisableIRQ(SDIO_IRQn);
        printf("V3F update: no SD card\r\n");
        return;
    }
    NVIC_DisableIRQ(SDIO_IRQn);             /* polling, as the V5F does */
    uint64_t n = (uint64_t)SDCardInfo.CardCapacity / SECTOR_BYTES;
    card_sectors = (n > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)n;
    if (card_find_blocks(card_read, card_sectors, &map, msg, sizeof msg) != 0 || !map.mounted) {
        printf("V3F update: no exFAT volume on the card\r\n");
        return;
    }
    if (card_find_file(card_read, card_sectors, &map, CARD_STAGE_FILE, &stage_base,
                       &io.stage_blocks) != 0) {
        io.stage_blocks = 0;
    }
    if (card_find_file(card_read, card_sectors, &map, CARD_BACKUP_FILE, &backup_base,
                       &io.backup_blocks) != 0) {
        io.backup_blocks = 0;
    }
    if (card_find_file(card_read, card_sectors, &map, CARD_UPDATE_FILE, &update_base,
                       &update_blocks) != 0) {
        update_blocks = 0;
    }
}

/* ------------------------------------------------------------------------
 * Stage 1
 */
static void io_init(void) {
    io.flash_read = f_read;
    io.flash_erase = f_erase;
    io.flash_write = f_write;
    io.stage_read = s_read;
    io.backup_read = b_read;
    io.backup_write = b_write;
    io.random = rng;
    io.hw_rev = 0;
    io.adopt_blank = SEC_ADOPT;             /* see stage1.h */
    io.check_pages = SEC_LOCKED_PAGES;      /* the flash pages are part of the image */
    io.check_signature = SEC_SIGNED_BOOT;   /* and the release key signed them */
}

/* ------------------------------------------------------------------------
 * The lock-down check: read protection, write protection and the boot
 * ROM's downloads, as a production board must have them
 * (doc/admin/firmware-updates.md section 8). Only reads the option bytes.
 */
static char insecure[200];              /* what's wrong, or "" */

static void add(int *n, const char *what) {
    *n += snprintf(insecure + *n, sizeof insecure - (size_t)*n, "%s%s", *n ? ", " : "", what);
}

static int security_check(void) {
    uint32_t obr = FLASH->OBR, wpr = FLASH->WPR;
    uint32_t user = obr >> 2;               /* the USER byte, as FLASH_GetUserOptionByte */
    int n = 0;
    insecure[0] = 0;
    if (!(obr & FLASH_OBR_RDPRT)) {
        add(&n, "read protection off");
    }
    if (wpr & STAGE1_WRP_PROTECT) {         /* a 1 in WPR: that sector can be written */
        add(&n, "the V3F's sectors not all write-protected");
    }
    if (~wpr & STAGE1_WRP_WRITABLE) {
        add(&n, "sectors that updates write are write-protected");
    }
    if (user & OB_USBFSDL_EN) {
        add(&n, "boot ROM USB download on");
    }
    if (user & OB_USARTDL_EN) {
        add(&n, "boot ROM USART download on");
    }
    if (n && n < (int)sizeof insecure) {
        snprintf(insecure + n, sizeof insecure - (size_t)n, " (OBR %08lX, WPR %08lX)",
                 (unsigned long)obr, (unsigned long)wpr);
    }
    return n != 0;
}

/* ------------------------------------------------------------------------
 * Stage 1
 */
static int recovering;                  /* stage1_boot said STAGE1_RECOVERY */
static int forced;                      /* by Ctrl+X x3 twice: on request, or the V5F got stuck */
static int locked_out;                  /* level 2 and not locked down */
static char why_recovery[220];
static void say(const char *s);         /* prints to the debug port and the terminal */

/* The V5F stays off: recovery, in stage1_poll */
static int enter_recovery(const char *why) {
    snprintf(why_recovery, sizeof why_recovery, "%s", why);
    if (!card_opened) {
        card_open();
    }
    recovering = 1;
    CDC_SHARED->binary = CDC_BINARY_MAGIC;  /* no Ctrl+X restart: there's no V5F */
    CDC_FENCE();
    printf("V3F: recovery mode: the V5F stays off%s (%s)\r\n",
           locked_out ? "" : "; waiting for a package over USB", why);
    return STAGE1_RECOVERY;
}

/* Boots in a row whose V5F never started (stage1_wait_v5f) */
static uint32_t nostart_count(void) {
    uint32_t n = CDC_SHARED->nostart;
    return (n & 0xFFFF0000u) == CDC_NOSTART_MAGIC ? n & 0xFFFFu : 0;
}

static void nostart_set(uint32_t n) {
    CDC_SHARED->nostart = n ? CDC_NOSTART_MAGIC | n : 0;
    CDC_FENCE();
}

/* Whether to start the V5F after stage 1's own work. An update that's
   under way (or just started) has its own trial count, so the boots its
   image doesn't start don't count here. */
/* Tells the V5F the installed image's version (Forth's `revision`) */
static void publish_revision(uint32_t version) {
    CDC_SHARED->revision = version;
    CDC_FENCE();
    CDC_SHARED->rev_magic = CDC_REVISION_MAGIC;
    CDC_FENCE();
}

static int start_or_not(int updating) {
    if (locked_out) {
        return enter_recovery("security level 2: this board isn't locked down, so the V5F won't start");
    }
    if (forced) {
        return enter_recovery("Ctrl+X x3 twice: recovery on request (or the V5F got stuck before LiteForth)");
    }
    if (updating) {
        nostart_set(0);
    } else if (nostart_count() >= STAGE1_START_TRIES) {
        static char why[100];
        snprintf(why, sizeof why, "the V5F didn't start, %u boots in a row",
                 (unsigned)nostart_count());
        nostart_set(0);
        forced = 1;                         /* a package then replaces the image */
        return enter_recovery(why);
    }
    return STAGE1_START;
}

/* No image check could be made (no record, or no key). Below level 2 the
   V5F starts as before; at level 2 (SEC_SIGNED_BOOT) only an image that
   passes the check starts, so it's recovery: else erasing the record
   sectors would start anything in the slot unchecked. */
static int unchecked(const char *why, int waiting) {
    if (SEC_SIGNED_BOOT && !locked_out) {
        return enter_recovery(why);
    }
    return start_or_not(waiting);
}

void stage1_wait_v5f(void) {
    for (uint32_t ms = 0; STAGE1_V5F_START_MS == 0 || ms < STAGE1_V5F_START_MS; ms++) {
        if (HSEM_GetFlagStatus(HSEM_ID0) == SET) {
            nostart_set(0);
            return;
        }
        Delay_Ms(1);
    }
    nostart_set(nostart_count() + 1);
    printf("V3F: the V5F didn't start within %u ms (%u in a row): resetting the chip\r\n",
           (unsigned)STAGE1_V5F_START_MS, (unsigned)nostart_count());
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {
    }
    NVIC_SystemReset();
}

static int boot(void) {
    static upd_record rec;
    static char msg[160];
    sealed_board keys;
    const char *why = "";
    io_init();
    if (security_check()) {
        printf("V3F security: not locked down: %s\r\n", insecure);
        locked_out = SEC_STRICT_RDP;
    }
    forced = (CDC_SHARED->recover == CDC_RECOVER_MAGIC);
    if (forced) {
        CDC_SHARED->recover = 0;
        CDC_SHARED->safe_boot = 0;          /* the V5F isn't starting now */
        CDC_FENCE();
    }
    int waiting = (CDC_SHARED->update == CDC_UPDATE_MAGIC);
    int r = upd_record_load(&io, &rec);
    publish_revision(r == 0 ? rec.v5f_version : 0);
    if (r == 1 && !waiting) {               /* never updated */
        return unchecked("no V5F image installed: type enroll, then send an app package", waiting);
    }
    if (r < 0) {
        printf("V3F update: can't read the security record (%d)\r\n", r);
        return unchecked("can't read the security record", waiting);
    }
    if (keys_get(&keys, r == 0 ? &rec : NULL, &why) != 0) {
        printf("V3F update: %s; nothing done\r\n", why);
        CDC_SHARED->update = 0;
        return unchecked(why, waiting);
    }
    int busy = (r == 0 && rec.state != UPD_IDLE);
    card_open();                            /* a record exists, or a package waits */
    /* Update from the card: with no `-2 ymodem` pending and nothing already
       in progress, an update.lfu on the card that is validly signed for this
       board and newer than the installed version is installed, exactly as
       `~x update.lfu -2` would. sealed_verify_package does the signature,
       product, recipient and block-hash checks in one call; setting its
       version floor to one above the installed version (and above a version
       that just rolled back) is the "install only if later" gate, and keeps
       the persistent file from re-installing every boot or boot-looping a bad
       image. On a pass the install source is pointed at update.lfu and the
       normal upd_boot installs it with backup, trial and rollback. */
    if (!waiting && !busy && update_blocks != 0) {
        sealed_package pkg;
        /* install only a version later than the installed one; don't retry a
           version that just rolled back (sealed_verify_package validates the
           signature, product, recipient and block hashes but ignores
           min_revision, so the version is compared here) */
        uint32_t floor = rec.v5f_version;
        if (rec.result == UPD_R_ROLLED_BACK && rec.pkg_version > floor) {
            floor = rec.pkg_version;
        }
        if (sealed_verify_package(update_blocks, &keys, u_read, (uint8_t *)io_buf, &pkg,
                                  msg, sizeof msg) == 0 && pkg.version > floor) {
            printf("V3F update: " CARD_UPDATE_FILE " version %lu is newer; installing\r\n",
                   (unsigned long)pkg.version);
            stage_base = update_base;       /* install from update.lfu, not lfstage.bin */
            io.stage_blocks = update_blocks;
            waiting = 1;
        }
    }
    int result = upd_boot(&io, &keys, waiting, &rec, msg, sizeof msg);
    crypto_wipe(&keys, sizeof keys);
    publish_revision(rec.v5f_version);      /* as it is now: installed, or rolled back */
    CDC_SHARED->update = 0;                 /* the package has been dealt with */
    CDC_FENCE();
    if (result < 0) {
        printf("V3F update: stopped by error %d (%s); it carries on at the next reset\r\n",
               result, msg);
        if (rec.state != UPD_INSTALLING && rec.state != UPD_ROLLING_BACK) {
            return start_or_not(waiting || busy);   /* the image in flash wasn't touched */
        }
        snprintf(msg, sizeof msg,           /* half programmed: don't run it */
                 "update: stopped by error %d part way through programming; "
                 "type reset to carry on", result);
        return enter_recovery(msg);
    }
    if (waiting) {
        say(msg);                     /* a package was sent: tell the terminal too, not just
                                         the debug port, so a refused install isn't silent */
    } else if (busy || result != UPD_START || strcmp(msg, "update: nothing to do") != 0) {
        printf("V3F %s\r\n", msg);    /* anything it did: an install, a rollback, an adopt */
    }
    if (result == UPD_START) {
        return start_or_not(waiting || busy || rec.state != UPD_IDLE);
    }
    return enter_recovery(msg);
}

/* The boot log's summary: the build's security level and the installed
   image's version (Forth's `revision`, the package's), so a log says which
   firmware it came from */
int stage1_boot(void) {
    int result = boot();
    uint32_t version = CDC_SHARED->rev_magic == CDC_REVISION_MAGIC ? CDC_SHARED->revision : 0;
    printf("V3F stage 1: security level %d, installed version %lu%s\r\n", SECURITY_LEVEL,
           (unsigned long)version, result == STAGE1_START ? "" : ", recovery");
    return result;
}

/* ------------------------------------------------------------------------
 * Recovery: the terminal, through the rings
 */
#define RING_MASK   (CDC_RING_SIZE - 1u)

/* The next byte from the host, or -1 after ms (0: just a look) */
static int t_get(int ms) {
    cdc_ring_t *r = &CDC_SHARED->rx;
    for (int t = 0; ; t++) {
        cdc_bridge_poll();
        if (cdc_ring_count(r)) {
            uint8_t c = r->data[r->tail & RING_MASK];
            CDC_FENCE();
            r->tail++;
            return c;
        }
        if (t >= ms * 10) {
            return -1;
        }
        Delay_Us(100);
    }
}

/* Sends a byte; dropped if the ring stays full (nobody reading) */
static void t_put(int c) {
    cdc_ring_t *r = &CDC_SHARED->tx;
    for (int t = 0; cdc_ring_space(r) == 0; t++) {
        if (!CDC_SHARED->host_open || t > 20000) {
            return;
        }
        cdc_bridge_poll();
        Delay_Us(100);
    }
    r->data[r->head & RING_MASK] = (uint8_t)c;
    CDC_FENCE();
    r->head++;
}

/* A line to the terminal, and to the debug UART */
static void say(const char *s) {
    printf("V3F %s\r\n", s);
    while (*s) {
        t_put(*s++);
    }
    t_put('\r');
    t_put('\n');
}

static void prompt(void) {
    for (const char *p = "recovery ok> "; *p; p++) {
        t_put(*p);
    }
}

/* Lets the host take what's in the tx ring, then resets the chip */
static void restart(void) {
    for (int t = 0; cdc_ring_count(&CDC_SHARED->tx) && t < 10000; t++) {
        cdc_bridge_poll();
        Delay_Us(100);
    }
    Delay_Ms(50);
    NVIC_SystemReset();
}

#if SEC_STRICT_RDP
/* `lockdown`, in level 2's recovery console: the option bytes as the
   lock-down check wants them. The values are those WCH's
   FLASH_EnableWriteProtection, FLASH_UserOptionByteConfig and
   FLASH_ReadOutProtection(ENABLE) would leave, one after the other, but
   with one erase and one pass, as those functions do each (so a power cut
   has one short window, and the code is smaller). They load at the next
   reset. Only a chip erase undoes it. */
static void ob_program(const uint16_t *ob) {
    volatile uint16_t *dst = (volatile uint16_t *)OB;
    if (FLASH->CTLR & FLASH_CTLR_LOCK) {
        FLASH->KEYR = 0x45670123u;          /* FLASH_KEY1, FLASH_KEY2 */
        FLASH->KEYR = 0xCDEF89ABu;
    }
    FLASH->OBKEYR = 0x45670123u;
    FLASH->OBKEYR = 0xCDEF89ABu;
    FLASH->CTLR |= FLASH_CTLR_OPTER;
    FLASH->CTLR |= FLASH_CTLR_STRT;
    while (FLASH->STATR & FLASH_STATR_BSY) {
    }
    FLASH->CTLR &= ~FLASH_CTLR_OPTER;
    FLASH->CTLR |= FLASH_CTLR_OPTPG;
    for (int i = 0; i < 8; i++) {
        dst[i] = ob[i];
        while (FLASH->STATR & FLASH_STATR_BSY) {
        }
    }
    FLASH->CTLR &= ~FLASH_CTLR_OPTPG;
    FLASH->CTLR |= FLASH_CTLR_LOCK;
}

static void lock_down(int really) {
    static char msg[80];
    volatile uint16_t *src = (volatile uint16_t *)OB;
    uint16_t ob[8];
    if (!really) {
        say("lockdown yes: write-protect the V3F's sectors, turn the boot ROM's downloads off");
        say("and read protection on, and reset. Only a chip erase undoes it.");
        return;
    }
    for (int i = 0; i < 8; i++) {
        ob[i] = src[i];
    }
    uint32_t keep = ~(uint32_t)STAGE1_WRP_PROTECT;  /* as FLASH_EnableWriteProtection */
    for (int i = 0; i < 4; i++) {
        ob[4 + i] = (uint16_t)((keep >> (8 * i)) & 0xFFu);
    }
    ob[1] &= (uint16_t)~(OB_USBFSDL_EN | OB_USARTDL_EN);  /* downloads off, the rest kept */
    ob[0] = 0x00FF;                         /* read protection on, as FLASH_ReadOutProtection */
    printf("V3F: lockdown: programming the option bytes\r\n");
    ob_program(ob);
    for (int i = 0; i < 8 && really; i++) {     /* the data bytes: the chip writes */
        really = ((src[i] ^ ob[i]) & 0xFFu) == 0;  /* each complement byte itself */
    }
    snprintf(msg, sizeof msg, "lockdown: option bytes now RDPR %04X USER %04X WRPR0 %04X: %s",
             (unsigned)src[0], (unsigned)src[1], (unsigned)src[4], really ? "ok" : "NOT AS WRITTEN");
    printf("V3F: %s\r\n", msg);
    say(msg);
    if (really) {
        say("lockdown: restarting (if the lock-down warning comes again, power it off and on)");
        restart();
    }
}
#endif

static void help(void) {
    say("LiteForth stage 1: recovery mode. The V5F stays off:");
    say(why_recovery);
    if (locked_out) {
        say(insecure);
#if SEC_STRICT_RDP
        say("lockdown: lock this board down (it says what it will do first).");
#endif
        say("reset: start again.");
        return;
    }
    say("In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.");
    say("enroll: this board's line for boards.csv (at security level 2 it makes the board's key).");
#if SEC_ADOPT
    say("adopt: take the V5F image in flash as it is (security levels 0 and 1 only).");
#endif
    say("reset: start again.");
}

/* `-2 ymodem`: a package into lfstage.bin, then the install */
static void receive(void) {
    static uint8_t block0[UPD_BLOCK] __attribute__((aligned(4)));
    static uint8_t work[UPD_BLOCK] __attribute__((aligned(4)));
    static upd_record rec;
    static char msg[160];
    if (io.stage_blocks == 0 || use_ram) {  /* no card file: into RAM */
        if (!use_ram) {
            say("recovery: no lfstage.bin on the card (or no card): the package goes into RAM");
        }
        use_ram = 1;
        io.stage_blocks = RAM_STAGE_BLOCKS;
    }
    ymr_io y = { t_get, t_put, s_write, io.stage_blocks, "LITEFORTHF", block0, work, 60000 };
    uint32_t size;
    int err = ymr_receive(&y, &size, msg, sizeof msg);
    say(msg);
    if (err) {
        return;
    }
    sealed_board keys;
    const char *why = "";
    int blank = upd_record_load(&io, &rec);
    if (keys_get(&keys, blank == 0 ? &rec : NULL, &why) != 0) {
        say(why);
        return;
    }
    if (forced) {                           /* the image got stuck: don't keep it */
        if (blank == 1) {
            upd_boot(&io, &keys, 0, &rec, msg, sizeof msg);     /* make the record first */
        }
        upd_mark_broken(&io, msg, sizeof msg);
        say(msg);
        forced = 0;
    }
    int result = upd_boot(&io, &keys, 1, &rec, msg, sizeof msg);
    crypto_wipe(&keys, sizeof keys);
    say(msg);
    if (result == UPD_START) {
        say("recovery: installed; restarting to try it");
        restart();
    }
    if (result < 0) {
        say("recovery: stopped by an error; it carries on at the next reset");
    } else {
        snprintf(why_recovery, sizeof why_recovery, "%s", msg);
    }
}

static void put_hex(char *out, const uint8_t *p, int n) {
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        *out++ = digits[p[i] >> 4];
        *out++ = digits[p[i] & 15];
    }
    *out = 0;
}

/* `enroll`: the board's boards.csv line. At level 2 a blank record is
   made first (a new device token, and so the board's key); from then on
   the board starts only a V5F image that stage 1 installed. */
static void enroll(void) {
    static upd_record rec;
    static char line[8 * 2 + 1 + 32 * 2 + 1];
    uint8_t pub[32];
    const char *why = "";
    int blank = upd_record_load(&io, &rec);
    if (blank < 0) {
        say("enroll: can't read the security record");
        return;
    }
#if !SEC_UID_KEY
    if (blank == 1) {
        static char msg[160];
        sealed_board none;
        memset(&none, 0, sizeof none);
        int err = upd_boot(&io, &none, 0, &rec, msg, sizeof msg);
        if (err < 0 || upd_record_load(&io, &rec) != 0) {
            say("enroll: can't make the security record (the RNG, or the flash)");
            return;
        }
        say("enroll: new key made (a new security record and device token)");
        blank = 0;
    }
#endif
    if (board_public(pub, blank == 0 ? &rec : NULL, &why) != 0) {
        say(why);
        return;
    }
#if SEC_UID_KEY
    say("enroll: security level below 2: the key comes from the UID, not the RNG");
#endif
    say("enroll: boards.csv line:");
    put_hex(line, (const uint8_t *)UID_ADDR, 8);
    line[16] = ',';
    put_hex(line + 17, pub, 32);
    say(line);
    crypto_wipe(&rec, sizeof rec);
}

static void command(const char *line) {
#if SEC_STRICT_RDP
    if (strcmp(line, "lockdown") == 0 || strcmp(line, "lockdown yes") == 0) {
        if (!security_check()) {
            say("lockdown: this board is locked down already");
        } else {
            lock_down(line[8] != 0);
        }
        return;
    }
#endif
    if (locked_out && strcmp(line, "reset") != 0) {
        if (strstr(line, "ymodem")) {
            t_put(0x18);                    /* CAN CAN: kwai stops waiting */
            t_put(0x18);
        }
        if (line[0]) {
            help();
        }
        return;
    }
    if (strcmp(line, "-2 ymodem") == 0) {
        receive();
    } else if (strstr(line, "ymodem")) {
        say("recovery: only -2 ymodem (a firmware update package) works here");
        t_put(0x18);
        t_put(0x18);
#if SEC_ADOPT
    } else if (strcmp(line, "adopt") == 0) {
        static char msg[100];
        int err = upd_adopt(&io, msg, sizeof msg);
        say(msg);
        if (!err) {
            restart();
        }
#endif
    } else if (strcmp(line, "enroll") == 0) {
        enroll();
    } else if (strcmp(line, "reset") == 0) {
        restart();
    } else if (line[0]) {
        help();
    }
}

static void recovery_poll(void) {
    static char line[64];
    static int n, opened;
    if (CDC_SHARED->host_open && !opened) {     /* a terminal came: say why */
        opened = 1;
        help();
        prompt();
    } else if (!CDC_SHARED->host_open) {
        opened = 0;
    }
    int c = t_get(0);
    if (c < 0) {
        return;
    }
    if (c == '\r' || c == '\n') {
        line[n] = 0;
        t_put('\r');
        t_put('\n');
        while (n && line[n - 1] == ' ') {
            line[--n] = 0;
        }
        const char *p = line;
        while (*p == ' ') {
            p++;
        }
        command(p);
        n = 0;
        prompt();
    } else if ((c == 8 || c == 127) && n) {
        n--;
        t_put(8);
        t_put(' ');
        t_put(8);
    } else if (c >= ' ' && c < 127 && n < (int)sizeof line - 1) {
        line[n++] = (char)c;
        t_put(c);
    }
}

/* Forth's `enroll` asks for the board's public key: from the UID below
   level 2, else from the record, if there is one (no record: enroll in
   recovery makes one) */
static void serve_enroll(void) {
    static upd_record rec;
    uint8_t pub[32];
    const char *why = "";
    int blank = upd_record_load(&io, &rec);
    int err = board_public(pub, blank == 0 ? &rec : NULL, &why);
    crypto_wipe(&rec, sizeof rec);
    for (int i = 0; i < 32; i++) {
        CDC_SHARED->board_pub[i] = err ? 0 : pub[i];
    }
    CDC_FENCE();
    CDC_SHARED->enroll = err ? CDC_ENROLL_NONE : CDC_ENROLL_DONE;
    CDC_FENCE();
}

void stage1_poll(void) {
    static upd_record rec;
    if (recovering) {
        recovery_poll();
        return;
    }
    if (CDC_SHARED->enroll == CDC_ENROLL_ASK) {
        serve_enroll();
    }
    if (CDC_SHARED->update != CDC_CONFIRM_MAGIC) {
        return;
    }
    CDC_SHARED->update = 0;
    CDC_FENCE();
    if (upd_record_load(&io, &rec) == 0 && rec.state == UPD_TRIAL) {
        int err = upd_confirm(&io);
        if (err) {
            printf("V3F update: the confirm failed (%d)\r\n", err);
        } else {
            printf("V3F update: version %lu confirmed\r\n", (unsigned long)rec.v5f_version);
        }
    }
}
