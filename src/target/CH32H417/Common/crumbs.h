#ifndef CRUMBS_H
#define CRUMBS_H

#include <stdint.h>
#include "cdc_shared.h"

/*
 * Breadcrumbs for debugging resets: a small trail in the shared SRAM
 * (CDC_SHARED_ADDR + 16K, which nothing else uses and which a system reset
 * doesn't clear), so the next boot can show what happened just before.
 *
 * The V5F appends crumbs: a 4-character tag and two values, with the time.
 * The V3F only leaves a note (why it reset the chip, or where it faulted)
 * in its own word, so the cores never write the same memory.
 * crumbs_report (V5F, at startup) prints the previous trail and the note on
 * USART1, then starts a new trail.
 */

#define CRUMB_ADDR      (CDC_SHARED_ADDR + 0x4000u)
_Static_assert(sizeof(cdc_shared_t) <= 0x4000u, "the shared block ends before the crumbs");
#define CRUMB_MAGIC     0xC2C2B0B0u
#define CRUMB_COUNT     128u            /* a ring of the last 128 */

/* A tag from 4 characters: TAG('S','T','R','T') prints as "STRT" */
#define TAG(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

typedef struct {
    uint32_t tag, a, b, t;
} crumb_t;

typedef struct {
    volatile uint32_t magic;            /* CRUMB_MAGIC once started */
    volatile uint32_t next;             /* crumbs written since the trail started */
    volatile uint32_t boots;            /* V5F starts since power-up */
    volatile uint32_t v3f_note;         /* V3F: a tag, 0 if none */
    volatile uint32_t v3f_value;        /* V3F: with it, e.g. mepc */
    volatile uint32_t v3f_value2;
    crumb_t e[CRUMB_COUNT];
} crumb_log_t;

#define CRUMBS ((crumb_log_t *)CRUMB_ADDR)

/* V3F: leave a note for the next boot (before a reset it causes) */
static inline void crumb_v3f_note(uint32_t tag, uint32_t value, uint32_t value2) {
    crumb_log_t *c = CRUMBS;
    c->v3f_value = value;
    c->v3f_value2 = value2;
    CDC_FENCE();
    c->v3f_note = tag;
    CDC_FENCE();
}

#endif /* CRUMBS_H */
