/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32h417_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "ch32h417_it.h"
#include "crumbs.h"                     /* LiteForth: notes that survive the reset */

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));

/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
/* LiteForth: say where the fault was on USART1 (the debug port) before
   resetting. Writes the UART directly, not through printf, which takes a
   hardware semaphore the fault may have interrupted. */
static void fault_putc(char c)
{
  while ((USART1->STATR & USART_FLAG_TXE) == 0) {
  }
  USART1->DATAR = (uint8_t)c;
}

static void fault_puts(const char *s)
{
  while (*s) {
    fault_putc(*s++);
  }
}

static void fault_hex(const char *name, uint32_t x)
{
  fault_puts(name);
  for (int i = 28; i >= 0; i -= 4) {
    fault_putc("0123456789ABCDEF"[(x >> i) & 15]);
  }
}

void HardFault_Handler(void)
{
  uint32_t mcause, mepc, mtval, ra, sp;
  __asm volatile ("csrr %0, mcause" : "=r"(mcause));
  __asm volatile ("csrr %0, mepc" : "=r"(mepc));
  __asm volatile ("csrr %0, mtval" : "=r"(mtval));
  __asm volatile ("mv %0, ra" : "=r"(ra));
  __asm volatile ("mv %0, sp" : "=r"(sp));
  crumb_v3f_note(TAG('F','L','T','3'), mepc, mcause);
  fault_puts("\r\nV3F fault:");
  fault_hex(" mcause ", mcause);
  fault_hex(" mepc ", mepc);
  fault_hex(" mtval ", mtval);
  fault_hex(" ra ", ra);
  fault_hex(" sp ", sp);
  fault_puts("\r\n");
  while ((USART1->STATR & USART_FLAG_TC) == 0) {
  }
  NVIC_SystemReset();
  while (1)
  {
  }
}


