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

#ifdef __cplusplus
}
#endif

#endif /* LFTIME_H */