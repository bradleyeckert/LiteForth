/* Compiles WCH's SD card driver (V5F/User/sdio.c, the V5F's copy) into the
   V3F project, so stage 1 can read lfstage.bin and write lfbackup.bin
   before the V5F starts. Polling transfers only (SD_ReadDisk, SD_WriteDisk). */
#include "../../V5F/User/sdio.c"
