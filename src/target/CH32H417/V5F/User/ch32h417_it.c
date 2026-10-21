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
#include "guard.h"                      /* LiteForth: the store emulation's checks */

void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((naked));

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
   longjmp. Anything else resets the chip as before.

   Before any of that, a store from user mode that PMP entry 2 refused
   (pmp.h) is done here instead, if it's to RCC or CRC
   (guard_store_allowed): the entry covers them only because they share
   its 16 KB with the DMA registers, and user mode writes them. The
   handler decodes the store (guard_store_decode), checks that its address
   is the one the core reported (mtval), writes the value, and returns
   past it. A store to the DMA registers, the flash controller, USBFS or
   the RNG isn't done: it's a fault as before. */
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

/* Does the user-mode store at mepc that faulted on mtval, if it's one
   guard_store_allowed lets through; returns nonzero if it did. regs holds
   the faulting code's registers. Reads the instruction a halfword at a
   time (mepc may be 2-byte aligned), from where the code was running, so
   nothing here can fault. */
static int emulate_store(const uint32_t *regs, uint32_t mepc, uint32_t mtval)
{
  uint32_t insn = *(const volatile uint16_t *)mepc;
  if ((insn & 3u) == 3u) {
    insn |= (uint32_t)*(const volatile uint16_t *)(mepc + 2) << 16;
  }
  guard_store s;
  if (guard_store_decode(insn, regs, &s) != 0 || s.addr != mtval || !guard_store_allowed(&s)) {
    return 0;
  }
  switch (s.size) {
  case 1: *(volatile uint8_t *)s.addr = (uint8_t)s.value; break;
  case 2: *(volatile uint16_t *)s.addr = (uint16_t)s.value; break;
  default: *(volatile uint32_t *)s.addr = s.value; break;
  }
  __asm volatile ("csrw mepc, %0" : : "r"(mepc + (uint32_t)s.len));
  return 1;
}

/* The handler proper, called by HardFault_Handler with the faulting
   code's registers: regs[i] is xi, regs[2] the sp it had. It returns to
   mret, or resets the chip. */
void __attribute__((used)) hardfault_c(uint32_t *regs);
void hardfault_c(uint32_t *regs)
{
  uint32_t mcause, mepc, mtval, mstatus;
  __asm volatile ("csrr %0, mcause" : "=r"(mcause));
  __asm volatile ("csrr %0, mepc" : "=r"(mepc));
  __asm volatile ("csrr %0, mtval" : "=r"(mtval));
  __asm volatile ("csrr %0, mstatus" : "=r"(mstatus));
  if (mcause == 7 && ((mstatus >> 11) & 3u) == 0      /* a store, from user mode */
      && emulate_store(regs, mepc, mtval)) {
    return;
  }
  uint32_t ra = regs[1], sp = regs[2];
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

/* The vector: saves x1-x31 below the stack pointer, with x2 as it was at
   the fault, calls hardfault_c with their address, puts them back and
   returns. Naked, because a C handler can't see the registers the
   faulting code had: the store emulation needs the one it stored. */
void HardFault_Handler(void)
{
  __asm volatile (
    "addi sp, sp, -128\n"
    "sw zero, 0(sp)\n"
    "sw x1, 4(sp)\n"
    "sw x3, 12(sp)\n"   "sw x4, 16(sp)\n"   "sw x5, 20(sp)\n"   "sw x6, 24(sp)\n"
    "sw x7, 28(sp)\n"   "sw x8, 32(sp)\n"   "sw x9, 36(sp)\n"   "sw x10, 40(sp)\n"
    "sw x11, 44(sp)\n"  "sw x12, 48(sp)\n"  "sw x13, 52(sp)\n"  "sw x14, 56(sp)\n"
    "sw x15, 60(sp)\n"  "sw x16, 64(sp)\n"  "sw x17, 68(sp)\n"  "sw x18, 72(sp)\n"
    "sw x19, 76(sp)\n"  "sw x20, 80(sp)\n"  "sw x21, 84(sp)\n"  "sw x22, 88(sp)\n"
    "sw x23, 92(sp)\n"  "sw x24, 96(sp)\n"  "sw x25, 100(sp)\n" "sw x26, 104(sp)\n"
    "sw x27, 108(sp)\n" "sw x28, 112(sp)\n" "sw x29, 116(sp)\n" "sw x30, 120(sp)\n"
    "sw x31, 124(sp)\n"
    "addi t0, sp, 128\n"
    "sw t0, 8(sp)\n"                   /* x2: the sp at the fault */
    "mv a0, sp\n"
    "call hardfault_c\n"
    "lw x1, 4(sp)\n"
    "lw x3, 12(sp)\n"   "lw x4, 16(sp)\n"   "lw x5, 20(sp)\n"   "lw x6, 24(sp)\n"
    "lw x7, 28(sp)\n"   "lw x8, 32(sp)\n"   "lw x9, 36(sp)\n"   "lw x10, 40(sp)\n"
    "lw x11, 44(sp)\n"  "lw x12, 48(sp)\n"  "lw x13, 52(sp)\n"  "lw x14, 56(sp)\n"
    "lw x15, 60(sp)\n"  "lw x16, 64(sp)\n"  "lw x17, 68(sp)\n"  "lw x18, 72(sp)\n"
    "lw x19, 76(sp)\n"  "lw x20, 80(sp)\n"  "lw x21, 84(sp)\n"  "lw x22, 88(sp)\n"
    "lw x23, 92(sp)\n"  "lw x24, 96(sp)\n"  "lw x25, 100(sp)\n" "lw x26, 104(sp)\n"
    "lw x27, 108(sp)\n" "lw x28, 112(sp)\n" "lw x29, 116(sp)\n" "lw x30, 120(sp)\n"
    "lw x31, 124(sp)\n"
    "addi sp, sp, 128\n"
    "mret\n"
  );
}
