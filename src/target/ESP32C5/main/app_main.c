/* LiteForth on the ESP32-C5: the ESP-IDF application entry.
 *
 * app_main() runs as a FreeRTOS task. It brings up the LiteForth target
 * layer in the same order the desktop and CH32H417 ports do, then hands
 * control to lfQuit(), the interpreter's main loop, which blocks on the
 * console. Wi-Fi / BLE / OTA, when added, run in their own IDF tasks; this
 * task is just LiteForth. See doc/ESP32C5/esp32c5-notes.md.
 *
 * STATUS: skeleton for milestone 1 (REPL over USB Serial/JTAG). The target
 * drivers it calls -- flash_init, serial_open, lfRtcInit, lfQuit -- and the
 * lf_*.c wrappers for the shared src/ are not written yet (see
 * main/CMakeLists.txt), so this does not link end to end until they land.
 */
#include <stdint.h>
#include "vm.h"              /* g_lf_sys_options */
#include "main.h"           /* lfInitPointers */
#include "flash.h"          /* flash_init */
#include "serial_io.h"      /* serial_open */
#include "lftime.h"         /* lfRtcInit */
#include "forth.h"          /* lfQuit */

void app_main(void)
{
    /* The IDF console (USB Serial/JTAG) is already up before app_main when
       CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y. */

    lfInitPointers();

    /* LiteForth's flash: the dedicated `lfflash` data partition, not the
       running app. flash_init hands the VM its flash region; on this target
       it maps the partition (reads) plus a RAM working page for open-flash.
       The name selects the partition rather than a file. */
    uint32_t *flash = 0;
    flash_init("lfflash", &flash);

    /* No microSD yet (milestone 3): blocks come later, over SPI. */

    serial_open(0, 0);      /* the USB Serial/JTAG console */
    lfRtcInit();            /* the RTC timer */

    /* Boot from the image in lfflash if one is present; otherwise a bare
       prompt with the C primitives only, until `1 load` from the card
       compiles core.f (same as the CH32H417 first boot). */
    (void)g_lf_sys_options;

    lfQuit();               /* the interpreter loop; does not return */
}
