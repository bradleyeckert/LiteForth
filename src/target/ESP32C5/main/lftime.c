/*
 * Time, clock and reset for the ESP32-C5. Microseconds and a wall clock come
 * from esp_timer (monotonic since boot) plus a software epoch offset; reboot
 * is esp_restart. The RTC alarm / standby (deep sleep) and the watchdog are
 * stubbed for milestone 1. See doc/ESP32C5/esp32c5-notes.md.
 */
#include "lftime.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_chip_info.h"

static uint32_t s_epoch;                    /* wall-clock seconds at boot (uptime 0) */
static uint32_t s_alarm;                    /* 0 = no alarm set */

uint64_t lfGetTimeMicroSec(void) {
    return (uint64_t)esp_timer_get_time();
}

void lfWatchdogPing(void) {
    /* M1: the REPL blocks in serial_getc (the task yields), so the idle task
       runs and the task WDT does not trip. Wire esp_task_wdt_reset here once
       an always-running app task is added. */
}

int lfRtcInit(void) {
    return 0;                               /* esp_timer is already running */
}

uint32_t lfRtcGet(void) {
    return s_epoch + (uint32_t)(esp_timer_get_time() / 1000000);
}

int lfRtcSet(uint32_t seconds) {
    s_epoch = seconds - (uint32_t)(esp_timer_get_time() / 1000000);
    return 0;
}

int lfRtcSetAlarm(uint32_t seconds) {
    s_alarm = seconds;                      /* recorded; deep-sleep wake is M-later */
    return 0;
}

int lfRtcAlarm(void) {
    return s_alarm;                         /* alarm time, or 0 if none set */
}

int lfStandby(void) {
    return 0;                               /* M1: standby/deep-sleep not implemented */
}

int lfReboot(void) {
    esp_restart();                          /* does not return */
    return 0;
}

int lfRevision(uint32_t *rev) {
    esp_chip_info_t info;
    esp_chip_info(&info);
    *rev = (uint32_t)info.revision;
    return 0;
}
