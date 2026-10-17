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
    printf("V5F crumbs: reset flags %08lX:%s%s%s%s%s%s\r\n", (unsigned long)why,
           (why & (1u << 31)) ? " LOCKUP" : "", (why & (1u << 30)) ? " WWDG" : "",
           (why & (1u << 29)) ? " IWDG" : "", (why & (1u << 28)) ? " software" : "",
           (why & (1u << 27)) ? " power" : "", (why & (1u << 26)) ? " pin" : "");
    RCC->RSTSCKR |= RCC_RMVF;           /* so the next boot sees only its own */

    if (c->magic == CRUMB_MAGIC) {
        uint32_t n = c->next;
        uint32_t shown = (n < REPORT_LAST) ? n : REPORT_LAST;
        printf("V5F crumbs: boot %lu; the previous run left %lu crumbs, the last %lu:\r\n",
               (unsigned long)c->boots, (unsigned long)n, (unsigned long)shown);
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
        printf("V5F crumbs: none from before (power-up)\r\n");
        c->boots = 1;
    }
    c->v3f_note = 0;
    c->next = 0;
    CDC_FENCE();
    c->magic = CRUMB_MAGIC;
    CDC_FENCE();
}
