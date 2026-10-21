/* blocksim: the little of WCH's ch32h417.h (and its SDIO, DMA and RCC
   headers) that the V5F's blocks.c and gdma.c use, so they build unchanged
   on the desktop. Not WCH's code: the names, register layouts and values,
   copied from SRC/Peripheral/inc/. The registers are at the chip's own
   addresses, which sim.c maps (Linux), so gdma.c's channel table, which
   holds &SDIO->FIFO, is a constant as it is on the board. */
#ifndef SIM_CH32H417_H
#define SIM_CH32H417_H

#include <stdint.h>
#include <stddef.h>

#define __IO volatile
#define __I volatile const
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef volatile uint8_t vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;
typedef enum { RESET = 0, SET = !RESET } FlagStatus, ITStatus;
typedef enum { DISABLE = 0, ENABLE = !DISABLE } FunctionalState;
typedef enum { NoREADY = 0, READY = !NoREADY } ErrorStatus;

#define PERIPH_BASE           ((uint32_t)0x40000000)
#define HBPERIPH_BASE         (PERIPH_BASE)
#define SDIO_BASE             (HBPERIPH_BASE + 0x18000)
#define DMA1_BASE             (HBPERIPH_BASE + 0x20000)
#define DMA2_BASE             (HBPERIPH_BASE + 0x20400)
#define DMA2_Channel1_BASE    (HBPERIPH_BASE + 0x20408)
#define DMAMUX_BASE           (HBPERIPH_BASE + 0x20800)
#define RCC_BASE              (HBPERIPH_BASE + 0x21000)
#define FLASH_R_BASE          (HBPERIPH_BASE + 0x22000)
#define CRC_BASE              (HBPERIPH_BASE + 0x23000)
#define RNG_BASE              (HBPERIPH_BASE + 0x23C00)

/* SDIO */
typedef struct {
    __IO uint32_t POWER;
    __IO uint32_t CLKCR;
    __IO uint32_t ARG;
    __IO uint32_t CMD;
    __I uint32_t RESPCMD;
    __I uint32_t RESP1;
    __I uint32_t RESP2;
    __I uint32_t RESP3;
    __I uint32_t RESP4;
    __IO uint32_t DTIMER;
    __IO uint32_t DLEN;
    __IO uint32_t DCTRL;
    __IO uint32_t DCOUNT;               /* read-only on the chip; sim.c sets it */
    __IO uint32_t STA;                  /* likewise */
    __IO uint32_t ICR;
    __IO uint32_t MASK;
    uint32_t RESERVED0[2];
    __I uint32_t FIFOCNT;
    uint32_t RESERVED1[5];
    __IO uint32_t DCTRL2;
    uint32_t RESERVED2[7];
    __IO uint32_t FIFO;
} SDIO_TypeDef;
#define SDIO                  ((SDIO_TypeDef *)SDIO_BASE)

typedef struct {
    uint32_t SDIO_Argument;
    uint32_t SDIO_CmdIndex;
    uint32_t SDIO_Response;
    uint32_t SDIO_Wait;
    uint32_t SDIO_CPSM;
} SDIO_CmdInitTypeDef;

typedef struct {
    uint32_t SDIO_DataTimeOut;
    uint32_t SDIO_DataLength;
    uint32_t SDIO_DataBlockSize;
    uint32_t SDIO_TransferDir;
    uint32_t SDIO_TransferMode;
    uint32_t SDIO_DPSM;
} SDIO_DataInitTypeDef;

