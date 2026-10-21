/*
 * blocksim: the V5F's block driver (V5F/User/blocks.c) and Guarded DMA
 * (gdma.c, guard.c), built unchanged on a simulated SD card, SDIO and DMA2
 * channel 1, and driven through blocks.h's API. The card is a file from
 * mkexfat.py (gen_cards.py makes them); the simulation moves its sectors
 * by "DMA" to and from wherever the channel points, a sector per tick,
 * and a tick is each lfGetTimeMicroSec call (blocks.c reads the time on
 * every poll), 100 simulated microseconds.
 *
 * What it checks, besides the data:
 *   - the channel only ever points at one address, Guarded DMA's buffer,
 *     never at a caller's buffer, and only at SDIO_FIFO on its other side;
 *   - after every transfer, finished or failed, the channel's registers,
 *     its DMAMUX request and the buffer are burned (zero);
 *   - runs longer than 32 sectors become several commands (CHNK crumbs);
 *   - errors it injects (a refused command, a data CRC error, a DMA
 *     transfer error, a stall, a card that stays busy) end the transfer
 *     with the right ior, and the next transfer works.
 *
 * The peripheral registers are at the chip's addresses (mmap, Linux), and
 * the program is linked without PIE so its .data and .bss, where the
 * buffers are, lie below 4 GB: blocks.c and gdma.c pass addresses as
 * uint32_t, as on the board. _data_vma and _susrstack, Link_v5f.ld's
 * symbols for gdma.c's buffer rule, are this program's .data and .bss.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include "ch32h417.h"
#include "sdio.h"                       /* V5F/User */
#include "blocks.h"
#include "gdma.h"
#include "pmp.h"
#include "board.h"
#include "v5f_crumbs.h"
#include "errcodes.h"

/* ---- Bits of the V5F that blocks.c and gdma.c call -------------------- */

uint32_t g_block_capacity;              /* api0.c on the board */
SD_CardInfo SDCardInfo;

int board_get(sealed_board *b, char *msg, int len) {
    memset(b, 0, sizeof *b);            /* no root key: no packages checked */
    snprintf(msg, len, "blocksim: no root key");
    return ERR_UNSUPPORTED_OPERATION;
}

static void tick(void);

/* The ecall: machine mode's service, then the hardware catches up, as it
   has by the time mret returns */
int pmp_call3(uint32_t n, uint32_t a, uint32_t b, uint32_t c) {
    int ior = (n >= GDMA_SVC_START && n <= GDMA_SVC_LAST) ? gdma_serve(n, a, b, c)
                                                         : ERR_UNSUPPORTED_OPERATION;
    tick();
    return ior;
}

int pmp_call(uint32_t n, uint32_t a, uint32_t b) {
    return pmp_call3(n, a, b, 0);
}

static uint64_t now_us;

uint64_t lfGetTimeMicroSec(void) {
    tick();
    now_us += 100;
    return now_us;
}

/* Crumbs: the tags, counted, so tests can see chunks and failures */
#define MAX_TAGS 64
static struct { uint32_t tag; int n; } tags[MAX_TAGS];

void crumb(uint32_t tag, uint32_t a, uint32_t b) {
    (void)a;
    (void)b;
    for (int i = 0; i < MAX_TAGS; i++) {
        if (tags[i].tag == tag || tags[i].tag == 0) {
            tags[i].tag = tag;
            tags[i].n++;
            return;
        }
    }
}

static int count(uint32_t tag) {
    for (int i = 0; i < MAX_TAGS; i++) {
        if (tags[i].tag == tag) return tags[i].n;
    }
    return 0;
}

static void clear_counts(void) {
    memset(tags, 0, sizeof tags);
}

void NVIC_DisableIRQ(IRQn_Type irq) {
    (void)irq;
}

/* ---- The simulated card, SDIO and DMA2 channel 1 ------------------------ */

static uint8_t *card;
static uint32_t card_sectors;

static int cmd;                         /* 18 or 25 while a transfer is on, else 0 */
static uint32_t lba;                    /* the next sector it moves */
static uint32_t data_left;              /* bytes the data path still expects */
static uint32_t sectors_moved;          /* sectors moved since the start */
static uint32_t ch_moved;               /* bytes moved since the channel was enabled */
static int was_enabled;
static int busy;                        /* ticks the card stays busy after CMD12 */
static uint32_t dma_addr;               /* the one address the channel points at */
static int sim_errors;                  /* the simulation's own complaints */

