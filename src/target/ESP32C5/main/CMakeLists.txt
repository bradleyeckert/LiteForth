# The LiteForth application component.
#
# LF_SRC is the shared, target-independent C (src/). Those files are compiled
# directly (no lf_*.c wrappers): INCLUDE_DIRS adds both this dir -- for the
# target headers and options.h -- and src/, so each src/<x>.c finds its
# headers. The set matches what the CH32H417 V5F links, minus the dual-core
# stage-1 pieces; vm.c (the portable C vmExec) is used, not vm_rv32.S, until
# VM_ASM is enabled. Milestone 1: REPL + flash; SD/LCD come later.
set(LF_SRC ../../..)

idf_component_register(
    SRCS
        # --- ESP32-C5 target drivers ---
        main.c
        serial_io.c
        flash.c
        lftime.c
        board.c
        blocks.c
        lcd.c
        # --- shared LiteForth core (src/) ---
        ${LF_SRC}/vm.c
        ${LF_SRC}/forth.c
        ${LF_SRC}/comp.c
        ${LF_SRC}/iomap.c
        ${LF_SRC}/api0.c
        ${LF_SRC}/utils.c
        ${LF_SRC}/lfblocks.c
        ${LF_SRC}/memalloc.c
        ${LF_SRC}/tools.c
        ${LF_SRC}/sealed.c
        ${LF_SRC}/ctr.c
        ${LF_SRC}/blake2s.c
        ${LF_SRC}/monocypher.c
        ${LF_SRC}/monocypher-ed25519.c
        ${LF_SRC}/ymodem.c
        ${LF_SRC}/card.c
        ${LF_SRC}/exfat.c
    INCLUDE_DIRS
        "."
        "${LF_SRC}"
    PRIV_REQUIRES
        esp_partition
        spi_flash
        driver
        esp_timer
        esp_system
        esp_hw_support
        # later: esp_lcd, esp_driver_spi, sdmmc, fatfs, esp_wifi, bt, app_update
)