#define SDIO_Response_Short                 ((uint32_t)0x00000040)
#define SDIO_Wait_No                        ((uint32_t)0x00000000)
#define SDIO_CPSM_Enable                    ((uint32_t)0x00000400)
#define SDIO_DataBlockSize_512b             ((uint32_t)0x00000090)
#define SDIO_TransferDir_ToCard             ((uint32_t)0x00000000)
#define SDIO_TransferDir_ToSDIO             ((uint32_t)0x00000002)
#define SDIO_TransferMode_Block             ((uint32_t)0x00000000)
#define SDIO_DPSM_Enable                    ((uint32_t)0x00000001)
#define SDIO_FLAG_CCRCFAIL                  ((uint32_t)0x00000001)
#define SDIO_FLAG_DCRCFAIL                  ((uint32_t)0x00000002)
#define SDIO_FLAG_CTIMEOUT                  ((uint32_t)0x00000004)
#define SDIO_FLAG_DTIMEOUT                  ((uint32_t)0x00000008)
#define SDIO_FLAG_TXUNDERR                  ((uint32_t)0x00000010)
#define SDIO_FLAG_RXOVERR                   ((uint32_t)0x00000020)
#define SDIO_FLAG_CMDREND                   ((uint32_t)0x00000040)
#define SDIO_FLAG_CMDSENT                   ((uint32_t)0x00000080)
#define SDIO_FLAG_DATAEND                   ((uint32_t)0x00000100)
#define SDIO_FLAG_STBITERR                  ((uint32_t)0x00000200)

void SDIO_SendCommand(SDIO_CmdInitTypeDef *cmd);
void SDIO_DataConfig(SDIO_DataInitTypeDef *data);
void SDIO_ClearFlag(uint32_t flag);

/* DMA and DMAMUX */
typedef struct {
    __IO uint32_t CFGR;
    __IO uint32_t CNTR;
    __IO uint32_t PADDR;
    __IO uint32_t MADDR;
    __IO uint32_t M1ADDR;
} DMA_Channel_TypeDef;

typedef struct {
    __IO uint32_t INTFR;
    __IO uint32_t INTFCR;               /* write 1 to clear: sim.c applies it */
} DMA_TypeDef;

typedef struct {
    __IO uint32_t CFGR0_3;
    __IO uint32_t CFGR4_7;
    __IO uint32_t CFGR8_11;
    __IO uint32_t CFGR12_15;
} DMAMUX_TypeDef;

#define DMA2                  ((DMA_TypeDef *)DMA2_BASE)
#define DMA2_Channel1         ((DMA_Channel_TypeDef *)DMA2_Channel1_BASE)
#define DMAMUX                ((DMAMUX_TypeDef *)DMAMUX_BASE)

#define DMA_CFGR1_EN                        ((uint16_t)0x0001)
#define DMA_DIR_PeripheralDST               ((uint32_t)0x00000010)
#define DMA_DIR_PeripheralSRC               ((uint32_t)0x00000000)
#define DMA_PeripheralInc_Disable           ((uint32_t)0x00000000)
#define DMA_MemoryInc_Enable                ((uint32_t)0x00000080)
#define DMA_PeripheralDataSize_Word         ((uint32_t)0x00000200)
#define DMA_MemoryDataSize_Word             ((uint32_t)0x00000800)
#define DMA_Mode_Normal                     ((uint32_t)0x00000000)
#define DMA_Priority_High                   ((uint32_t)0x00002000)
#define DMA_M2M_Disable                     ((uint32_t)0x00000000)
#define DMA2_FLAG_GL1                       ((uint32_t)0x00000001)
#define DMA2_FLAG_TC1                       ((uint32_t)0x00000002)
#define DMA2_FLAG_HT1                       ((uint32_t)0x00000004)
#define DMA2_FLAG_TE1                       ((uint32_t)0x00000008)
#define DMA_MuxChannel9                     ((uint8_t)0x08)

/* RCC: only as far as HBPCENR */
typedef struct {
    __IO uint32_t CTLR;
    __IO uint32_t CFGR0;
    __IO uint32_t PLLCFGR;
    __IO uint32_t INTR;
    __IO uint32_t HB2PRSTR;
    __IO uint32_t HB1PRSTR;
    __IO uint32_t HBPCENR;
} RCC_TypeDef;
#define RCC                   ((RCC_TypeDef *)RCC_BASE)
#define RCC_HBPeriph_DMA1     ((uint32_t)0x00000001)
#define RCC_HBPeriph_DMA2     ((uint32_t)0x00000002)

/* Interrupts */
typedef enum { SDIO_IRQn = 1 } IRQn_Type;
void NVIC_DisableIRQ(IRQn_Type irq);

#endif
