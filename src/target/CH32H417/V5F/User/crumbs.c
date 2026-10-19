/*
 * The V5F's side of the breadcrumbs (Common/crumbs.h): crumb() appends one,
 * crumbs_report() prints the previous run's trail at startup.
 */
#include "debug.h"
#include "ch32h417.h"
#include "crumbs.h"
#include "v5f_crumbs.h"
#include "lftime.h"

#define REPORT_LAST     40u             /* crumbs shown from the previous run */

/* 1: list the previous run's crumbs after every reset. 0 (the default): only
   after a reset by a watchdog or a lockup, the ones that mean something went
   wrong; after a pin, power-on or software reset (`reboot`, stage 1 after an
   install or when the V5F doesn't start), one line says how many there were.
   A chip reset the V3F makes because the V5F didn't come back from Ctrl+X x3
   still shows, as the V3F's note. The debug UART runs at 9600 baud and printf
   waits, so the 40 crumbs hold up the start by about 1.5 s. */
#ifndef CRUMBS_TRAIL
#define CRUMBS_TRAIL    0
#endif
#define RESET_TROUBLE   ((1u << 31) | (1u << 30) | (1u << 29))     /* LOCKUP, WWDG, IWDG */

void crumb(uint32_t tag, uint32_t a, uint32_t b) {
    crumb_log_t *c = CRUMBS;
    if (c->magic != CRUMB_MAGIC) {
        return;                         /* crumbs_report hasn't run */
    }
    uint32_t n = c->next;
    crumb_t *e = &c->e[n % CRUMB_COUNT];
    e->tag = tag;
    e->a = a;
    e->b = b;
    e->t = (uint32_t)lfGetTimeMicroSec();
    CDC_FENCE();
    c->next = n + 1;
}

static void print_tag(uint32_t tag) {
    for (int i = 0; i < 4; i++) {
        char ch = (char)(tag >> (8 * i));
        printf("%c", (ch >= ' ' && ch < 127) ? ch : '?');
    }
}

void crumbs_report(void) {
    crumb_log_t *c = CRUMBS;
    uint32_t why = RCC->RSTSCKR;
    RCC->RSTSCKR |= RCC_RMVF;           /* so the next boot sees only its own */
    printf("V5F crumbs: reset by%s%s%s%s%s%s%s",
           (why & (1u << 31)) ? " LOCKUP" : "", (why & (1u << 30)) ? " WWDG" : "",
           (why & (1u << 29)) ? " IWDG" : "", (why & (1u << 28)) ? " software" : "",
           (why & (1u << 27)) ? " power" : "", (why & (1u << 26)) ? " pin" : "",
           (why & (0x3Fu << 26)) ? "" : " the V3F");

    if (c->magic == CRUMB_MAGIC) {
        uint32_t n = c->next;
        uint32_t shown = (n < REPORT_LAST) ? n : REPORT_LAST;
        if (!CRUMBS_TRAIL && !(why & RESET_TROUBLE)) {
            shown = 0;
        }
        printf(", boot %lu; the previous run left %lu crumbs%s\r\n",
               (unsigned long)c->boots, (unsigned long)n, shown ? ", the last:" : "");
        for (uint32_t i = n - shown; i != n; i++) {
            const crumb_t *e = &c->e[i % CRUMB_COUNT];
            printf("  %10lu ", (unsigned long)e->t);
            print_tag(e->tag);
            printf(" %08lX %08lX\r\n", (unsigned long)e->a, (unsigned long)e->b);
        }
        if (c->v3f_note) {
            printf("V5F crumbs: the V3F left ");
            print_tag(c->v3f_note);
            printf(" %08lX %08lX\r\n", (unsigned long)c->v3f_value, (unsigned long)c->v3f_value2);
        }
        c->boots = c->boots + 1;
    } else {
        printf("; none from before (power-up)\r\n");
        c->boots = 1;
    }
    c->v3f_note = 0;
    c->next = 0;
    CDC_FENCE();
    c->magic = CRUMB_MAGIC;
    CDC_FENCE();
}
