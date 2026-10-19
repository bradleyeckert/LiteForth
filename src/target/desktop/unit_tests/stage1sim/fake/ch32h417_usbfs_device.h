/* stage1sim: the state of WCH's USB device driver that Common/cdc_bridge.c
   shares with the USB interrupt. sim.c plays the interrupt: host bytes
   into the slots of UART_Tx_Buf, and endpoint 3's packets out to stdout. */
#ifndef SIM_USBFS_DEVICE_H
#define SIM_USBFS_DEVICE_H

#include "debug.h"

#define DEF_USBD_FS_PACK_SIZE       64
#define DEF_USB_FS_PACK_LEN         64
#define DEF_UARTx_TX_BUF_NUM_MAX    16

typedef struct {
    volatile uint16_t Tx_PackLen[DEF_UARTx_TX_BUF_NUM_MAX];
    volatile uint16_t Tx_DealNum;
    volatile uint16_t Tx_RemainNum;
    volatile uint16_t Tx_CurPackPtr;
    volatile uint8_t USB_Down_StopFlag;
    volatile uint8_t USB_Up_IngFlag;
    volatile uint16_t USB_Up_TimeOut;
    volatile uint8_t USB_Up_Pack0_Flag;
} UART_CTL;
extern UART_CTL Uart;
extern uint8_t UART_Tx_Buf[DEF_UARTx_TX_BUF_NUM_MAX * DEF_USB_FS_PACK_LEN];
extern uint8_t USBFS_EP3_Buf[DEF_USBD_FS_PACK_SIZE];

typedef struct {
    volatile uint8_t UEP2_RX_CTRL;
    volatile uint16_t UEP3_TX_LEN;
    volatile uint8_t UEP3_TX_CTRL;
} USBFSD_TypeDef;
extern USBFSD_TypeDef sim_usbfsd;
#define USBFSD                  (&sim_usbfsd)
#define USBFS_UEP_R_RES_MASK    0x0Cu
#define USBFS_UEP_R_RES_ACK     0x00u
#define USBFS_UEP_R_RES_NAK     0x08u
#define USBFS_UEP_T_RES_MASK    0x03u
#define USBFS_UEP_T_RES_ACK     0x00u
#define USBFS_UEP_T_RES_NAK     0x02u
extern volatile uint8_t USBFS_DevEnumStatus;
void USBFS_Device_Init(FunctionalState state);

#endif
