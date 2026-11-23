/********************************** (C) COPYRIGHT *******************************
* File Name          : UART.H
* Author             : WCH
* Version            : V1.01
* Date               : 2024/01/19
* Description        : UART communication-related headers
*******************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#ifndef __UART_H__
#define __UART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdio.h"
#include "string.h"
#include "ch32h417_conf.h"
#include "string.h"
#include "ch32h417_usbfs_device.h"

/******************************************************************************/
/* Related macro definitions */
/* Serial buffer related definitions */
#define DEF_UARTx_TX_BUF_LEN       ( 2 * 512 )                                  /* Serial x transmit buffer size */
#define DEF_USB_FS_PACK_LEN        DEF_USBD_FS_PACK_SIZE                        /* USB full speed mode packet size for serial x data */
#define DEF_UARTx_TX_BUF_NUM_MAX   ( DEF_UARTx_TX_BUF_LEN / DEF_USB_FS_PACK_LEN ) /* Serial x transmit buffer size */

/* Serial port receive timeout related macro definition */
#define DEF_UARTx_BAUDRATE         115200                                       /* Default baud rate for serial port */
#define DEF_UARTx_STOPBIT          0                                            /* Default stop bit for serial port */
#define DEF_UARTx_PARITY           0                                            /* Default parity bit for serial port */
#define DEF_UARTx_DATABIT          8                                            /* Default data bit for serial port */
#define DEF_UARTx_RX_TIMEOUT       30                                           /* Serial port receive timeout, in 100uS */
#define DEF_UARTx_USB_UP_TIMEOUT   60000                                        /* Serial port receive upload timeout, in 100uS */

/* LiteForth: no USART behind the port (UART.c), so no USART4 or DMA here */
/************************************************************/
/* Serial port X related structure definition */
typedef struct __attribute__((packed)) _UART_CTL
{
    uint16_t Rx_LoadPtr;                                                         /* Serial x data receive buffer load pointer */
    uint16_t Rx_DealPtr;                                                         /* Pointer to serial x data receive buffer processing */
    volatile uint16_t Rx_RemainLen;                                              /* Remaining unprocessed length of the serial x data receive buffer */
    uint8_t  Rx_TimeOut;                                                         /* Serial x data receive timeout */
    uint8_t  Rx_TimeOutMax;                                                      /* Serial x data receive timeout maximum */

    volatile uint16_t Tx_LoadNum;                                                /* Serial x data send buffer load number */
    volatile uint16_t Tx_DealNum;                                                /* Serial x data send buffer processing number */
    volatile uint16_t Tx_RemainNum;                                              /* Serial x data send buffer remaining unprocessed number */
    volatile uint16_t Tx_PackLen[ DEF_UARTx_TX_BUF_NUM_MAX ];                    /* The current packet length of the serial x data send buffer */
    uint8_t  Tx_Flag;                                                            /* Serial x data send status */
    uint8_t  Recv1;
    uint16_t Tx_CurPackLen;                                                      /* The current packet length sent by serial port x */
    uint16_t Tx_CurPackPtr;                                                      /* Pointer to the packet currently being sent by serial port x */

    uint8_t  USB_Up_IngFlag;                                                     /* Serial xUSB packet being uploaded flag */
    uint8_t  Recv2;
    uint16_t USB_Up_TimeOut;                                                     /* Serial xUSB packet upload timeout timer */
    uint8_t  USB_Up_Pack0_Flag;                                                  /* Serial xUSB data needs to upload 0-length packet flag */
    uint8_t  USB_Down_StopFlag;                                                  /* Serial xUSB packet stop down flag */

    uint8_t  Com_Cfg[ 8 ];                                                       /* Serial x parameter configuration (default baud rate is 115200, 1 stop bit, no parity, 8 data bits) */
    uint8_t  Recv3;
    uint8_t  USB_Int_UpFlag;                                                     /* Serial x interrupt upload status */
    uint16_t USB_Int_UpTimeCount;                                                /* Serial x interrupt upload timing */
}UART_CTL, *PUART_CTL;

/***********************************************************************************************************************/
/* Constant, variable extents */
/* The following are serial port transmit and receive related variables and buffers */
extern volatile UART_CTL Uart;                                                    /* Serial x control related structure */
extern __attribute__ ((aligned(4))) uint8_t UART_Tx_Buf[ DEF_UARTx_TX_BUF_LEN ]; /* USB OUT packets from the host */

/***********************************************************************************************************************/
/* Function extensibility */
extern uint8_t RCC_Configuration( void );
extern void TIM7_Init( void );
extern void UART_ParaInit( uint8_t mode );                                       /* The USB side's state (Uart): mode 1 also sets the default line coding */
extern void UART_USB_Init( void );                                               /* After SET_LINE_CODING: the state afresh, EP2 rearmed */
#ifdef __cplusplus
}
#endif

#endif

/***********************************************************************************************************************/


