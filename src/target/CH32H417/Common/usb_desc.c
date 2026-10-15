/********************************** (C) COPYRIGHT *******************************
 * File Name          : usb_desc.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2025/05/26
 * Description        : usb device descriptor,configuration descriptor,
 *                      string descriptors and other descriptors.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#include "usb_desc.h"

/* Device Descriptor */
const uint8_t  MyDevDescr[] =
{
    0x12,       // bLength
    0x01,       // bDescriptorType (Device)
    0x10, 0x01, // bcdUSB 1.10
    0x02,       // bDeviceClass
    0x00,       // bDeviceSubClass
    0x00,       // bDeviceProtocol
    DEF_USBD_UEP0_SIZE,   // bMaxPacketSize0 64
    (uint8_t)DEF_USB_VID, (uint8_t)(DEF_USB_VID >> 8),  // idVendor 0x1A86
    (uint8_t)DEF_USB_PID, (uint8_t)(DEF_USB_PID >> 8),  // idProduct 0xFE0C
    DEF_IC_PRG_VER, 0x00, // bcdDevice 0.01
    0x01,       // iManufacturer (String Index)
    0x02,       // iProduct (String Index)
    0x03,       // iSerialNumber (String Index): LiteForth, the chip's UID
    0x01,       // bNumConfigurations 1
};

/* Configuration Descriptor */
const uint8_t  MyCfgDescr[] =
{
    /* Configure descriptor */
    0x09, 0x02, 0x43, 0x00, 0x02, 0x01, 0x00, 0x80, 0x32,

    /* Interface 0 (CDC) descriptor */
    0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01,  0x00,

    /* Functional Descriptors */
    0x05, 0x24, 0x00, 0x10, 0x01,

    /* Length/management descriptor (data class interface 1) */
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x02,
    0x05, 0x24, 0x06, 0x00, 0x01,

    /* Interrupt upload endpoint descriptor */
    0x07, 0x05, 0x81, 0x03, (uint8_t)DEF_USBD_ENDP1_SIZE, (uint8_t)( DEF_USBD_ENDP1_SIZE >> 8 ), 0x01,

    /* Interface 1 (data interface) descriptor */
    0x09, 0x04, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,

    /* Endpoint descriptor */
    0x07, 0x05, 0x02, 0x02, (uint8_t)DEF_USBD_ENDP2_SIZE, (uint8_t)( DEF_USBD_ENDP2_SIZE >> 8 ), 0x00,

    /* Endpoint descriptor */
    0x07, 0x05, 0x83, 0x02, (uint8_t)DEF_USBD_ENDP3_SIZE, (uint8_t)( DEF_USBD_ENDP3_SIZE >> 8 ), 0x00,
};

/* Language Descriptor */
const uint8_t  MyLangDescr[] =
{
    0x04, 0x03, 0x09, 0x04
};

/* Manufacturer Descriptor */
const uint8_t  MyManuInfo[ ] =
{
    0x0E, 
    0x03, 
    'w', 0, 
    'c', 0, 
    'h', 0, 
    '.', 0, 
    'c', 0, 
    'n', 0
};

/* Product Information */
const uint8_t MyProdInfo[ ]  =
{
    0x12, 
    0x03, 
    'C', 0, 
    'H', 0, 
    '3', 0, 
    '2', 0, 
    'H', 0, 
    '4', 0, 
    '1', 0, 
    '7', 0
};

/* LiteForth: Serial Number Information, 16 hex digits of the chip's
 * unique ID (ESIG UNIID1 and UNIID2), filled in by USB_SerialInit before
 * USB starts. Until then bLength is 0 and the descriptor is empty. The
 * reference manual calls the ID 96 bits, but UNIID3 reads 0xE339E339,
 * erased flash, so only the first 64 bits are used. */
uint8_t MySerNumInfo[ 2 + 2 * DEF_USBD_SN_DIGITS ];

void USB_SerialInit( void )
{
    static const char hex[ ] = "0123456789ABCDEF";
    uint32_t id[ 2 ];
    int i;

    id[ 0 ] = *(volatile uint32_t *)DEF_ESIG_UNIID1;
    id[ 1 ] = *(volatile uint32_t *)( DEF_ESIG_UNIID1 + 4 );
    for( i = 0; i < DEF_USBD_SN_DIGITS; i++ )
    {
        /* UNIID1 first, each word most significant digit first */
        MySerNumInfo[ 2 + 2 * i ] = hex[ ( id[ i / 8 ] >> ( 28 - 4 * ( i % 8 ) ) ) & 0xF ];
        MySerNumInfo[ 3 + 2 * i ] = 0;
    }
    MySerNumInfo[ 1 ] = 0x03;
    MySerNumInfo[ 0 ] = sizeof MySerNumInfo;
}
