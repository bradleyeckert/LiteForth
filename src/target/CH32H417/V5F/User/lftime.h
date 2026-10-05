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

#ifdef __cplusplus
}
#endif

#endif /* LFTIME_H */
