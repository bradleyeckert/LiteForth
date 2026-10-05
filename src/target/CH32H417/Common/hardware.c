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
void Hardware(void)
{
    printf("Build Time: %s %s\n", __DATE__, __TIME__);
    printf("GCC Version: %d.%d.%d\n",__GNUC__, __GNUC_MINOR__,__GNUC_PATCHLEVEL__);
    printf("USB CDC bridge to V5F running on USBFS controller\n");
	RCC_Configuration( );

    /* Tim2 init */
    TIM2_Init( );

    /* USB state (Uart, used by the USB interrupt) and USART4 */
    UART_Init( 1, DEF_UARTx_BAUDRATE, DEF_UARTx_STOPBIT, DEF_UARTx_PARITY );

    /* USB20 device init */
    USBFS_RCC_Init( );
    USBFS_Device_Init( ENABLE );
    
    uint8_t addr = 0, configured = 0;
    while(1)
    {
        cdc_bridge_poll( );

        /* Enumeration progress, for the debug UART */
        if( USBFS_DevAddr != addr )
        {
            addr = USBFS_DevAddr;
            printf("USB address %d\r\n", addr);
        }
        if( USBFS_DevEnumStatus != configured )
        {
            configured = USBFS_DevEnumStatus;
            printf("USB configured\r\n");
        }
    }
}
