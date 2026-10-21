/*
 * stage1sim: the V3F's stage 1 (V3F/User/stage1.c) and USB bridge
 * (Common/cdc_bridge.c), built unchanged on the desktop and run on a
 * simulated board, so test_stage1.py can play every update, rollback and
 * recovery scenario, with resets and power cuts, without a board.
 *
 * One process is one boot, from reset to the next reset:
 *
 *   sim [options]           boots: exit 2 = the chip reset itself
 *                           (NVIC_SystemReset), 3 = a power cut (SIM_CUT_AT)
 *   sim --stage FILE        writes FILE into the card's lfstage.bin and sets
 *                           CDC_UPDATE_MAGIC, as the V5F's `-2 ymodem` does
 *                           after its checks
 *   sim --zap NAME          spoils block 0 of the card file NAME
 *
 * The board's state lives in files in the current directory, mapped where
 * the chip has it, so the real code reads and writes it in place:
 *
 *   flash.bin   the code flash, 1 MB at 0x08000000
 *   shared.bin  the V3F's top 32 KB of SRAM at 0x20178000, shared with the
 *               V5F (cdc_shared.h); it survives a reset, not a power cut
 *   card.img    the microSD card (mkexfat.py); no file = no card
 *
 * plus the UID at 0x1FFFF7E8 (SIM_UID, 16 hex digits). The option bytes
 * come from SIM_OBR and SIM_WPR (hex; FLASH->OBR and FLASH->WPR), or else
 * from ob.bin, which `lockdown` writes (WCH's FLASH_* option byte
 * functions, below), as the chip loads them at reset.
 *
 * The debug UART is stderr; USB is stdin (from the host) and stdout (to
 * it), as endpoints 2 and 3 through the real bridge. Lines starting "SIM:"
 * on stderr are this simulation's own.
 *
 * The V5F is a model, chosen by the start of its image in flash at UPD_V5F_ADDR
 * (or SIM_V5F): "SIMV5F:OK" boots LiteForth (counts the start, then after
 * SIM_SETUP_MS, its memory setup, clears safe_boot and confirms; answers
 * lines with ok>, and `-2 ymodem` by taking a package into lfstage.bin and
 * setting the flag, without the real V5F's checks), "SIMV5F:STUCK" starts but
 * gets stuck before LiteForth (counts the start, nothing more), anything
 * else never starts at all, so stage1_wait_v5f resets the chip. A line
 * `reboot` resets the chip, as lfReboot does.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "ch32h417.h"
#include "debug.h"
#include "ch32h417_usbfs_device.h"
#include "../../../CH32H417/V5F/User/sdio.h"
#include "cdc_shared.h"
#include "cdc_bridge.h"
#include "stage1.h"
#include "stage1_layout.h"
#include "card.h"
#include "ymrecv.h"

#define FLASH_SIZE      0x100000u
#define ERASED_WORD     0xE339E339u     /* erased flash reads this on this chip */
#define UID_PAGE        0x1FFFF000u
#define SHARED_SIZE     0x8000u

/* ------------------------------------------------------------------------
 * The simulated hardware
 */
FLASH_TypeDef sim_flash_regs;
USART_TypeDef sim_usart1;
UART_CTL Uart;
uint8_t UART_Tx_Buf[DEF_UARTx_TX_BUF_NUM_MAX * DEF_USB_FS_PACK_LEN];
uint8_t USBFS_EP3_Buf[DEF_USBD_FS_PACK_SIZE];
USBFSD_TypeDef sim_usbfsd;
volatile uint8_t USBFS_DevEnumStatus = 1;
SD_CardInfo SDCardInfo;

static uint8_t *flash = (uint8_t *)STAGE1_FLASH_BASE;
static long steps, cut_at = -1;
static int card_fd = -1;
static uint64_t rng_state;
static int stdin_eof;

int sim_debug_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);
    return n;
}

static void die(const char *what) {
    fprintf(stderr, "SIM: %s: %s\n", what, strerror(errno));
    exit(9);
}

/* A write or erase: the power may go now, leaving it half done */
static int step(void) {
    return steps++ == cut_at;
}

static void power_cut(void) {
    fprintf(stderr, "SIM: power cut at step %ld\n", cut_at);
    fflush(stderr);
    _exit(3);
}

