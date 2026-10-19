#ifndef STAGE1_LAYOUT_H
#define STAGE1_LAYOUT_H

/* Where stage 1's update code (src/update.c) works in the code flash, as
   offsets from its start (0x08000000), before update.h's defaults.

   For now the flash map stays as it is (doc/firmware-updates.md section 2
   proposes moving things): the V3F's image in the first 64 KB, the V5F's
   in the next 128 KB (Link_v5f.ld), LiteForth's flash pages at 0x30000,
   the V5F's board record at 0x70000. The security record's two copies go
   in the next two free 8 KB sectors. */
#define UPD_SECTOR          8192u
#define UPD_V5F_ADDR        0x10000u
#define UPD_V5F_SLOT        0x20000u
#define UPD_RECORD_A        0x72000u
#define UPD_RECORD_B        0x74000u

#define STAGE1_FLASH_BASE   0x08000000u     /* the code flash, for reading and FLASH_ROM_* */

/* Write protection as a locked-down board has it (stage1.c's check; WPR has
   one bit per 8 KB sector, sectors 31-119 sharing bit 31, and a 0 means
   protected): the V3F's own image, below the V5F's slot, protected, and
   everything that updates and LiteForth write (the V5F slot, LiteForth's
   flash, the records, the board record) writable. */
#define STAGE1_WRP_PROTECT  ((1u << (UPD_V5F_ADDR / UPD_SECTOR)) - 1u)
#define STAGE1_WRP_WRITABLE (~STAGE1_WRP_PROTECT)

#endif /* STAGE1_LAYOUT_H */
