#ifndef V5F_CRUMBS_H
#define V5F_CRUMBS_H

#include <stdint.h>
#include "crumbs.h"

/**
 * Appends a breadcrumb (Common/crumbs.h): a tag made with TAG(), two
 * values, and the time in microseconds. Does nothing until crumbs_report
 * has started the trail.
 */
void crumb(uint32_t tag, uint32_t a, uint32_t b);

/**
 * At startup: prints on USART1 the reset flags (RCC_RSTSCKR, then
 * clears them) and how many crumbs the previous run left, then the last
 * of them if the reset was by a watchdog, a lockup or software (or always,
 * with CRUMBS_TRAIL 1), and any note the V3F left; then starts a new trail.
 */
void crumbs_report(void);

#endif /* V5F_CRUMBS_H */
