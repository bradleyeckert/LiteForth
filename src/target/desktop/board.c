#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "errcodes.h"
#include "monocypher.h"
#include "tools.h"

static const sealed_board *loaded;      /* the -K board, for enroll */

/* Converts exactly 2 * n hex digits at s into out; returns 0 or -1 */
static int hex(const char *s, uint8_t *out, int n) {
    if ((int)strlen(s) != 2 * n) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        unsigned x;
        if (sscanf(s + 2 * i, "%2x", &x) != 1 || !strchr("0123456789abcdefABCDEF", s[2 * i])
            || !strchr("0123456789abcdefABCDEF", s[2 * i + 1])) {
            return -1;
        }
        out[i] = (uint8_t)x;
    }
    return 0;
}

static int number(const char *s, uint32_t *out) {
    char *end;
    unsigned long x = strtoul(s, &end, 0);
    if (*s == '\0' || *end != '\0' || x > 0xFFFFFFFFul) {
        return -1;
    }
    *out = (uint32_t)x;
    return 0;
}

int board_load(const char *path, sealed_board *b, char *msg, int len) {
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(msg, len, "can't read the board file %s", path);
        return ERR_BLK_PARSE_FAIL;
    }
    memset(b, 0, sizeof *b);
    char line[256], name[32], value[200];
    int have = 0, bad = 0, n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        char *hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        int k = sscanf(line, "%31s %199s", name, value);
        if (k <= 0) {
            continue;
        }
        int err = (k != 2);
        if (!err && !strcmp(name, "uid")) {
            err = hex(value, b->uid, 8); have |= 1;
        } else if (!err && !strcmp(name, "private")) {
            err = hex(value, b->private_key, 32); have |= 2;
        } else if (!err && !strcmp(name, "root")) {
            err = hex(value, b->root, 32); have |= 4;
        } else if (!err && !strcmp(name, "product")) {
            err = number(value, &b->product); have |= 8;
        } else if (!err && !strcmp(name, "min-revision")) {
            err = number(value, &b->min_revision);
        } else if (!err && !strcmp(name, "min-serial")) {
            err = number(value, &b->min_serial);
        } else if (!err && !strcmp(name, "sealed-only")) {
            err = number(value, &b->sealed_only);
        } else {
            err = 1;
        }
        if (err && !bad) {
            bad = n;
        }
    }
    fclose(f);
    if (bad) {
        snprintf(msg, len, "%s line %d: bad setting", path, bad);
    } else if (have != 15) {
        snprintf(msg, len, "%s needs uid, private, root and product", path);
    }
    if (bad || have != 15) {
        memset(b, 0, sizeof *b);
        return ERR_BLK_PARSE_FAIL;
    }
    loaded = b;
    return 0;
}

static void put_hex(const uint8_t *p, int n) {
    static const char digits[] = "0123456789abcdef";
    char s[3] = {0};
    for (int i = 0; i < n; i++) {
        s[0] = digits[p[i] >> 4];
        s[1] = digits[p[i] & 15];
        lf_puts(s);
    }
}

int lfAPI_enroll(void) {
    if (loaded == NULL) {
        lf_puts("enroll: no board file (lf -K)");
        lfCR();
        return ERR_UNSUPPORTED_OPERATION;
    }
    uint8_t pub[32];
    crypto_x25519_public_key(pub, loaded->private_key);
    lf_puts("boards.csv line:");
    lfCR();
    put_hex(loaded->uid, 8);
    lf_puts(",");
    put_hex(pub, 32);
    lfCR();
    return 0;
}
