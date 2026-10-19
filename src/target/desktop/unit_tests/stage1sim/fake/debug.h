/* stage1sim: WCH's debug.h as stage 1 uses it. printf goes to the
   simulated debug UART (stderr); the delays are real time, and serve the
   simulated USB and V5F meanwhile (sim.c). */
#ifndef SIM_DEBUG_H
#define SIM_DEBUG_H

#include <stdio.h>
#include "ch32h417.h"

void Delay_Us(uint32_t n);
void Delay_Ms(uint32_t n);
int sim_debug_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define printf sim_debug_printf

#endif
