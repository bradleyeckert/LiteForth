/*
 * Time base for LiteForth on the V5F.
 *
 * SysTick1 (the V5F's system timer) counts HCLK cycles up to CMP and
 * reloads to 0, interrupting once a millisecond. The interrupt counts
 * milliseconds in 64 bits; a reading is that count plus the cycles into
 * the current millisecond. Same setup as WCH's SYSTICK_Interrupt example.
 *
 * The V5F's SysTick flag is bit 1 of SysTick0->ISR, a register it shares
 * with the V3F (bit 0). The interrupt clears its bit with the same
 * read-modify-write WCH's code uses on both cores.
 */
#include "ch32h417.h"
#include "lftime.h"

extern uint32_t HCLKClock;              /* set by SystemAndCoreClockUpdate */

#define TICK_FLAG   (1u << 1)           /* the V5F's bit in SysTick0->ISR */

static volatile uint64_t ms_count;      /* whole milliseconds since lfTimeInit */
static uint32_t cycles_per_us;
static uint32_t cycles_per_ms;

void SysTick1_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

void SysTick1_Handler(void) {
    if (SysTick0->ISR & TICK_FLAG) {
        SysTick0->ISR &= ~TICK_FLAG;
        ms_count++;
    }
}

void lfTimeInit(void) {
    cycles_per_us = HCLKClock / 1000000u;
    cycles_per_ms = HCLKClock / 1000u;
    ms_count = 0;
    SysTick1->CTLR = 0;
    SysTick0->ISR &= ~TICK_FLAG;
    SysTick1->CMP = cycles_per_ms - 1;  /* counts 0..CMP: one millisecond */
    SysTick1->CNT = 0;
    SysTick1->CTLR = 0xF;               /* reload at CMP, HCLK, interrupt, enable */
    NVIC_SetPriority(SysTick1_IRQn, 0);
    NVIC_EnableIRQ(SysTick1_IRQn);
}

uint64_t lfGetTimeMicroSec(void) {
    uint64_t ms;
    uint32_t cycles, reloaded;
    if (cycles_per_us == 0) return 0;   /* lfTimeInit not called */
    /* Upper, lower, upper: read the millisecond count, then the counter and
       its reload flag, then the count again, and retry if the interrupt ran
       in between. Reading the flag inside the loop matters: if the interrupt
       ran after the counter was read, it would have cleared the flag, and a
       small counter value would be added to the old count (1 ms back). */
    do {
        ms = ms_count;
        cycles = SysTick1->CNT;
        reloaded = SysTick0->ISR & TICK_FLAG;
    } while (ms != ms_count);
    /* The counter has reloaded but the interrupt hasn't run yet (interrupts
       off, or it's pending): count that millisecond here. A small counter
       value means the reload came before the counter was read. */
    if (reloaded && cycles < cycles_per_ms / 2) {
        ms++;
    }
    return ms * 1000u + cycles / cycles_per_us;
}

void lfWatchdogPing(void) {
}

/*=========================================================================
 * Real-time clock and standby
 *
 * The RTC (reference manual chapter 6) is a 32-bit seconds counter with a
 * prescaler and an alarm, in the backup domain, so it keeps counting
 * through resets and Stop mode. Writes go through configuration mode and
 * take a few RTC clocks: RTOFF is 1 when the last one has finished. After
 * a reset or Stop, reads are valid once RSF is set.
 *
 * The CH32H417 has no Standby mode. lfStandby uses Stop mode, which the
 * chip enters when both cores are in deep sleep: the V3F's bridge sleeps
 * when asked (cdc_shared.h), then the V5F does, waiting for the RTC alarm
 * on EXTI line 17, as WCH's PWR/Stop_Mode example waits for a pin.
 *=========================================================================*/

#include "cdc_shared.h"
#include "../../../../errcodes.h"

#define RTC_SYNC_MS     100             /* RSF, RTOFF: a few RTC clocks */
#define TX_DRAIN_MS     200             /* lfStandby lets the terminal output go */

static int rtc_started;                 /* lfRtcInit succeeded */
static int rtc_woke;                    /* this boot came from an alarm wake-up */
static uint32_t rtc_alarm;              /* the alarm lfRtcSetAlarm set, 0 = off */

