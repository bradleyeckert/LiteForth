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
#include "../../../../vm.h"              /* LiteForth: vmTrap */
#include "../../../../errcodes.h"

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
/* LiteForth: say where the fault was on USART1 (the debug port). Writes
   the UART directly, not through printf, which takes a hardware semaphore
   the fault may have interrupted.

   A fault during a VM run (vmTrapArmed) goes back to Forth instead of
   resetting the chip: the handler sets mepc to trap_resume and returns,
   so mret leaves the handler and continues there, as ordinary code, which
   longjmps to the innermost vmRun (vmTrap in vm.c). A stepped app gets its
   yeet handler, a word the terminal runs gets an error, and the terminal
   carries on. That's for the exceptions an `@` or `!` in Forth can cause,
   such as reading a peripheral that isn't clocked or isn't there (the I/O
   page at SECURITY_LEVEL 0 maps every address), and only when no
   interrupt is active, since an interrupt handler can't be left by a
   longjmp. Anything else resets the chip as before. */
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

static volatile int32_t trap_ior;       /* for trap_resume */

static void __attribute__((noreturn, used)) trap_resume(void)
{
  vmTrap(trap_ior);
}

/* The ior for an exception Forth can get back, else 0 */
static int32_t trap_to_ior(uint32_t mcause)
{
  switch (mcause) {
  case 0:                               /* instruction address misaligned */
  case 4:                               /* load address misaligned */
  case 6: return ERR_ADDRESS_ALIGNMENT; /* store address misaligned */
  case 1:                               /* instruction access fault */
  case 5:                               /* load access fault */
  case 7: return ERR_BUS_FAULT;         /* store access fault */
  case 2: return ERR_CPU_EXCEPTION;     /* illegal instruction */
  default: return 0;                    /* interrupts, ebreak, ecall, ... */
  }
}

/* Nonzero if an interrupt other than this exception (and NMI) is active */
static int other_irq_active(void)
{
  uint32_t a = PFIC->IACTR[0] & ~((1u << NonMaskableInt_IRQn) | (1u << HardFault_IRQn));
  for (int i = 1; i < 8; i++) {
    a |= PFIC->IACTR[i];
  }
  return a != 0;
}

void HardFault_Handler(void)
{
  uint32_t mcause, mepc, mtval, ra, sp;
  __asm volatile ("csrr %0, mcause" : "=r"(mcause));
  __asm volatile ("csrr %0, mepc" : "=r"(mepc));
  __asm volatile ("csrr %0, mtval" : "=r"(mtval));
  __asm volatile ("mv %0, ra" : "=r"(ra));
  __asm volatile ("mv %0, sp" : "=r"(sp));
  int32_t ior = trap_to_ior(mcause);
  int forth = ior && vmTrapArmed() && !other_irq_active();
  crumb_log_t *c = CRUMBS;              /* a crumb by hand: no calls in here */
  if (c->magic == CRUMB_MAGIC) {
    crumb_t *e = &c->e[c->next % CRUMB_COUNT];
    e->tag = forth ? TAG('T','R','P','5') : TAG('F','L','T','5');
    e->a = mepc;
    e->b = mcause;
    e->t = 0;
    c->next = c->next + 1;
  }
  fault_puts("\r\nV5F fault:");
  fault_hex(" mcause ", mcause);
  fault_hex(" mepc ", mepc);
  fault_hex(" mtval ", mtval);
  fault_hex(" ra ", ra);
  fault_hex(" sp ", sp);
  fault_puts(forth ? " -> Forth\r\n" : "\r\n");
  if (forth) {
    trap_ior = ior;
    __asm volatile ("csrw mepc, %0" : : "r"((uint32_t)trap_resume));
    return;                             /* mret: on to trap_resume */
  }
  while ((USART1->STATR & USART_FLAG_TC) == 0) {
  }
  NVIC_SystemReset();
  while (1)
  {
  }
}
