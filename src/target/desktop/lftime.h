#ifndef LFTIME_H
#define LFTIME_H

#include <stdint.h>

/**
 * @file systime.h
 * @brief Header file for time functions.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Gets the system time in microseconds since hard reset, or Epoch time if the
 * system has a real-time clock. The resolution is typically 1 microsecond,
 * but the actual precision may be lower depending on the hardware.
 * @return 64-bit count.
 */
uint64_t lfGetTimeMicroSec(void);

#ifdef __cplusplus
}
#endif

#endif /* LFTIME_H */