FLASH_Status FLASH_ROM_ERASE(uint32_t addr, uint32_t len) {
    uint32_t off = addr - STAGE1_FLASH_BASE;
    if (addr < STAGE1_FLASH_BASE || off + len > FLASH_SIZE || off % UPD_SECTOR || len % UPD_SECTOR) {
        fprintf(stderr, "SIM: bad erase at %08X, %u bytes\n", (unsigned)addr, (unsigned)len);
        return FLASH_ADR_RANGE_ERROR;
    }
    int cut = step();
    uint32_t n = cut ? len / 2 : len;
    for (uint32_t i = 0; i < n; i += 4) {
        memcpy(flash + off + i, &(uint32_t){ERASED_WORD}, 4);
    }
    if (cut) {
        power_cut();
    }
    return FLASH_COMPLETE;
}

/* The option bytes: plain memory here (the UID page), which `lockdown`
   programs as the chip's flash controller would. At every reset, they go
   to ob.bin if they changed, for the next boot to load. */
static uint16_t ob_at_boot[8];

static void ob_save(void) {
    if (memcmp(ob_at_boot, (const void *)OB, 16) == 0) {
        return;
    }
    FILE *f = fopen("ob.bin", "wb");
    if (!f || fwrite((const void *)OB, 1, 16, f) != 16) {
        die("ob.bin");
    }
    fclose(f);
}

/* At boot: OBR and WPR from the option bytes, as the chip loads them */
static void ob_load(void) {
    FLASH->OBR = ((OB->RDPR & 0xFFu) != 0xA5u ? FLASH_OBR_RDPRT : 0u) | (OB->USER & 0xFFu) << 2;
    FLASH->WPR = (OB->WRPR0 & 0xFFu) | (OB->WRPR1 & 0xFFu) << 8 |
                 (OB->WRPR2 & 0xFFu) << 16 | (uint32_t)(OB->WRPR3 & 0xFFu) << 24;
}

/* The option bytes that would load as OBR and WPR (SIM_OBR, SIM_WPR) */
static void ob_from_regs(uint32_t obr, uint32_t wpr) {
    uint16_t user = (obr >> 2) & 0xFF;
    OB->RDPR = (obr & FLASH_OBR_RDPRT) ? 0x00FF : 0x5AA5;
    OB->USER = (uint16_t)(~user << 8) | user;
    OB->WRPR0 = wpr & 0xFF;
    OB->WRPR1 = (wpr >> 8) & 0xFF;
    OB->WRPR2 = (wpr >> 16) & 0xFF;
    OB->WRPR3 = (wpr >> 24) & 0xFF;
}

FLASH_Status FLASH_ROM_WRITE(uint32_t addr, uint32_t *buf, uint32_t len) {
    uint32_t off = addr - STAGE1_FLASH_BASE;
    if (addr < STAGE1_FLASH_BASE || off + len > FLASH_SIZE || off % 256 || len % 256) {
        fprintf(stderr, "SIM: bad write at %08X, %u bytes\n", (unsigned)addr, (unsigned)len);
        return FLASH_ADR_RANGE_ERROR;
    }
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t w;
        memcpy(&w, flash + off + i, 4);
        if (w != ERASED_WORD) {
            fprintf(stderr, "SIM: write over unerased flash at %08X\n", (unsigned)(addr + i));
            return FLASH_ERROR_PG;
        }
    }
    int cut = step();
    memcpy(flash + off, buf, cut ? len / 2 : len);
    if (cut) {
        power_cut();
    }
    return FLASH_COMPLETE;
}

void RCC_HBPeriphClockCmd(uint32_t periph, FunctionalState state) {
    (void)periph;
    (void)state;
}

void RNG_Cmd(FunctionalState state) {
    (void)state;
}

FlagStatus RNG_GetFlagStatus(uint32_t flag) {
    return flag == RNG_FLAG_DRDY ? SET : RESET;
}

uint32_t RNG_GetRandomNumber(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 16);
}

void NVIC_DisableIRQ(IRQn_Type irq) {
    (void)irq;
}

void NVIC_EnableIRQ(IRQn_Type irq) {
    (void)irq;
}

