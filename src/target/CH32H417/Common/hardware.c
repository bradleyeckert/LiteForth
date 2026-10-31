/********************************** (C) COPYRIGHT  *******************************
* File Name          : hardware.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : This file provides all the hardware firmware functions.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
/*
 *@Note
 *USB-CDC device on the V3F. Based on WCH's SimulateCDC example, which
 *bridges USB to USART4; here cdc_bridge_poll bridges USB to the V5F
 *through the shared rings in cdc_shared.h instead.
 *Debug printf output goes to USART1.
*/
#include "hardware.h"
#include "ch32h417_usbfs_device.h"

#include "UART.h"
#include "ch32h417_usb.h"
#include "cdc_bridge.h"

/*********************************************************************
 * @fn      Hardware
 *
 * @brief   Starts the USB CDC device and runs the bridge to the V5F.
 *          Never returns.
 *
 * @return  none
 */
/* LiteForth: V3F/User/stage1.c. Weak, since the V5F project builds this
   file too and has no stage 1. */
void stage1_poll(void) __attribute__((weak));

void Hardware(void)
{
    printf("V3F: built %s %s, GCC %d.%d.%d\r\n", __DATE__, __TIME__,
           __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
	RCC_Configuration( );

    /* Tim7 init (a basic timer, no PWM pins: frees TIM2 for applications) */
    TIM7_Init( );

    /* USB state (Uart, used by the USB interrupt) and USART4 */
    UART_Init( 1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY );

    /* USB20 device init */
    USBFS_RCC_Init( );
    /* LiteForth: force a detach the host can see before attaching. A software
       reset (`reboot`, an update install) leaves the device pull-up
       (USBFS_UC_DEV_PU_EN) asserted -- only a pin or power reset clears the
       USBFS peripheral -- so without this the host never sees a disconnect
       after a software reset and won't re-enumerate (no COM port, and an
       update's confirm stalls, until a manual reset; doc/ch32h417-notes.md).
       Dropping the pull-up and waiting past the host's disconnect debounce
       gives a clean enumeration every boot; it's a no-op at a cold or pin
       reset, where the pull-up is already low. */
    USBFS_Device_Init( DISABLE );
    Delay_Ms( 20 );
    USBFS_Device_Init( ENABLE );
    
    uint8_t addr = 0, configured = 0;
    while(1)
    {
        cdc_bridge_poll( );
        if( stage1_poll ) stage1_poll( );   /* LiteForth: a new V5F image's confirm */

        /* Enumeration progress, for the debug UART */
        if( USBFS_DevAddr != addr )
        {
            addr = USBFS_DevAddr;
        }
        if( USBFS_DevEnumStatus != configured )
        {
            configured = USBFS_DevEnumStatus;
            if( configured ) printf("V3F: USB configured\r\n");
        }
    }
}