/* What to break */
static struct {
    int refuse_cmd;                     /* this command's response fails, once */
    uint32_t crc_at;                    /* a data CRC error instead of sector sectors_moved + 1 */
    uint32_t te_at;                     /* a DMA transfer error, likewise */
    int stall;                          /* the data stops (a timeout) */
    int busy_forever;                   /* the card stays busy after CMD12 */
} fault;

static void complain(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("SIM: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    sim_errors++;
}

void SDIO_SendCommand(SDIO_CmdInitTypeDef *c) {
    SDIO->ARG = c->SDIO_Argument;
    SDIO->CMD = c->SDIO_CmdIndex;
    switch (c->SDIO_CmdIndex) {
    case SD_CMD_READ_MULT_BLOCK:
    case SD_CMD_WRITE_MULT_BLOCK:
        if (cmd) complain("CMD%u while CMD%d is still on", c->SDIO_CmdIndex, cmd);
        cmd = (int)c->SDIO_CmdIndex;
        lba = c->SDIO_Argument;         /* SDHC: sector addresses */
        break;
    case SD_CMD_STOP_TRANSMISSION:
        if (cmd == SD_CMD_WRITE_MULT_BLOCK || cmd == SD_CMD_READ_MULT_BLOCK) {
            busy = fault.busy_forever ? 1 << 30 : 3;
        }
        cmd = 0;
        break;
    default:
        break;
    }
}

SD_Error CmdResp1Error(u8 c) {
    if (fault.refuse_cmd == c) {
        fault.refuse_cmd = 0;
        if (c == SD_CMD_READ_MULT_BLOCK || c == SD_CMD_WRITE_MULT_BLOCK) {
            cmd = 0;                    /* the card didn't take it */
        }
        return SD_CMD_RSP_TIMEOUT;
    }
    return SD_OK;
}

SD_Error IsCardProgramming(u8 *state) {
    *state = busy ? SD_CARD_PROGRAMMING : SD_CARD_TRANSFER;
    return SD_OK;
}

void SDIO_DataConfig(SDIO_DataInitTypeDef *d) {
    SDIO->DTIMER = d->SDIO_DataTimeOut;
    SDIO->DLEN = d->SDIO_DataLength;
    /* as WCH's: DMAEN (bit 3) kept, the rest set from d */
    SDIO->DCTRL = (SDIO->DCTRL & 0x08u) | d->SDIO_DataBlockSize | d->SDIO_TransferDir
                  | d->SDIO_TransferMode | d->SDIO_DPSM;
    data_left = d->SDIO_DataLength;
    SDIO->DCOUNT = data_left;
}

void SDIO_ClearFlag(uint32_t flag) {
    SDIO->STA &= ~flag;
}

SD_Error SD_Init(void) {
    SDCardInfo.CardType = SDIO_HIGH_CAPACITY_SD_CARD;
    SDCardInfo.CardCapacity = (long long)card_sectors * 512;
    return card ? SD_OK : SD_ERROR;
}

/* The hardware's step: write-1-to-clear flags, the card's busy time, and
   a sector of data if the card, the data path and the channel agree */
static void tick(void) {
    DMA_Channel_TypeDef *ch = DMA2_Channel1;
    if (DMA2->INTFCR) {
        DMA2->INTFR &= ~DMA2->INTFCR;
        DMA2->INTFCR = 0;
    }
    int enabled = (ch->CFGR & DMA_CFGR1_EN) != 0;
    if (enabled && !was_enabled) {
        ch_moved = 0;
        if (ch->PADDR != (uint32_t)(uintptr_t)&SDIO->FIFO) {
            complain("channel enabled with PADDR %08X, not SDIO_FIFO", ch->PADDR);
        }
        if (!dma_addr) {
            dma_addr = ch->MADDR;
        } else if (ch->MADDR != dma_addr) {
            complain("channel enabled with MADDR %08X, not %08X as before", ch->MADDR, dma_addr);
        }
        if (((DMAMUX->CFGR8_11 & 0x7Fu) + 1u) != 111u) {
            complain("DMAMUX channel 9 isn't on request 111 (SDIO)");
        }
    }
    was_enabled = enabled;
    if (busy > 0 && !cmd) {
        busy--;
    }
    if (!cmd || !(SDIO->DCTRL & SDIO_DPSM_Enable) || !(SDIO->DCTRL & 0x08u) || !enabled
        || data_left == 0 || ch->CNTR == 0 || fault.stall
        || (SDIO->STA & (SDIO_FLAG_DCRCFAIL | SDIO_FLAG_DTIMEOUT)) || (DMA2->INTFR & DMA2_FLAG_TE1)) {
        return;
    }
    int reading = cmd == SD_CMD_READ_MULT_BLOCK;
    int to_sdio = (SDIO->DCTRL & SDIO_TransferDir_ToSDIO) != 0;
    int to_periph = (ch->CFGR & DMA_DIR_PeripheralDST) != 0;
    if (reading != to_sdio || reading == to_periph) {
        complain("CMD%d with data path %s and DMA %s", cmd, to_sdio ? "to SDIO" : "to card",
                 to_periph ? "to peripheral" : "from peripheral");
        return;
    }
    if (fault.crc_at && sectors_moved + 1 == fault.crc_at) {
        fault.crc_at = 0;
        SDIO->STA |= SDIO_FLAG_DCRCFAIL;
        return;
    }
    if (fault.te_at && sectors_moved + 1 == fault.te_at) {
        fault.te_at = 0;
        DMA2->INTFR |= DMA2_FLAG_TE1 | DMA2_FLAG_GL1;
        return;
    }
    if (lba >= card_sectors) {
        SDIO->STA |= SDIO_FLAG_DTIMEOUT;   /* past the card's end */
        return;
    }
    uint8_t *mem = (uint8_t *)(uintptr_t)(ch->MADDR + ch_moved);
    if (reading) {
        memcpy(mem, card + (size_t)lba * 512, 512);
    } else {
        memcpy(card + (size_t)lba * 512, mem, 512);
    }
    lba++;
    sectors_moved++;
    ch_moved += 512;
    ch->CNTR -= 128;
    data_left -= 512;
    SDIO->DCOUNT = data_left;
    if (ch->CNTR == 0) {
        DMA2->INTFR |= DMA2_FLAG_TC1 | DMA2_FLAG_GL1;
    }
    if (data_left == 0) {
        SDIO->STA |= SDIO_FLAG_DATAEND;
    }
}

static void load_card(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        exit(2);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    free(card);
    card = malloc((size_t)n);
    if (!card || fread(card, 1, (size_t)n, f) != (size_t)n) {
        perror(path);
        exit(2);
    }
    fclose(f);
    card_sectors = (uint32_t)(n / 512);
}

static void map_peripherals(void) {
    static const struct { uint32_t base, size; } at[] = {
        { SDIO_BASE & ~0xFFFu, 0x1000 },   /* SDIO */
        { DMA1_BASE, 0x4000 },              /* PMP entry 2's block: DMA, DMAMUX, RCC, ... */
    };
    for (unsigned i = 0; i < sizeof at / sizeof at[0]; i++) {
        void *p = mmap((void *)(uintptr_t)at[i].base, at[i].size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (p != (void *)(uintptr_t)at[i].base) {
            perror("blocksim: mapping the peripherals");
            exit(2);
        }
    }
}

/* ---- The tests -------------------------------------------------------- */

static int checks, failures;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define BLOCK_BYTES 4096u
#define BLOCK_CELLS 1024u

/* Two 8-block buffers in .bss, where gdma.c lets machine mode copy */
static uint32_t buf_a[8 * BLOCK_CELLS];
static uint32_t buf_b[8 * BLOCK_CELLS];

/* gen_cards.py's pattern: byte i of block n (n >= 1) */
static uint8_t pattern(uint32_t n, uint32_t i) {
    return (uint8_t)(n * 37u + i * 7u + (i >> 9));
}

static uint32_t base_sector;            /* block 0's first sector on the card */

/* Waits for the transfer in progress; returns blk_status's last value */
static int wait(void) {
    int s, n = 0;
    while ((s = blk_status()) > 0) {
        if (++n > 1000000) {
            return 999;                 /* the simulation hung */
        }
    }
    return s;
}

/* Nonzero if the channel, its DMAMUX request and the DMA buffer are burned */
static int burned(void) {
    DMA_Channel_TypeDef *ch = DMA2_Channel1;
    if (ch->CFGR || ch->CNTR || ch->PADDR || ch->MADDR || ch->M1ADDR
        || (DMAMUX->CFGR8_11 & 0x7Fu)) {
        return 0;
    }
    if (dma_addr) {
        /* Guarded DMA's buffer is in .bss; if the channel ever pointed
           anywhere else, tick() has complained, so don't read past it */
        extern char _end[];
        const uint8_t *p = (const uint8_t *)(uintptr_t)dma_addr;
        uintptr_t n = (uintptr_t)_end > (uintptr_t)p ? (uintptr_t)_end - (uintptr_t)p : 0;
        if (n > GDMA_BUF_BYTES) n = GDMA_BUF_BYTES;
        for (uintptr_t i = 0; i < n; i++) {
            if (p[i]) return 0;
        }
    }
    return 1;
}

static int block_is_pattern(const uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < BLOCK_BYTES; i++) {
        if (p[i] != pattern(n, i)) return 0;
    }
    return 1;
}

static const uint8_t *card_block(uint32_t n) {
    return card + (size_t)(base_sector + n * 8u) * 512;
}

static void test_mount(uint32_t want_blocks) {
    uint32_t cap = 0;
    clear_counts();
    CHECK(blk_init(NULL, &cap) == 0, "blk_init");
    CHECK(cap == want_blocks, "capacity %u, want %u", cap, want_blocks);
    CHECK(burned(), "burned after the mount");
    CHECK(count(TAG('C','H','N','K')) > 0, "the mount read the card through Guarded DMA");
}

static void test_reads(void) {
    /* every block, one at a time */
    int ok = 1;
    for (uint32_t n = 1; n < 40; n++) {
        if (blk_read(n, buf_a) != 0 || !block_is_pattern((uint8_t *)buf_a, n)) {
            ok = 0;
            printf("  block %u\n", n);
        }
    }
    CHECK(ok, "single-block reads of blocks 1-39");
    CHECK(burned(), "burned after single reads");

    /* 8 blocks at once: 64 sectors, two chunks of 32 */
    memset(buf_a, 0xA5, sizeof buf_a);
    clear_counts();
    CHECK(blk_read_imm(10, buf_a, 8) == 0, "8-block read starts");
    CHECK(wait() == 0, "8-block read finishes");
    ok = 1;
    for (uint32_t k = 0; k < 8; k++) {
        if (!block_is_pattern((uint8_t *)buf_a + k * BLOCK_BYTES, 10 + k)) ok = 0;
    }
    CHECK(ok, "8-block read: all data right");
    CHECK(count(TAG('C','H','N','K')) == 2, "8-block read in %d chunks, want 2",
          count(TAG('C','H','N','K')));
    CHECK(burned(), "burned after the 8-block read");

    /* 33 sectors: 32 + 1 */
    memset(buf_a, 0xA5, sizeof buf_a);
    clear_counts();
    CHECK(blk_sector_read(base_sector + 8 * 3, buf_a, 33) == 0, "33-sector read");
    CHECK(block_is_pattern((uint8_t *)buf_a, 3) && block_is_pattern((uint8_t *)buf_a + 3 * 4096, 6),
          "33-sector read: data right");
    CHECK(((uint8_t *)buf_a)[33 * 512] == 0xA5, "33-sector read stops at sector 33");
    CHECK(count(TAG('C','H','N','K')) == 2, "33-sector read in %d chunks, want 2",
          count(TAG('C','H','N','K')));
    CHECK(burned(), "burned after the 33-sector read");

    /* 32 sectors: one chunk */
    clear_counts();
    CHECK(blk_sector_read(base_sector + 8, buf_a, 32) == 0, "32-sector read");
    CHECK(count(TAG('C','H','N','K')) == 1, "32-sector read in one chunk");
}

static void test_writes(void) {
    /* 8 blocks of new data over blocks 20-27 */
    for (uint32_t i = 0; i < sizeof buf_a; i++) {
        ((uint8_t *)buf_a)[i] = (uint8_t)(i * 13u + 5u);
    }
    clear_counts();
    CHECK(blk_write_imm(20, buf_a, 8) == 0, "8-block write starts");
    CHECK(wait() == 0, "8-block write finishes");
    CHECK(memcmp(card_block(20), buf_a, sizeof buf_a) == 0, "8-block write: the card has it");
    CHECK(count(TAG('C','H','N','K')) == 2, "8-block write in %d chunks, want 2",
          count(TAG('C','H','N','K')));
    CHECK(block_is_pattern(card_block(19), 19) && block_is_pattern(card_block(28), 28),
          "8-block write: neighbours untouched");
    CHECK(burned(), "burned after the 8-block write");
    memset(buf_b, 0, sizeof buf_b);
    CHECK(blk_read_imm(20, buf_b, 8) == 0 && wait() == 0, "read it back");
    CHECK(memcmp(buf_a, buf_b, sizeof buf_a) == 0, "8-block write reads back");

    /* one block */
    for (uint32_t i = 0; i < BLOCK_BYTES; i++) {
        ((uint8_t *)buf_a)[i] = (uint8_t)~i;
    }
    CHECK(blk_write(30, buf_a) == 0, "single-block write");
    CHECK(memcmp(card_block(30), buf_a, BLOCK_BYTES) == 0, "single-block write: the card has it");
    CHECK(burned(), "burned after the single write");
}

static void test_faults(void) {
    /* CMD18 refused: no transfer, no DMA */
    fault.refuse_cmd = SD_CMD_READ_MULT_BLOCK;
    CHECK(blk_read_imm(1, buf_a, 1) == ERR_BLK_READ_FAIL, "refused CMD18 fails the read");
    CHECK(burned(), "burned after a refused CMD18");
    CHECK(blk_read(2, buf_a) == 0 && block_is_pattern((uint8_t *)buf_a, 2), "and the next read works");

    /* CMD25 refused */
    fault.refuse_cmd = SD_CMD_WRITE_MULT_BLOCK;
    CHECK(blk_write_imm(31, buf_a, 1) == ERR_BLK_WRITE_FAIL, "refused CMD25 fails the write");
    CHECK(block_is_pattern(card_block(31), 31), "and writes nothing");
    CHECK(burned(), "burned after a refused CMD25");

    /* a CRC error in the second chunk of an 8-block read */
    fault.crc_at = sectors_moved + 40;  /* in the second chunk */
    clear_counts();
    CHECK(blk_read_imm(1, buf_a, 8) == 0, "8-block read with a CRC error starts");
    CHECK(wait() == ERR_BLK_READ_FAIL, "it fails");
    CHECK(count(TAG('F','A','I','L')) == 1, "with a FAIL crumb");
    CHECK(burned(), "burned after the CRC error");
    CHECK(blk_read(5, buf_a) == 0 && block_is_pattern((uint8_t *)buf_a, 5), "and the next read works");

    /* a DMA transfer error in the first chunk of an 8-block write */
    fault.te_at = sectors_moved + 3;    /* in the first chunk */
    clear_counts();
    for (uint32_t i = 0; i < sizeof buf_a; i++) ((uint8_t *)buf_a)[i] = 0x5A;
    CHECK(blk_write_imm(32, buf_a, 8) == 0, "8-block write with a DMA error starts");
    CHECK(wait() == ERR_BLK_WRITE_FAIL, "it fails");
    CHECK(count(TAG('D','M','A','!')) == 1, "with a DMA! crumb");
    CHECK(burned(), "burned after the DMA error");
    CHECK(block_is_pattern(card_block(34), 34), "the blocks after the error are untouched");

    /* the data stops: a timeout */
    fault.stall = 1;
    CHECK(blk_read_imm(6, buf_a, 1) == 0, "stalled read starts");
    CHECK(wait() == ERR_BLK_TIMEOUT, "it times out");
    fault.stall = 0;
    CHECK(burned(), "burned after the timeout");
    CHECK(blk_read(7, buf_a) == 0 && block_is_pattern((uint8_t *)buf_a, 7), "and the next read works");

    /* the card stays busy after CMD12 */
    fault.busy_forever = 1;
    CHECK(blk_write_imm(35, buf_a, 1) == 0, "write to a card that stays busy starts");
    CHECK(wait() == ERR_BLK_TIMEOUT, "it times out");
    fault.busy_forever = 0;
    busy = 0;
    CHECK(burned(), "burned after the busy timeout");
    CHECK(blk_read(8, buf_a) == 0 && block_is_pattern((uint8_t *)buf_a, 8), "and the next read works");

    /* a second transfer while one is on */
    CHECK(blk_read_imm(9, buf_a, 1) == 0, "a read starts");
    CHECK(blk_read_imm(10, buf_b, 1) == ERR_BLK_BUSY, "a second one is busy");
    CHECK(wait() == 0 && block_is_pattern((uint8_t *)buf_a, 9), "the first finishes right");
}

static void test_guard(void) {
    /* buffers machine mode won't copy to or from: the command goes out,
       Guarded DMA refuses the buffer, blocks.c stops the card (CMD12), and
       the channel never points at them */
    static uint32_t peripheral_like = 0x40020408u;      /* DMA2 channel 1's CFGR */
    uint32_t before = DMA2_Channel1->CFGR;
    int r = blk_read_imm(1, (uint32_t *)(uintptr_t)peripheral_like, 1);
    CHECK(r == ERR_INVALID_ADDRESS, "a read into the DMA registers: %d, want %d", r, ERR_INVALID_ADDRESS);
    CHECK(DMA2_Channel1->CFGR == before, "the channel's registers untouched");
    CHECK(burned(), "burned after the refusal");
    uint32_t local[BLOCK_CELLS];        /* the stack: machine mode's frames are there */
    if ((uintptr_t)local < 0x100000000u) {
        r = blk_read_imm(1, local, 1);
        CHECK(r == ERR_INVALID_ADDRESS, "a read into the stack: %d", r);
    }
    r = blk_write_imm(36, (uint32_t *)(uintptr_t)0x40021000u, 1);    /* RCC */
    CHECK(r == ERR_INVALID_ADDRESS, "a write from RCC: %d", r);
    CHECK(block_is_pattern(card_block(36), 36), "and writes nothing");
    CHECK(blk_read(3, buf_a) == 0 && block_is_pattern((uint8_t *)buf_a, 3), "the next read works");
    CHECK(dma_addr != 0 && (dma_addr < (uint32_t)(uintptr_t)buf_a
                            || dma_addr >= (uint32_t)(uintptr_t)(buf_b + 8 * BLOCK_CELLS)),
          "the channel's address %08X is none of the callers' buffers", dma_addr);
}

static void test_protected(void) {
    /* the MBR, outside the exFAT volume: blk_write_fence refuses it */
    static uint32_t sector[128];
    uint8_t mbr[512];
    memcpy(mbr, card, 512);
    CHECK(blk_sector_write_imm(0, sector, 1) == ERR_BLK_WRITE_PROTECTED, "a write to the MBR");
    CHECK(memcmp(mbr, card, 512) == 0, "the MBR untouched");
}

static void test_unsigned(void) {
    /* a block file without the LITEFORTH signature: capacity 1, no writes */
    uint32_t cap = 9;
    CHECK(blk_init(NULL, &cap) == 0 && cap == 1, "an unsigned file offers 1 block (%u)", cap);
    CHECK(blk_write_imm(0, buf_a, 1) == ERR_BLK_WRITE_PROTECTED, "and refuses writes");
    CHECK(burned(), "burned after mounting it");
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: sim card.img unsigned.img\n");
        return 2;
    }
    map_peripherals();
    load_card(argv[1]);
    /* the block file's first sector: block 0 starts with LITEFORTH */
    for (uint32_t s = 0; s < card_sectors; s++) {
        if (memcmp(card + (size_t)s * 512, "LITEFORTH", 9) == 0) {
            base_sector = s;
            break;
        }
    }
    CHECK(base_sector != 0, "found the block file on the card");
    test_mount(40);
    test_reads();
    test_writes();
    test_faults();
    test_guard();
    test_protected();
    load_card(argv[2]);
    test_unsigned();
    CHECK(sim_errors == 0, "the simulation complained %d times", sim_errors);
    printf("blocksim: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