/* RCC_BDCTLR, the backup domain's control register, is written only
   through bd_write: a whole 32-bit value, then a wait (up to RTC_SYNC_MS)
   until it reads back. WCH's RCC_LSEConfig writes it a byte at a time,
   and with it the board never started the crystal; RCC_RTCCLKConfig and
   RCC_RTCCLKCmd are read-modify-writes. Whole values, one step at a
   time, are what worked when typed at the REPL (BDRST, 1, 41, 141). */
#define BD_BITS (RCC_LSEON | RCC_LSEBYP | RCC_RTCSEL | RCC_RTCEN | RCC_BDRST)

static int bd_write(uint32_t value) {
    uint64_t t0 = lfGetTimeMicroSec();
    RCC->BDCTLR = value;
    while ((RCC->BDCTLR & BD_BITS) != (value & BD_BITS)) {
        if (lfGetTimeMicroSec() - t0 > (uint64_t)RTC_SYNC_MS * 1000u) return 0;
    }
    return 1;
}

/* Waits up to ms milliseconds for an RCC flag (RCC_FLAG_LSERDY, ...) */
static int rcc_wait(uint8_t flag, uint32_t ms) {
    uint64_t t0 = lfGetTimeMicroSec();
    while (RCC_GetFlagStatus(flag) == RESET) {
        if (lfGetTimeMicroSec() - t0 > (uint64_t)ms * 1000u) return 0;
    }
    return 1;
}

/* Waits up to RTC_SYNC_MS for an RTC_CTLRL flag. Returns 1 if it's set. */
static int rtc_flag_wait(uint16_t flag) {
    uint64_t t0 = lfGetTimeMicroSec();
    while ((RTC->CTLRL & flag) == 0) {
        if (lfGetTimeMicroSec() - t0 > (uint64_t)RTC_SYNC_MS * 1000u) return 0;
    }
    return 1;
}

/* The last write has finished, so the RTC takes another */
static int rtc_ready(void) {
    return rtc_flag_wait(RTC_FLAG_RTOFF);
}

/* Waits for the registers to sync after a reset or Stop (RSF) */
static int rtc_sync(void) {
    RTC->CTLRL &= (uint16_t)~RTC_FLAG_RSF;
    return rtc_flag_wait(RTC_FLAG_RSF);
}

