#ifndef LFTIME_H
#define LFTIME_H

#include <stdint.h>

/**
 * @file lftime.h
 * @brief Time functions for the CH32H417's V5F core. Same interface as
 * src/target/desktop/lftime.h, plus lfTimeInit.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Starts the microsecond counter: SysTick1 interrupts every millisecond
 * (on HCLK), and the interrupt counts milliseconds in 64 bits. Call once at
 * startup, after the clocks are set up (SystemAndCoreClockUpdate). After
 * this the V5F must not use WCH's Delay_Us or Delay_Ms, which reprogram
 * and stop SysTick1.
 */
void lfTimeInit(void);

/**
 * Reads a free-running microsecond counter that counts from lfTimeInit.
 * Only differences between readings are meaningful. The resolution is
 * 1 microsecond.
 * @return 64-bit count.
 */
uint64_t lfGetTimeMicroSec(void);

/**
 * Tells the watchdog that the Forth code is alive. The VM calls it each
 * time the app executes `break`. No hardware watchdog is enabled yet, so
 * it does nothing.
 */
void lfWatchdogPing(void);

#define LFRTC_LSE_TIMEOUT_MS 3000   /* lfRtcInit falls back on the LSI after this */
#define LFRTC_V3F_SLEEP_MS   100    /* lfStandby waits this long for the V3F to sleep */

/** Times before this (2026-01-01 00:00:00 UTC) mean the RTC isn't set. */
#define LFRTC_VALID_AFTER 1767225600u

/**
 * Starts the real-time clock if it isn't already running.
 * The RTC is in the backup domain, so after a reset (or a wake from
 * lfStandby) it is normally still running, and this only waits for the
 * registers to sync. After the backup domain has lost power it starts the
 * 32.768 kHz crystal (LSE), or the LSI (about 40 kHz, a few percent off)
 * if the crystal doesn't start within LFRTC_LSE_TIMEOUT_MS, sets the
 * prescaler for 1 Hz, and sets the counter and the alarm to 0. An RTC
 * left enabled without a clock source (RTCSEL 00, which can't be changed
 * until the backup domain is reset) gets a backup domain reset first.
 * RCC_BDCTLR is written one whole value at a time, each read back before
 * the next (not with WCH's byte-wide RCC_LSEConfig). It also
 * reads and clears the wake-up mark lfStandby leaves (CDC_SHARED->rtc_wake).
 * Call after lfTimeInit: the timeouts use lfGetTimeMicroSec.
 * @return 0, or ERR_UNSUPPORTED_OPERATION (-21) if no clock starts.
 */
int lfRtcInit(void);

/**
 * Reads the real-time clock: seconds since 1970-01-01 00:00:00 UTC, an
 * unsigned 32-bit count that lasts until 2106. A time before
 * LFRTC_VALID_AFTER means the clock hasn't been set since it lost power.
 * The RTC counter, RTC_CNT.
 * @return The time, or 0 before lfRtcInit.
 */
uint32_t lfRtcGet(void);

/**
 * Sets the real-time clock, in seconds since 1970 (UTC).
 * Writes RTC_CNT in configuration mode and waits for the write to finish.
 * @return 0, or ERR_UNSUPPORTED_OPERATION (-21) before lfRtcInit.
 */
int lfRtcSet(uint32_t seconds);

/**
 * Sets the alarm to a time in seconds since 1970; 0 turns it off. It also
 * clears an alarm that has gone off but hasn't been seen by lfRtcAlarm.
 * @return 0, or ERR_UNSUPPORTED_OPERATION (-21) before lfRtcInit.
 */
int lfRtcSetAlarm(uint32_t seconds);

/**
 * Tells whether the alarm has gone off, and clears that: either while
 * running (the time reached the alarm), or as the wake-up from lfStandby
 * that started this boot. A reset or power-up that didn't come from the
 * alarm doesn't count.
 * @return 1 if the alarm went off, else 0.
 */
int lfRtcAlarm(void);

/**
 * Powers down until the alarm, then restarts everything, as a reset does,
 * after which lfRtcAlarm returns 1 once.
 * Asks the V3F to stop USB and sleep (CDC_SHARED->standby), waits up to
 * LFRTC_V3F_SLEEP_MS for it to, then puts the V5F in deep sleep, so the
 * chip enters Stop mode (the CH32H417 has no Standby mode; Stop keeps RAM
 * and turns off the high-speed clocks). The RTC alarm (EXTI line 17)
 * wakes it; the V5F then marks the wake in CDC_SHARED->rtc_wake and resets
 * the chip with NVIC_SystemReset. The reset pin also ends Stop, as a plain
 * reset, without the mark.
 * The alarm must have been set (lfRtcSetAlarm, since this start) to at
 * least 2 seconds ahead, so that it can't go off before the chip sleeps.
 * @return Only on failure: ERR_INVALID_NUMERIC (-24) if the alarm isn't set
 *         at least 2 s ahead, or
 *         ERR_UNSUPPORTED_OPERATION (-21) before lfRtcInit.
 */
int lfStandby(void);

/**
 * Resets the whole chip, as the reset pin does: on the CH32H417 both cores
 * restart, the V3F's stage 1 first (doc/firmware-updates.md), after the
 * terminal has had up to 100 ms to send what's waiting. On the desktop, lf
 * restarts its interpreter as lfStandby does (lfReset in main.c).
 * @return Doesn't.
 */
int lfReboot(void);

/**
 * Forth word `cpu-fault`  ( n -- x )
 * TEMPORARY, for testing the trap routing (vmTrap in vm.c, the V5F's
 * HardFault_Handler); to be removed after the board test. Makes the CPU
 * do something it may reject, from the C code of an API call, as a bug
 * in one would:
 *   1  an illegal instruction (0x0000)          expect -128
 *   2  a 32-bit load from an odd RAM address    -23, or x if the core
 *                                               handles misaligned loads
 *   3  a load from 0x30000000 (nothing there)   -127, or x if it reads
 *   4  a store of 0 to 0x30000000               -127, or 0 if ignored
 * x is what the load read (0 for 1 and 4, which push it only if the CPU
 * carries on); any other n gives -24. On the desktop: -21.
 * @return 0, or an ior.
 */
int lfAPI_cpuFault(void);

#ifdef __cplusplus
}
#endif

#endif /* LFTIME_H */