void NVIC_SystemReset(void) {
    ob_save();
    fflush(stdout);
    fprintf(stderr, "SIM: reset\n");
    fflush(stderr);
    _exit(2);
}

void __disable_irq(void) {
}

FlagStatus USART_GetFlagStatus(USART_TypeDef *u, uint16_t flag) {
    (void)u;
    (void)flag;
    return SET;                         /* sent at once */
}

void PWR_EnterSTOPMode(uint32_t regulator, uint8_t entry) {
    (void)regulator;
    (void)entry;
    fprintf(stderr, "SIM: idle (Stop mode for good)\n");
    fflush(stderr);
    pause();
    _exit(0);
}

void USBFS_Device_Init(FunctionalState state) {
    (void)state;
}

uint32_t HSEM_FastTake(HSEM_ID_TypeDef id) {
    (void)id;
    return 0;
}

static int v5f_started;                 /* the V5F model released HSEM0 */

FlagStatus HSEM_GetFlagStatus(HSEM_ID_TypeDef id) {
    return id == HSEM_ID0 && v5f_started ? SET : RESET;
}

void HSEM_ReleaseOneSem(HSEM_ID_TypeDef id, uint32_t process) {
    (void)id;                           /* the V3F only releases HSEM0's take */
    (void)process;
}

/* The card, 512-byte sectors from card.img */
SD_Error SD_Init(void) {
    return card_fd >= 0 ? SD_OK : SD_ERROR;
}

u8 SD_ReadDisk(u8 *buf, u32 sector, u8 cnt) {
    ssize_t n = pread(card_fd, buf, (size_t)cnt * 512, (off_t)sector * 512);
    return n == (ssize_t)cnt * 512 ? SD_OK : SD_ERROR;
}

u8 SD_WriteDisk(u8 *buf, u32 sector, u8 cnt) {
    int cut = step();
    size_t len = (size_t)cnt * 512 / (cut ? 2 : 1);
    ssize_t n = pwrite(card_fd, buf, len, (off_t)sector * 512);
    if (cut) {
        power_cut();
    }
    return n == (ssize_t)len ? SD_OK : SD_ERROR;
}

/* ------------------------------------------------------------------------
 * The V5F
 */
enum { V5F_DEAD, V5F_OK, V5F_STUCK };
static int v5f_kind, v5f_running, v5f_busy;
static char v5f_line[128];
static int v5f_len;
static double v5f_ready_at;             /* LiteForth starts then (memory setup) */
static double setup_ms;

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static void v5f_put(const char *s) {
    cdc_ring_t *t = &CDC_SHARED->tx;
    for (; *s; s++) {
        if (cdc_ring_space(t) == 0) {
            return;
        }
        t->data[t->head & (CDC_RING_SIZE - 1)] = (uint8_t)*s;
        CDC_FENCE();
        t->head++;
    }
}

/* From its reset entry, as far as the model gets */
static void v5f_boot(void) {
    CDC_SHARED->v5f_starts++;           /* serial_open */
    v5f_ready_at = 0;
    if (v5f_kind == V5F_STUCK) {
        fprintf(stderr, "SIM: V5F stuck before LiteForth\n");
        return;
    }
    v5f_ready_at = now_ms() + setup_ms;
}

/* lfMapMemory done: LiteForth starts */
static void v5f_liteforth(void) {
    cdc_shared_t *sh = CDC_SHARED;
    int safe = (sh->safe_boot == CDC_SAFE_BOOT_MAGIC);
    if (safe) {
        sh->safe_boot = 0;
    }
    /* What Forth's `revision` gives; before the start line, which tests
       stop at, so the confirm below isn't delayed */
    if (sh->rev_magic == CDC_REVISION_MAGIC) {
        fprintf(stderr, "SIM: revision %lu\r\n", (unsigned long)sh->revision);
    } else {
        fprintf(stderr, "SIM: no revision\r\n");
    }
    fprintf(stderr, "V5F: LiteForth starting%s\r\n", safe ? ", safe boot (app not started)" : "");
    if (sh->update != CDC_UPDATE_MAGIC) {
        sh->update = CDC_CONFIRM_MAGIC;
        CDC_FENCE();
    }
    v5f_put("ok>\r\n");
}