int lfRtcInit(void) {
    cdc_shared_t *sh = CDC_SHARED;
    rtc_woke = (sh->rtc_wake == CDC_RTC_WAKE_MAGIC);
    sh->rtc_wake = 0;

    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    PWR_BackupAccessCmd(ENABLE);
    uint32_t bd = RCC->BDCTLR;
    if ((bd & RCC_RTCEN) && (bd & RCC_RTCSEL)) {
        /* Running since before this reset. The LSI, unlike the LSE, is
           switched off by a reset, so start it again if the RTC uses it. */
        if ((bd & RCC_RTCSEL) == RCC_RTCSEL_LSI) {
            RCC_LSICmd(ENABLE);
            if (!rcc_wait(RCC_FLAG_LSIRDY, RTC_SYNC_MS)) return ERR_UNSUPPORTED_OPERATION;
        }
        if (!rtc_sync()) return ERR_UNSUPPORTED_OPERATION;
        rtc_started = 1;
        return 0;
    }

    /* The backup domain has lost power, or the RTC is half set up (RTCEN
       without a clock, and RTCSEL can't be changed until a backup domain
       reset): reset it, choose a clock and start over */
    if (!bd_write(RCC_BDRST) || !bd_write(0)) return ERR_UNSUPPORTED_OPERATION;
    uint32_t sel = RCC_RTCSEL_LSE;
    uint32_t on = RCC_LSEON;            /* keeps the crystal running */
    uint32_t reload = 32768 - 1;        /* 32.768 kHz: one count a second */
    if (!bd_write(RCC_LSEON) || !rcc_wait(RCC_FLAG_LSERDY, LFRTC_LSE_TIMEOUT_MS)) {
        if (!bd_write(0)) return ERR_UNSUPPORTED_OPERATION;
        RCC_LSICmd(ENABLE);
        if (!rcc_wait(RCC_FLAG_LSIRDY, RTC_SYNC_MS)) return ERR_UNSUPPORTED_OPERATION;
        sel = RCC_RTCSEL_LSI;
        on = 0;
        reload = 40000 - 1;             /* the LSI's nominal 40 kHz */
    }
    /* RTCSEL in one write, then RTCEN */
    if (!bd_write(on | sel) || !bd_write(on | sel | RCC_RTCEN)) {
        return ERR_UNSUPPORTED_OPERATION;
    }
    if (!rtc_sync() || !rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    RTC_SetPrescaler(reload);
    if (!rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    RTC_SetCounter(0);                  /* 1970: "not set" (LFRTC_VALID_AFTER) */
    if (!rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    RTC_SetAlarm(0);
    if (!rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    rtc_started = 1;
    return 0;
}

uint32_t lfRtcGet(void) {
    if (!rtc_started) return 0;
    return RTC_GetCounter();
}

int lfRtcSet(uint32_t seconds) {
    if (!rtc_started || !rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    RTC_SetCounter(seconds);
    return rtc_ready() ? 0 : ERR_UNSUPPORTED_OPERATION;
}

int lfRtcSetAlarm(uint32_t seconds) {
    if (!rtc_started || !rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    RTC_SetAlarm(seconds);
    if (!rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    RTC_ClearFlag(RTC_FLAG_ALR);
    rtc_alarm = seconds;
    rtc_woke = 0;
    return 0;
}

int lfRtcAlarm(void) {
    if (rtc_woke) {
        rtc_woke = 0;
        return 1;
    }
    if (!rtc_started || rtc_alarm == 0) return 0;
    if ((RTC->CTLRL & RTC_FLAG_ALR) == 0) return 0;
    if (rtc_ready()) RTC_ClearFlag(RTC_FLAG_ALR);
    return 1;
}

int lfStandby(void) {
    cdc_shared_t *sh = CDC_SHARED;
    if (!rtc_started) return ERR_UNSUPPORTED_OPERATION;
    /* At least 2 s ahead, so the alarm can't go off before the V5F sleeps */
    if (rtc_alarm == 0 || rtc_alarm < lfRtcGet() + 2) return ERR_INVALID_NUMERIC;

    /* Let the terminal see what was printed last */
    uint64_t t0 = lfGetTimeMicroSec();
    while (sh->host_open && cdc_ring_count(&sh->tx)
           && lfGetTimeMicroSec() - t0 < (uint64_t)TX_DRAIN_MS * 1000u) {
        cdc_backoff();
    }

    /* The alarm, as an event on EXTI line 17. The RTC's alarm output
       (ALRIE) feeds the line; its interrupt stays off in the PFIC. */
    if (!rtc_ready()) return ERR_UNSUPPORTED_OPERATION;
    RTC_ClearFlag(RTC_FLAG_ALR);
    RTC_ITConfig(RTC_IT_ALR, ENABLE);
    EXTI_InitTypeDef exti = {0};
    exti.EXTI_Line = EXTI_Line17;
    exti.EXTI_Mode = EXTI_Mode_Event;
    exti.EXTI_Trigger = EXTI_Trigger_Rising;
    exti.EXTI_LineCmd = ENABLE;
    EXTI_Init(&exti);
    EXTI_ClearFlag(EXTI_Line17);

    /* Ask the V3F to sleep (cdc_standby) and give it time to */
    sh->standby = CDC_STANDBY_MAGIC;
    CDC_FENCE();
    t0 = lfGetTimeMicroSec();
    while ((NVIC->CSTAR[0] & 0xC000) == 0      /* the V3F's sleep state */
           && lfGetTimeMicroSec() - t0 < (uint64_t)LFRTC_V3F_SLEEP_MS * 1000u) {
    }

    __disable_irq();
    SysTick1->CTLR = 0;                 /* no millisecond ticks to wake us */
    __WFE_IDSET(EXTI_Line17);
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);

    /* __WFE returns only once EXTI line 17 has fired: the alarm. The clocks
       are back on the HSI; a reset puts everything back as it was. */
    sh->rtc_wake = CDC_RTC_WAKE_MAGIC;
    CDC_FENCE();
    NVIC_SystemReset();
    return 0;
}

int lfReboot(void) {
    cdc_shared_t *sh = CDC_SHARED;
    uint64_t t0 = lfGetTimeMicroSec();
    while (cdc_ring_count(&sh->tx) && lfGetTimeMicroSec() - t0 < 100000u) {
    }
    NVIC_SystemReset();
    return 0;
}

int lfRevision(uint32_t *rev) {
    cdc_shared_t *sh = CDC_SHARED;
    if (sh->rev_magic != CDC_REVISION_MAGIC) {
        *rev = 0;
        return ERR_UNSUPPORTED_OPERATION;
    }
    *rev = sh->revision;
    return 0;
}
