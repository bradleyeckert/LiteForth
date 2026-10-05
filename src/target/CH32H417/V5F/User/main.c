/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2025/05/26
 * Description        : Main program body for V5F.
 *********************************************************************************
 * Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "debug.h"
#include "hardware.h"
#include "serial_io.h"

/*********************************************************************
 * USB CDC echo test
 *
 * The V3F runs the USB device; the V5F talks to the host through the
 * shared rings via serial_io. Each byte typed in the terminal is echoed,
 * and Enter starts a new line with a prompt that shows how many bytes the
 * V5F counted on the line before, so the reply visibly comes from the V5F.
 */
static void put_str(const char *s)
{
    while (*s) serial_putc(*s++);
}

static void put_dec(uint32_t n)
{
    char buf[11];
    int i = 0;
    do {
        buf[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i) serial_putc(buf[--i]);
}

static void Echo(void)
{
    uint32_t count = 0;
    int last = 0;

    serial_open(NULL, 0);
    put_str("\r\nV5F echo test over USB CDC\r\nV5F> ");
    while (1)
    {
        int c = serial_getc();
        if (c == '\n' && last == '\r') {
            last = c;                   /* CRLF: the CR already did it */
            continue;
        }
        last = c;
        if (c == '\r' || c == '\n') {
            put_str("\r\n(");
            put_dec(count);
            put_str(" bytes) V5F> ");
            count = 0;
        } else {
            serial_putc((char)c);
            count++;
        }
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
    SystemAndCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(9600);
    printf("V5F SystemCoreClk:%d\r\n", SystemCoreClock);
    
#if (Run_Core == Run_Core_V3FandV5F)
    HSEM_FastTake(HSEM_ID0);
    HSEM_ReleaseOneSem(HSEM_ID0, 0);    /* wake the V3F, which runs USB */
    printf("V5F released HSEM0, running echo\r\n");
    Echo();

#elif (Run_Core == Run_Core_V3F)

#elif (Run_Core == Run_Core_V5F)
    Hardware();
#endif

    while(1)
    {
        ;
    }
}
