/*
 * V3F side of the USB CDC streams: moves bytes between the USB endpoints
 * and the shared rings in cdc_shared.h. See cdc_bridge.c.
 */
#ifndef CDC_BRIDGE_H
#define CDC_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Empties both rings and marks them ready (CDC_SHARED_MAGIC). The V3F
 * calls this once, before it wakes the V5F, so the V5F never sees the
 * uninitialized RAM left by power-up.
 */
void cdc_shared_init(void);

/**
 * Moves what it can in both directions without waiting:
 * host packets (EP2 OUT) into the rx ring, and up to one 64-byte packet
 * from the tx ring to the host (EP3 IN). Call it continually from the
 * V3F's main loop, in place of the SimulateCDC example's
 * UART_DataRx_Deal and UART_DataTx_Deal.
 *
 * Output is discarded while the USB device is not configured, or once the
 * host has left a packet unread for CDC_TX_TIMEOUT (no terminal has the
 * port open), so the V5F never waits forever for space in the tx ring.
 *
 * Three consecutive Ctrl+X bytes from the host restart the V5F so that it
 * boots without starting the app (see cdc_shared.h). That takes up to
 * CDC_RESTART_WAIT_MS, after which the V3F resets the whole chip if the
 * V5F hasn't started again.
 */
void cdc_bridge_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* CDC_BRIDGE_H */
