/*
 * Board identity for the ESP32-C5 at SECURITY_LEVEL 0 (development).
 *
 * Milestone 1 stubs: `enroll` prints the board's UID (the eFuse factory MAC);
 * there is no board keypair yet and no package checking. The real root of
 * trust will use the chip's secure boot + flash encryption + eFuse, not the
 * CH32H417's dual-core scheme. See doc/ESP32C5/esp32c5-notes.md.
 */
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "serial_io.h"
#include "esp_mac.h"

static void put(const char *s) {
    while (*s) serial_putc(*s++);
}

int board_load(const char *path, sealed_board *b, char *msg, int len) {
    (void)path;
    if (b) memset(b, 0, sizeof *b);
    snprintf(msg, len, "the ESP32-C5 holds its own identity; no board file");
    return 1;                               /* not used on this target */
}

int lfAPI_enroll(void) {
    uint8_t mac[6] = {0};
    char line[48];
    esp_efuse_mac_get_default(mac);
    snprintf(line, sizeof line,
             "enroll: UID %02x%02x%02x%02x%02x%02x (no key at level 0)\r\n",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    put(line);
    return 0;
}

int lfAPI_ecdcTest(void) {
    return 0;                               /* no hardware crypto engine wired yet */
}
