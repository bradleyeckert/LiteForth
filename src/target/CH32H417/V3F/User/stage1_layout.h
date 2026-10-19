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

#endif /* STAGE1_LAYOUT_H */
