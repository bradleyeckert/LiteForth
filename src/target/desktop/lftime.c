#include <stdint.h>
#include "lftime.h"

/*=========================================================================
* Timing function for the VM
*
* The desktop simulates an MCU's free-running microsecond counter with the
* host's monotonic clock. Its starting point is unspecified (typically the
* host's boot), so, like a hardware counter, only differences between
* readings are meaningful. It isn't affected by changes to the wall-clock
* time.
=========================================================================*/

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>

uint64_t lfGetTimeMicroSec(void) {
    static LARGE_INTEGER freq;          // counts per second, fixed at boot
    LARGE_INTEGER now;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&now);
    // Split into seconds and remainder so the multiply can't overflow
    uint64_t f = (uint64_t)freq.QuadPart;
    uint64_t c = (uint64_t)now.QuadPart;
    return (c / f) * 1000000u + ((c % f) * 1000000u) / f;
}

#else

#include <time.h>

uint64_t lfGetTimeMicroSec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

#endif
