/*
 * Terminal I/O for the ESP32-C5 over the CH340 UART bridge (UART0, the ROM
 * download / default console UART, which has the CH340's auto-reset for
 * flashing). Uses the IDF uart driver with an RX ring, so serial_ready never
 * blocks and serial_getc blocks the FreeRTOS task (not a busy-spin) while it
 * waits. The chip's USB Serial/JTAG stays available as an alternative console.
 *
 * IDF's own boot logs also go to UART0; lower CONFIG_LOG_DEFAULT_LEVEL if the
 * interleaving is distracting. See doc/ESP32C5/esp32c5-notes.md.
 */
#include "serial_io.h"
#include "../../../errcodes.h"   /* src/errcodes.h */
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

#define LF_UART        UART_NUM_0
#define RX_RING_BYTES  1024

static int s_open;
static int s_binary;

int set_terminal_mode(int enable) {
    (void)enable;                           /* a raw UART: nothing to switch */
    return 0;
}

int serial_open(char* name, int baudrate) {
    (void)name;
    if (s_open) {
        return 0;
    }
    uart_config_t cfg = {
        .baud_rate = baudrate ? baudrate : 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(LF_UART, RX_RING_BYTES, 0, 0, NULL, 0) != ESP_OK) {
        return ERR_PORT_OPEN_FAILED;
    }
    uart_param_config(LF_UART, &cfg);
    /* Keep UART0's default pins (where the CH340 is wired). */
    uart_set_pin(LF_UART, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    s_open = 1;
    s_binary = 0;
    return 0;
}

void serial_close(void) {
    if (s_open) {
        uart_wait_tx_done(LF_UART, portMAX_DELAY);
    }
}

int serial_ready(void) {
    size_t n = 0;
    if (!s_open || uart_get_buffered_data_len(LF_UART, &n) != ESP_OK) {
        return 0;
    }
    return n != 0;
}

int serial_busy(void) {
    return 0;                               /* serial_putc blocks instead */
}

int serial_getc(void) {
    uint8_t c = 0;
    while (uart_read_bytes(LF_UART, &c, 1, portMAX_DELAY) != 1) {
        /* retry: portMAX_DELAY blocks the task, so this does not busy-spin */
    }
    return c;
}

int serial_putc(char c) {
    uart_write_bytes(LF_UART, &c, 1);
    return 0;
}

void serial_binary(int on) {
    s_binary = on;                          /* no Ctrl+X bridge escape here */
}