/* `-2 ymodem` on the V5F, without its checks: into lfstage.bin */
static int y_get(int ms) {
    cdc_ring_t *r = &CDC_SHARED->rx;
    for (int t = 0; ; t++) {
        cdc_bridge_poll();              /* the V3F goes on bridging meanwhile */
        if (cdc_ring_count(r)) {
            uint8_t c = r->data[r->tail & (CDC_RING_SIZE - 1)];
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

static void y_put(int c) {
    cdc_ring_t *t = &CDC_SHARED->tx;
    while (cdc_ring_space(t) == 0) {
        cdc_bridge_poll();
        Delay_Us(100);
    }
    t->data[t->head & (CDC_RING_SIZE - 1)] = (uint8_t)c;
    CDC_FENCE();
    t->head++;
}

static uint32_t stage_base, stage_blocks;
static int card_file(const char *name, uint32_t *base, uint32_t *blocks);

static int y_write(uint32_t blk, const uint8_t *buf) {
    if (blk >= stage_blocks) {
        return -1;
    }
    return pwrite(card_fd, buf, YMR_BLOCK, ((off_t)stage_base + blk * 8u) * 512) == YMR_BLOCK ? 0 : -1;
}

static void v5f_ymodem(void) {
    static uint8_t block0[YMR_BLOCK], work[YMR_BLOCK];
    char msg[160], out[400];
    uint32_t size;
    if (card_file(CARD_STAGE_FILE, &stage_base, &stage_blocks) != 0) {
        v5f_put("ymodem: no lfstage.bin\r\n ok>\r\n");
        return;
    }
    CDC_SHARED->binary = CDC_BINARY_MAGIC;
    ymr_io y = { y_get, y_put, y_write, stage_blocks, "LITEFORTHF", block0, work, 60000 };
    int err = ymr_receive(&y, &size, msg, sizeof msg);
    CDC_SHARED->binary = 0;
    if (err) {
        snprintf(out, sizeof out, "%s\r\n ok>\r\n", msg);
    } else {
        CDC_SHARED->update = CDC_UPDATE_MAGIC;
        CDC_FENCE();
        snprintf(out, sizeof out, "%s\r\nymodem: staged in lfstage.bin, %u blocks; "
                 "waiting for stage 1 to install it\r\n ok>\r\n", msg,
                 (unsigned)(size / YMR_BLOCK));
    }
    v5f_put(out);
}

/* Runs alongside the V3F: starts LiteForth once its memory setup is done,
   and answers each line. (Ctrl+X x3 resets the chip: the V3F's
   NVIC_SystemReset, exit 2.) */
static void v5f_service(void) {
    if (!v5f_running || v5f_busy) {
        return;
    }
    if (v5f_ready_at && now_ms() >= v5f_ready_at) {
        v5f_ready_at = 0;
        v5f_liteforth();
    }
    if (v5f_kind != V5F_OK || v5f_ready_at) {
        return;
    }
    cdc_ring_t *r = &CDC_SHARED->rx;
    while (cdc_ring_count(r)) {
        char c = (char)r->data[r->tail & (CDC_RING_SIZE - 1)];
        CDC_FENCE();
        r->tail++;
        if (c == '\r' || c == '\n') {
            v5f_line[v5f_len] = 0;
            if (!strcmp(v5f_line, "reboot")) {
                NVIC_SystemReset();     /* lfReboot: the whole chip */
            } else if (!strcmp(v5f_line, "-2 ymodem")) {
                v5f_busy = 1;
                v5f_ymodem();
                v5f_busy = 0;
            } else {
                v5f_put(" ok>\r\n");
            }
            v5f_len = 0;
        } else if (v5f_len < (int)sizeof v5f_line - 1) {
            v5f_line[v5f_len++] = c;
        }
    }
}

/* ------------------------------------------------------------------------
 * USB, as the USB interrupt does it, and the passing of time
 */
static void usb_service(void) {
    if (!stdin_eof && Uart.Tx_RemainNum < DEF_UARTx_TX_BUF_NUM_MAX) {
        struct pollfd p = { 0, POLLIN, 0 };
        if (poll(&p, 1, 0) > 0) {
            uint16_t slot = (uint16_t)((Uart.Tx_DealNum + Uart.Tx_RemainNum) % DEF_UARTx_TX_BUF_NUM_MAX);
            ssize_t n = read(0, &UART_Tx_Buf[slot * DEF_USB_FS_PACK_LEN], DEF_USB_FS_PACK_LEN);
            if (n <= 0) {
                stdin_eof = 1;
            } else {
                Uart.Tx_PackLen[slot] = (uint16_t)n;
                Uart.Tx_RemainNum++;
            }
        }
    }
    if ((USBFSD->UEP3_TX_CTRL & USBFS_UEP_T_RES_MASK) == USBFS_UEP_T_RES_ACK) {
        if (USBFSD->UEP3_TX_LEN && write(1, USBFS_EP3_Buf, USBFSD->UEP3_TX_LEN) < 0) {
            exit(9);
        }
        USBFSD->UEP3_TX_CTRL = USBFS_UEP_T_RES_NAK;
        Uart.USB_Up_IngFlag = 0;
    }
    v5f_service();
}

void Delay_Us(uint32_t n) {
    usb_service();
    struct timespec ts = { n / 1000000u, (long)(n % 1000000u) * 1000 };
    struct pollfd p = { 0, POLLIN, 0 };
    if (stdin_eof || Uart.Tx_RemainNum >= DEF_UARTx_TX_BUF_NUM_MAX) {
        nanosleep(&ts, NULL);
    } else {
        ppoll(&p, 1, &ts, NULL);        /* host bytes end the wait early */
    }
}

void Delay_Ms(uint32_t n) {
    while (n--) {
        Delay_Us(1000);
    }
}

/* ------------------------------------------------------------------------
 * Setting up
 */
static void *map_file(const char *name, uint32_t addr, size_t size, int must_exist) {
    int fd = open(name, O_RDWR | (must_exist ? 0 : O_CREAT), 0644);
    if (fd < 0) {
        die(name);
    }
    struct stat st;
    if (fstat(fd, &st) == 0 && (size_t)st.st_size < size && ftruncate(fd, (off_t)size) != 0) {
        die(name);
    }
    void *p = mmap((void *)(uintptr_t)addr, size, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    if (p == MAP_FAILED || p != (void *)(uintptr_t)addr) {
        die("mmap");
    }
    close(fd);
    return p;
}

static uint32_t env_hex(const char *name, uint32_t dflt) {
    const char *s = getenv(name);
    return s ? (uint32_t)strtoul(s, NULL, 16) : dflt;
}

static void setup(void) {
    map_file("flash.bin", STAGE1_FLASH_BASE, FLASH_SIZE, 1);
    map_file("shared.bin", CDC_SHARED_ADDR, SHARED_SIZE, 0);
    uint8_t *uid = mmap((void *)(uintptr_t)UID_PAGE, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (uid != (uint8_t *)(uintptr_t)UID_PAGE) {
        die("mmap UID");
    }
    const char *u = getenv("SIM_UID");
    if (!u) {
        u = "0102030405060708";
    }
    for (int i = 0; i < 8; i++) {
        unsigned b = 0;
        sscanf(u + 2 * i, "%2x", &b);
        uid[0x7E8 + i] = (uint8_t)b;
    }
    card_fd = open("card.img", O_RDWR);
    if (card_fd >= 0) {
        struct stat st;
        fstat(card_fd, &st);
        SDCardInfo.CardCapacity = st.st_size;
    }
    /* a board fresh from WCH: read protection off, nothing write-protected,
       both boot ROM downloads on; or what `lockdown` left in ob.bin */
    FILE *obf = fopen("ob.bin", "rb");
    if (!getenv("SIM_OBR") && !getenv("SIM_WPR") && obf && fread((void *)OB, 1, 16, obf) == 16) {
        ob_load();
    } else {
        FLASH->OBR = env_hex("SIM_OBR", (uint32_t)(0xC1u << 2));
        FLASH->WPR = env_hex("SIM_WPR", 0xFFFFFFFFu);
        ob_from_regs(FLASH->OBR, FLASH->WPR);
    }
    if (obf) {
        fclose(obf);
    }
    memcpy(ob_at_boot, (const void *)OB, 16);
    const char *c = getenv("SIM_CUT_AT");
    cut_at = c ? atol(c) : -1;
    rng_state = env_hex("SIM_SEED", 0x2545F491u) | 1u;
    const char *m = getenv("SIM_SETUP_MS");
    setup_ms = m ? atof(m) : 0;
    USBFSD->UEP3_TX_CTRL = USBFS_UEP_T_RES_NAK;
}

static int card_read(uint32_t sector, uint8_t *buf) {
    return SD_ReadDisk(buf, sector, 1) == SD_OK ? 0 : -1;
}

/* The base sector and blocks of a file in the card's root */
static int card_file(const char *name, uint32_t *base, uint32_t *blocks) {
    static card_map m;
    char msg[100];
    uint32_t sectors = (uint32_t)(SDCardInfo.CardCapacity / 512);
    if (card_fd < 0 || card_find_blocks(card_read, sectors, &m, msg, sizeof msg) != 0 || !m.mounted) {
        fprintf(stderr, "SIM: no exFAT card\n");
        return -1;
    }
    if (card_find_file(card_read, sectors, &m, name, base, blocks) != 0) {
        fprintf(stderr, "SIM: no %s on the card\n", name);
        return -1;
    }
    return 0;
}

static int tool_stage(const char *path) {
    uint32_t base, blocks;
    if (card_file(CARD_STAGE_FILE, &base, &blocks) != 0) {
        return 1;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        die(path);
    }
    static uint8_t data[4096];
    uint32_t blk = 0;
    size_t n;
    while ((n = fread(data, 1, sizeof data, f)) > 0) {
        if (blk >= blocks) {
            fprintf(stderr, "SIM: %s doesn't fit in lfstage.bin\n", path);
            return 1;
        }
        memset(data + n, 0, sizeof data - n);
        if (pwrite(card_fd, data, sizeof data, ((off_t)base + blk * 8u) * 512) != (ssize_t)sizeof data) {
            die("card.img");
        }
        blk++;
    }
    fclose(f);
    CDC_SHARED->update = CDC_UPDATE_MAGIC;
    fprintf(stderr, "SIM: staged %s, %u blocks\n", path, (unsigned)blk);
    return 0;
}

static int tool_zap(const char *name) {
    uint32_t base, blocks;
    if (card_file(name, &base, &blocks) != 0) {
        return 1;
    }
    static uint8_t zero[4096];
    if (pwrite(card_fd, zero, sizeof zero, (off_t)base * 512) != (ssize_t)sizeof zero) {
        die("card.img");
    }
    fprintf(stderr, "SIM: zapped %s\n", name);
    return 0;
}

static int v5f_model(void) {
    const char *e = getenv("SIM_V5F");
    const char *img = (const char *)flash + UPD_V5F_ADDR;
    if (e) {
        return !strcmp(e, "ok") ? V5F_OK : !strcmp(e, "stuck") ? V5F_STUCK : V5F_DEAD;
    }
    if (!memcmp(img, "SIMV5F:OK", 9)) {
        return V5F_OK;
    }
    if (!memcmp(img, "SIMV5F:STUCK", 12)) {
        return V5F_STUCK;
    }
    return V5F_DEAD;
}

/* ------------------------------------------------------------------------
 * The V3F's main.c and Hardware(), as far as stage 1 goes
 */
int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setup();
    if (argc == 3 && !strcmp(argv[1], "--stage")) {
        return tool_stage(argv[2]);
    }
    if (argc == 3 && !strcmp(argv[1], "--zap")) {
        return tool_zap(argv[2]);
    }
    cdc_shared_init();
    CDC_SHARED->host_open = (getenv("SIM_NOHOST") == NULL);   /* a terminal has the port */
    if (stage1_boot() == STAGE1_START) {
        printf("V3F: starting the V5F\r\n");
        v5f_kind = v5f_model();
        v5f_started = (v5f_kind != V5F_DEAD);   /* its main() releases HSEM0 first */
        stage1_wait_v5f();              /* or resets the chip */
        printf("V3F: the V5F is running\r\n");
        v5f_running = 1;
        v5f_boot();
        v5f_service();
    }
    printf("V3F: USB configured\r\n");
    for (;;) {
        cdc_bridge_poll();
        stage1_poll();
        Delay_Us(200);
    }
}
