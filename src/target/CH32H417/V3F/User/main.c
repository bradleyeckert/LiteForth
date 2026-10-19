/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2025/05/26
 * Description        : Main program body for V3F.
 *********************************************************************************
 * Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
#include "debug.h"
#include "stage1.h"                     /* LiteForth: firmware updates */
#include "hardware.h"
#include "cdc_bridge.h"
/*********************************************************************
 * @fn      main
 *
 * @brief   Main program.
 *
 * @return  none
 */
int main(void)
{
	SystemInit();
    SystemAndCoreClockUpdate();
    Delay_Init();
    USART_Printf_Init(9600);

    /* LiteForth: VIO18, the supply of the high-speed GPIOs, at 3.3 V, as the
       nanoCH32H417's SPI_LCD example sets it. The board's hardware setting
       is too low for the ST7789 on the LCD connector (PB12-15, PD8-9): SPI2
       runs but the module sees no valid highs. */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    PWR_VIO18ModeCfg(PWR_VIO18CFGMODE_SW);
    PWR_VIO18LevelCfg(PWR_VIO18Level_MODE3);

#if (Run_Core == Run_Core_V3FandV5F)
    cdc_shared_init();                  //empty the rings before the V5F uses them
    stage1_boot();                      //LiteForth: install a staged firmware update first
    printf("V3F: starting the V5F\r\n"); //if "V3F: the V5F is running" never follows, it didn't start
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET); //finish sending before STOP
	NVIC_WakeUp_V5F(Core_V5F_StartAddr);//wake up V5
	HSEM_ITConfig(HSEM_ID0, ENABLE);
    NVIC->SCTLR |= 1<<4;
	RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR,ENABLE);
	PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
	HSEM_ClearFlag(HSEM_ID0);
	printf("V3F: the V5F is running\r\n");

    Hardware();

#elif (Run_Core == Run_Core_V3F)
    Hardware();

#elif (Run_Core == Run_Core_V5F)
    NVIC_WakeUp_V5F(Core_V5F_StartAddr);//wake up V5
    PWR_EnterSTOPMode(PWR_Regulator_ON, PWR_STOPEntry_WFE);
    printf("V3F: the V5F is running\r\n");
#endif

    
    while(1)
    {
        ;
    }
}
