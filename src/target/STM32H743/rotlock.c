#include "stm32h7xx_hal.h"

// Destination address must be perfectly 32-byte aligned 
// Place this inside Sector 0, but away from your executable code
#define PROVISIONED_KEY_ADDR    0x08007E00 
#define SEC_0_WRP_SECTORS       OB_WRP_SECTOR_0 // Target sector 0 for protection

extern RNG_HandleTypeDef hrng; // Assumes CubeMX initiated Hardware TRNG

void Check_And_Self_Provision_RoT(void) {
    uint32_t *key_ptr = (uint32_t *)PROVISIONED_KEY_ADDR;
    uint8_t is_blank = 1;

    // 1. Check if the 32-byte (8 x 32-bit words) flash word is unprogrammed (all 0xFF)
    for (int i = 0; i < 8; i++) {
        if (key_ptr[i] != 0xFFFFFFFF) {
            is_blank = 0; // Key already exists, proceed to normal secure boot
            break;
        }
    }

    if (is_blank) {
        uint32_t random_token[8]; // 32-byte buffer

        // 2. Generate 32 bytes of entropy via internal Hardware TRNG
        for (int i = 0; i < 8; i++) {
            if (HAL_RNG_GenerateRandomNumber(&hrng, &random_token[i]) != HAL_OK) {
                // Handle TRNG failure gracefully (e.g., infinite loop/fault handler)
                Error_Handler(); 
            }
        }

        // 3. Program the 32-byte Flash Word
        HAL_FLASH_Unlock();
        
        // STM32H7 mandatory flash-word write configuration
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, PROVISIONED_KEY_ADDR, (uint32_t)random_token) != HAL_OK) {
            Error_Handler();
        }

        // 4. Configure Option Bytes to write-protect Sector 0
        FLASH_OBProgramInitTypeDef OBInit;
        HAL_FLASH_OB_Unlock();

        // Get current configurations to avoid overwriting unrelated option bytes
        HAL_FLASHEx_OBGetConfig(&OBInit);

        OBInit.OptionType = OPTIONBYTE_WRP;
        OBInit.WRPSector = SEC_0_WRP_SECTORS;
        OBInit.WRPState = OB_WRPSTATE_ENABLE;
        OBInit.Banks = FLASH_BANK_1;

        if (HAL_FLASHEx_OBProgram(&OBInit) != HAL_OK) {
            Error_Handler();
        }

        // 5. Force the hardware reload. 
        // WARNING: This macro forces an immediate chip System Reset.
        // Upon reboot, Sector 0 is permanently write-protected.
        HAL_FLASH_OB_Launch(); 
        
        // Execution will never reach here due to reset
        while(1);
    }
}
