/********************************** (C) COPYRIGHT *******************************
* File Name          : UART.C
* Author             : WCH
* Version            : V1.01
* Date               : 2024/01/19
* Description        : uart serial port related initialization and processing
*******************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#include "UART.h"
#include "ch32h417_usb.h"
/*******************************************************************************/
/* Variable Definition */
/* Global */
void TIM7_IRQHandler( void )__attribute__((interrupt("WCH-Interrupt-fast")));


/* The following are serial port transmit and receive related variables and buffers */
volatile UART_CTL Uart;

__attribute__ ((aligned(4))) uint8_t  UART_Tx_Buf[ DEF_UARTx_TX_BUF_LEN ];  /* USB OUT packets from the host (cdc_bridge.c) */

/* LiteForth: WCH's example bridged USB to USART4 (PF3/PF4, DMA1 channels 1
   and 2). The bridge here is to the V5F (cdc_bridge.c), so the USART, its
   DMA and its receive buffer are gone, and with them the example's test
   output on PA15: this file keeps the USB side's state (Uart) and TIM7's
   timeouts. */

/*********************************************************************
 * @fn      RCC_Configuration
 *
 * @brief   Configures the different system clocks.
 *
 * @return  none
 */
uint8_t RCC_Configuration( void )
{
    RCC_HB1PeriphClockCmd( RCC_HB1Periph_TIM7, ENABLE );
    return 0;
}

/*********************************************************************
 * @fn      TIM7_Init
 *
 * @brief   100us Timer
 *          144 * 100 * 13.8888 -----> 100uS
 *
 * @return  none
 */
void TIM7_Init( void )
{
    TIM_TimeBaseInitTypeDef  TIM_TimeBaseStructure = {0};

    TIM_DeInit( TIM7 );

    /* Time base configuration */
    TIM_TimeBaseStructure.TIM_Period = 100 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler = SystemCoreClock / 1000000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit( TIM7, &TIM_TimeBaseStructure );

    /* Clear TIM7 update pending flag */
    TIM_ClearFlag( TIM7, TIM_FLAG_Update );

    /* TIM IT enable */
    TIM_ITConfig( TIM7, TIM_IT_Update, ENABLE );

    /* Enable Interrupt */
    NVIC_EnableIRQ( TIM7_IRQn );

    /* TIM7 enable counter */
    TIM_Cmd( TIM7, ENABLE );
}

/*********************************************************************
 * @fn      UART_ParaInit
 *
 * @brief   CDC2 parameters initialization
 *          mode = 0 : Used in usb modify initialization
 *          mode = 1 : Used in default initializations
 * @return  none
 */
void UART_ParaInit( uint8_t mode )
{
    uint8_t i;

    Uart.Rx_LoadPtr = 0x00;
    Uart.Rx_DealPtr = 0x00;
    Uart.Rx_RemainLen = 0x00;
    Uart.Rx_TimeOut = 0x00;
    Uart.Rx_TimeOutMax = 30;

    Uart.Tx_LoadNum = 0x00;
    Uart.Tx_DealNum = 0x00;
    Uart.Tx_RemainNum = 0x00;
    for( i = 0; i < DEF_UARTx_TX_BUF_NUM_MAX; i++ )
    {
        Uart.Tx_PackLen[ i ] = 0x00;
    }
    Uart.Tx_Flag = 0x00;
    Uart.Tx_CurPackLen = 0x00;
    Uart.Tx_CurPackPtr = 0x00;

    Uart.USB_Up_IngFlag = 0x00;
    Uart.USB_Up_TimeOut = 0x00;
    Uart.USB_Up_Pack0_Flag = 0x00;
    Uart.USB_Down_StopFlag = 0x00;

    if( mode )
    {
        Uart.Com_Cfg[ 0 ] = (uint8_t)( DEF_UARTx_BAUDRATE );
        Uart.Com_Cfg[ 1 ] = (uint8_t)( DEF_UARTx_BAUDRATE >> 8 );
        Uart.Com_Cfg[ 2 ] = (uint8_t)( DEF_UARTx_BAUDRATE >> 16 );
        Uart.Com_Cfg[ 3 ] = (uint8_t)( DEF_UARTx_BAUDRATE >> 24 );
        Uart.Com_Cfg[ 4 ] = DEF_UARTx_STOPBIT;
        Uart.Com_Cfg[ 5 ] = DEF_UARTx_PARITY;
        Uart.Com_Cfg[ 6 ] = DEF_UARTx_DATABIT;
        Uart.Com_Cfg[ 7 ] = DEF_UARTx_RX_TIMEOUT;
    }
}


/*********************************************************************
 * @fn      UART_USB_Init
 *
 * @brief   The host set a line coding (CDC SET_LINE_CODING): start the
 *          USB side's state afresh. LiteForth: the line coding is only
 *          stored (Uart.Com_Cfg) and echoed back, since no UART is behind
 *          the port.
 *
 * @return  none
 */
void UART_USB_Init( void )
{
    UART_ParaInit( 0 );

    /* restart usb receive  */
    USBFSD->UEP2_DMA = (uint32_t)(uint8_t *)&UART_Tx_Buf[ 0 ];
    USBFSD->UEP2_RX_CTRL &= ~USBFS_UEP_R_RES_MASK;
    USBFSD->UEP2_RX_CTRL |= USBFS_UEP_R_RES_ACK;
}

/*********************************************************************
 * @fn      TIM7_IRQHandler
 *
 * @brief   This function handles TIM7 exception.
 *
 * @return  none
 */
#ifndef Core_V5F    /* LiteForth: the V5F never takes this interrupt, and its
                        machine mode runs only code in flash (V5F/User/mmode.h) */
void TIM7_IRQHandler( void )
{
    /* USB upload timeout count (cdc_bridge.c) */
    Uart.USB_Up_TimeOut++;

    /* clear status */
    TIM7->INTFR = (uint16_t)~TIM_IT_Update;
}
#endif