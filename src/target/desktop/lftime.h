#ifndef LFTIME_H
#define LFTIME_H

#include <stdint.h>

/**
 * @file lftime.h
 * @brief Header file for time functions.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reads a free-running microsecond counter. Each target provides it: on an
 * MCU it counts from hard reset, or is Epoch time if the system has a
 * real-time clock. The desktop version uses the host's monotonic clock,
 * which starts at an unspecified time and isn't affected by changes to the
 * wall-clock time. Only differences between readings are meaningful. The
 * resolution is 1 microsecond, but the actual precision may be lower
 * depending on the hardware.
 * @return 64-bit count.
 */
uint64_t lfGetTimeMicroSec(void);

/**
 * Tells the watchdog that the Forth code is alive. The VM calls it each time
 * the app executes `break`, which it does at least once a turn. On an MCU it
 * would reload a hardware watchdog timer, so code that hangs without reaching
 * `break` gets a hard reset. On the desktop it does nothing.
 */
void lfWatchdogPing(void);

/** Times before this (2026-01-01 00:00:00 UTC) mean the RTC isn't set. */
#define LFRTC_VALID_AFTER 1767225600u

/**
 * Starts the real-time clock if it isn't already running.
 * The desktop's clock is the host's, so this only marks it started.
 * @return 0, or ERR_UNSUPPORTED_OPERATION (-21) if no clock starts.
 */
int lfRtcInit(void);

/**
 * Reads the real-time clock: seconds since 1970-01-01 00:00:00 UTC, an
 * unsigned 32-bit count that lasts until 2106. A time before
 * LFRTC_VALID_AFTER means the clock hasn't been set since it lost power.
 * On the desktop, the host's clock plus the offset lfRtcSet leaves.
 * @return The time, or 0 before lfRtcInit.
 */
uint32_t lfRtcGet(void);

/**
 * Sets the real-time clock, in seconds since 1970 (UTC).
 * On the desktop this changes only lf's offset from the host's clock (not
 * the host's clock), which lasts until lf exits.
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
 * On the desktop, lf waits until the time reaches the alarm (what's typed
 * meanwhile waits too), then restarts the interpreter as main() starts it:
 * the same options, flash and blocks, and lfQuit from the top (lfReset in
 * main.c). The RAM page keeps its contents, like an MCU's SRAM.
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