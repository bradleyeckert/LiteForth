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
    uint32_t cycles;
    do {                                /* retry if a tick lands in between */
        ms = ms_count;
        cycles = SysTick1->CNT;
    } while (ms != ms_count);
    if (cycles_per_us == 0) return 0;   /* lfTimeInit not called */
    /* The counter has reloaded but the interrupt hasn't run yet (interrupts
       off): count that millisecond here, so time never goes backwards. */
    if ((SysTick0->ISR & TICK_FLAG) && cycles < cycles_per_ms / 2) {
        ms++;
    }
    return ms * 1000u + cycles / cycles_per_us;
}

void lfWatchdogPing(void) {
}
