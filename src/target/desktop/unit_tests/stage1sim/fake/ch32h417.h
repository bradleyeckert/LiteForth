/* stage1sim: the little of WCH's ch32h417.h (and the peripheral headers)
   that the V3F's stage 1 and CDC bridge use, backed by sim.c. Not WCH's
   code: just the names, so that the real stage1.c and cdc_bridge.c build
   on the desktop. */
#ifndef SIM_CH32H417_H
#define SIM_CH32H417_H

#include <stdint.h>
#include <stddef.h>

#define __IO volatile
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef enum { RESET = 0, SET = !RESET } FlagStatus, ITStatus;
typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;

/* FLASH: the option bytes as loaded at reset, and the code flash */
typedef struct {
    __IO uint32_t KEYR;
    __IO uint32_t OBKEYR;
    __IO uint32_t STATR;                /* never busy */
    __IO uint32_t CTLR;
    __IO uint32_t OBR;
    __IO uint32_t WPR;
} FLASH_TypeDef;
extern FLASH_TypeDef sim_flash_regs;
#define FLASH               (&sim_flash_regs)
#define FLASH_OBR_RDPRT     ((uint16_t)0x0002)
#define FLASH_STATR_BSY     ((uint8_t)0x01)
#define FLASH_CTLR_OPTPG    ((uint32_t)0x00000010)
#define FLASH_CTLR_OPTER    ((uint32_t)0x00000020)
#define FLASH_CTLR_STRT     ((uint32_t)0x00000040)
#define FLASH_CTLR_LOCK     ((uint32_t)0x00000080)
#define OB_USBFSDL_EN       ((uint16_t)0x0040)
#define OB_USARTDL_EN       ((uint16_t)0x0080)
typedef enum {
    FLASH_BUSY = 1, FLASH_ERROR_PG, FLASH_ERROR_WRP, FLASH_COMPLETE, FLASH_TIMEOUT,
    FLASH_RDP, FLASH_OP_RANGE_ERROR = 0xFD, FLASH_ALIGN_ERROR = 0xFE, FLASH_ADR_RANGE_ERROR = 0xFF,
} FLASH_Status;
/* The option bytes themselves (OB_BASE, in the UID page), which `lockdown`
   programs, and which load into OBR and WPR at the next boot (sim.c keeps
   them in ob.bin) */
typedef struct {
    __IO uint16_t RDPR;
    __IO uint16_t USER;
    __IO uint16_t Data0;
    __IO uint16_t Data1;
    __IO uint16_t WRPR0;
    __IO uint16_t WRPR1;
    __IO uint16_t WRPR2;
    __IO uint16_t WRPR3;
} OB_TypeDef;
#define OB_BASE             0x1FFFF800u
#define OB                  ((OB_TypeDef *)(uintptr_t)OB_BASE)
FLASH_Status FLASH_ROM_ERASE(uint32_t StartAddr, uint32_t Length);
FLASH_Status FLASH_ROM_WRITE(uint32_t StartAddr, uint32_t *pbuf, uint32_t Length);

/* RCC and the RNG */
#define RCC_HBPeriph_RNG    0x1u
void RCC_HBPeriphClockCmd(uint32_t periph, FunctionalState state);
#define RNG_FLAG_DRDY       0x1u
#define RNG_FLAG_CECS       0x2u
#define RNG_FLAG_SECS       0x4u
void RNG_Cmd(FunctionalState state);
FlagStatus RNG_GetFlagStatus(uint32_t flag);
uint32_t RNG_GetRandomNumber(void);

/* Interrupts and resets */
typedef enum { SDIO_IRQn = 1, USBFS_IRQn, TIM2_IRQn, HSEM_IRQn } IRQn_Type;
void NVIC_DisableIRQ(IRQn_Type irq);
void NVIC_EnableIRQ(IRQn_Type irq);
void NVIC_SystemReset(void) __attribute__((noreturn));
void __disable_irq(void);

/* The debug UART (USART1) */
typedef struct { int unused; } USART_TypeDef;
extern USART_TypeDef sim_usart1;
#define USART1              (&sim_usart1)
#define USART_FLAG_TC       0x40u
FlagStatus USART_GetFlagStatus(USART_TypeDef *u, uint16_t flag);

/* Power */
#define PWR_Regulator_ON    0u
#define PWR_STOPEntry_WFI   1u
void PWR_EnterSTOPMode(uint32_t regulator, uint8_t entry);

/* Hardware semaphores between the cores */
typedef enum { HSEM_ID0 = 0, HSEM_ID1, HSEM_ID2 } HSEM_ID_TypeDef;
uint32_t HSEM_FastTake(HSEM_ID_TypeDef id);
void HSEM_ReleaseOneSem(HSEM_ID_TypeDef id, uint32_t process);
FlagStatus HSEM_GetFlagStatus(HSEM_ID_TypeDef id);

#endif
