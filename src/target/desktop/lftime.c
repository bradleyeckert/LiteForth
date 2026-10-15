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

/*=========================================================================
* Watchdog
*
* The desktop has no watchdog: hung Forth code is stopped by VM_STEP_LIMIT.
=========================================================================*/

void lfWatchdogPing(void) {
}

/*=========================================================================
* Real-time clock
*
* The host's wall clock (time()) plus an offset that lfRtcSet changes, so
* setting the time doesn't touch the host. The alarm is a time to compare
* with; lfStandby sleeps until it, then restarts lf (lfReset, main.c).
=========================================================================*/

#include <time.h>
#include "errcodes.h"
#include "main.h"

static int rtc_started;
static int64_t rtc_offset;              // lfRtcGet() - time(NULL)
static uint32_t rtc_alarm;              // 0: off
static int rtc_woke;                    // this start came from lfStandby

int lfRtcInit(void) {
    rtc_started = 1;
    return 0;
}

uint32_t lfRtcGet(void) {
    if (!rtc_started) return 0;
    return (uint32_t)((int64_t)time(NULL) + rtc_offset);
}

int lfRtcSet(uint32_t seconds) {
    if (!rtc_started) return ERR_UNSUPPORTED_OPERATION;
    rtc_offset = (int64_t)seconds - (int64_t)time(NULL);
    return 0;
}

int lfRtcSetAlarm(uint32_t seconds) {
    if (!rtc_started) return ERR_UNSUPPORTED_OPERATION;
    rtc_alarm = seconds;
    rtc_woke = 0;
    return 0;
}

int lfRtcAlarm(void) {
    if (rtc_woke) {
        rtc_woke = 0;
        return 1;
    }
    if (rtc_alarm && lfRtcGet() >= rtc_alarm) {
        rtc_alarm = 0;                  // goes off once, as RTC_CNT == ALRM does
        return 1;
    }
    return 0;
}

#if defined(_WIN32) || defined(_WIN64)
static void sleep_ms(unsigned ms) { Sleep(ms); }
#else
static void sleep_ms(unsigned ms) {
    struct timespec ts = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}
#endif

int lfStandby(void) {
    if (!rtc_started) return ERR_UNSUPPORTED_OPERATION;
    // At least 2 s ahead, as on the CH32H417 (V5F/User/lftime.c)
    if (rtc_alarm == 0 || rtc_alarm < lfRtcGet() + 2) return ERR_INVALID_NUMERIC;
    while (lfRtcGet() < rtc_alarm) {
        sleep_ms(50);
    }
    rtc_alarm = 0;
    rtc_woke = 1;
    lfReset();                          // doesn't return
    return 0;
}
