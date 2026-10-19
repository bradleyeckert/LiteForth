/*
 * Stage 1 on the V3F, first step: see stage1.h. The callbacks give
 * src/update.c the code flash (FLASH_ROM_ERASE and FLASH_ROM_WRITE, fenced
 * to the V5F's slot and the two record sectors, so no mistake here can
 * erase the V3F itself), the card's lfstage.bin and lfbackup.bin (WCH's
 * polling SD driver, through src/card.c to find them), and the RNG.
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

/* The V5F's board record (V5F/User/board.c): 256 bytes at 0x08070000 */
#define BOARD_RECORD_ADDR   0x08070000u
#define BOARD_RECORD_MAGIC  0x314B464Cu     /* "LFK1" */
#define UID_ADDR            0x1FFFF7E8u
typedef struct {
    uint32_t magic;
    uint32_t min_revision;
    uint32_t min_serial;
    uint32_t reserved;
    uint8_t private_key[32];
    uint8_t check[16];                      /* BLAKE2s-128 of the first 48 bytes, LFrecord */
} board_record;

static card_map map;
static uint32_t card_sectors;
static uint32_t stage_base, backup_base;
static uint32_t io_buf[UPD_BLOCK / 4] __attribute__((aligned(32)));

/* ------------------------------------------------------------------------
 * The callbacks
 */
static int in_range(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len;
    return (addr >= UPD_V5F_ADDR && end <= UPD_V5F_ADDR + UPD_V5F_SLOT && end > addr)
        || (addr >= UPD_RECORD_A && end <= UPD_RECORD_A + UPD_SECTOR)
        || (addr >= UPD_RECORD_B && end <= UPD_RECORD_B + UPD_SECTOR);
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
   asks for it again. */
#define RAM_STAGE_BLOCKS    (2u + UPD_V5F_SLOT / UPD_BLOCK + 2u)
static uint8_t ram_stage[RAM_STAGE_BLOCKS * UPD_BLOCK] __attribute__((aligned(32)));
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
 * The board's keys, as V5F/User/board.c's board_get has them
 */
static int keys_get(sealed_board *b, const char **why) {
    memset(b, 0, sizeof *b);
#ifndef SEALED_ROOT_KEY
    *why = "no root key built in (V5F/User/sealed_root.h)";
    return -1;
#else
    static const uint8_t root[32] = SEALED_ROOT_KEY;
    memcpy(b->uid, (const void *)UID_ADDR, 8);
    memcpy(b->root, root, 32);
    b->product = SEALED_PRODUCT;
#if SEC_UID_KEY
    (void)why;
    static const uint8_t person[8] = "LFinsec1";
    blake2s_state S;
    b2s_init(&S, 32, NULL, 0, NULL, person);
    b2s_puts(&S, b->uid, 8);
    b2s_final(&S, b->private_key);
    crypto_wipe(&S, sizeof S);
#else
    board_record r;
    uint8_t check[16];
    static const uint8_t person[8] = "LFrecord";
    blake2s_state S;
    memcpy(&r, (const void *)BOARD_RECORD_ADDR, sizeof r);
    b2s_init(&S, 16, NULL, 0, NULL, person);
    b2s_puts(&S, (const uint8_t *)&r, 48);
    b2s_final(&S, check);
    if (r.magic != BOARD_RECORD_MAGIC || memcmp(check, r.check, 16) != 0) {
        crypto_wipe(&r, sizeof r);
        *why = "this board has no key: run enroll";
        return -1;
    }
    memcpy(b->private_key, r.private_key, 32);
    b->min_serial = r.min_serial;
    crypto_wipe(&r, sizeof r);
#endif
    return 0;
#endif
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
}

/* ------------------------------------------------------------------------
 * The lock-down check: read protection, write protection and the boot
 * ROM's downloads, as a production board must have them
 * (doc/firmware-updates.md section 8). Only reads the option bytes.
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
static int forced;                      /* by Ctrl+X x3 twice: the V5F got stuck */
static int locked_out;                  /* level 2 and not locked down */
static char why_recovery[220];

/* The V5F stays off: recovery, in stage1_poll */
static int enter_recovery(const char *why) {
    snprintf(why_recovery, sizeof why_recovery, "%s", why);
    if (!card_opened) {
        card_open();
    }
    recovering = 1;
    CDC_SHARED->binary = CDC_BINARY_MAGIC;  /* no Ctrl+X restart: there's no V5F */
    CDC_FENCE();
    printf("V3F: recovery mode: the V5F stays off%s\r\n",
           locked_out ? "" : "; waiting for a package over USB");
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
static int start_or_not(int updating) {
    if (locked_out) {
        return enter_recovery("security level 2: this board isn't locked down, so the V5F won't start");
    }
    if (forced) {
        return enter_recovery("Ctrl+X x3 twice: the V5F got stuck before LiteForth started");
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

int stage1_boot(void) {
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
    if (r == 1 && !waiting) {
        return start_or_not(waiting);       /* never updated: boot as before */
    }
    if (r < 0) {
        printf("V3F update: can't read the security record (%d)\r\n", r);
        return start_or_not(waiting);
    }
    if (keys_get(&keys, &why) != 0) {
        printf("V3F update: %s; nothing done\r\n", why);
        CDC_SHARED->update = 0;
        return start_or_not(waiting);
    }
    int busy = (r == 0 && rec.state != UPD_IDLE);
    card_open();                            /* a record exists, or a package waits */
    int result = upd_boot(&io, &keys, waiting, &rec, msg, sizeof msg);
    crypto_wipe(&keys, sizeof keys);
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
    if (waiting || busy || result != UPD_START) {
        printf("V3F %s\r\n", msg);
    }
    if (result == UPD_START) {
        return start_or_not(waiting || busy || rec.state != UPD_IDLE);
    }
    return enter_recovery(msg);
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

static void help(void) {
    say("LiteForth stage 1: recovery mode. The V5F stays off:");
    say(why_recovery);
    if (locked_out) {
        say(insecure);
        say("reset: start again.");
        return;
    }
    say("In kwai, ~x FILE -2 sends a firmware update package (into lfstage.bin) to install.");
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
    if (keys_get(&keys, &why) != 0) {
        say(why);
        return;
    }
    if (forced) {                           /* the image got stuck: don't keep it */
        if (upd_record_load(&io, &rec) == 1) {
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

static void command(const char *line) {
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

void stage1_poll(void) {
    static upd_record rec;
    if (recovering) {
        recovery_poll();
        return;
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
