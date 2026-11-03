# The LiteForth application component.
#
# SRC is the shared, target-independent C (VM, interpreter, sealed format,
# exfat, ...). Each src/<x>.c is pulled in through a thin lf_<x>.c wrapper in
# this directory (the same convention as the CH32H417 target), so the
# wrappers -- and the target drivers -- are added here as milestone 1 lands.

set(LF_SRC ../../../../src)

idf_component_register(
    SRCS
        app_main.c
        # --- target drivers (milestone 1; to be added) ---
        # serial_io.c          # USB Serial/JTAG console
        # flash.c              # the lfflash partition via esp_partition_*
        # lftime.c             # esp_timer / RTC / esp_restart
        # board.c              # UID from the eFuse MAC; enroll stubbed at level 0
        # --- shared src via wrappers (to be added) ---
        # lf_vm.c lf_forth.c lf_comp.c lf_iomap.c lf_api0.c lf_utils.c
        # lf_lfblocks.c lf_memalloc.c ...
        # lf_vm_rv32.S          # if options.h defines VM_ASM
    INCLUDE_DIRS
        "."
        "${LF_SRC}"
    PRIV_REQUIRES
        esp_partition
        esp_timer
        esp_system
        efuse
        # later: esp_lcd, sdmmc, fatfs, esp_wifi, bt, app_update
)
