#ifndef STAGE1_LAYOUT_H
#define STAGE1_LAYOUT_H

/* Where stage 1's update code (src/update.c) works in the code flash, as
   offsets from its start (0x08000000), before update.h's defaults
   (doc/admin/firmware-updates.md section 2):

     0x00000  64 KB   the V3F's image (stage 0 and 1)
     0x10000   8 KB   the security record, copy A (with the device token,
                      and so the board's key at level 2)
     0x12000   8 KB   copy B
     0x14000 112 KB   the V5F's slot (Link_v5f.ld's FLASH)
     0x30000 256 KB   LiteForth's flash pages
     0x70000          free (the V5F's old board record and stage 1's old
                      records were here)

   Everything the V5F mustn't reach is below the V5F's slot, under its
   PMP entry 0 (V5F/User/pmp.c). */
#define UPD_SECTOR          8192u
#define STAGE1_V3F_END      0x10000u        /* the V3F's own image ends here */
#define UPD_RECORD_A        0x10000u
#define UPD_RECORD_B        0x12000u
#define UPD_V5F_ADDR        0x14000u
#define UPD_V5F_SLOT        0x1C000u

#define UPD_PAGES_LEN       0x40000u        /* LiteForth's 4 x 64K flash pages (V5F/User/options.h) */
#define STAGE1_FLASH_BASE   0x08000000u     /* the code flash, for reading and FLASH_ROM_* */

/* A SEALED_V5F_APP package's image is the V5F's slot and then LiteForth's
   flash pages, which Link_v5f.ld puts right after it (__lf_flash_start) */
_Static_assert(UPD_V5F_ADDR + UPD_V5F_SLOT == 0x30000u, "the flash pages follow the V5F slot");
_Static_assert(UPD_RECORD_B + UPD_SECTOR == UPD_V5F_ADDR, "the records are just below the V5F's slot");
_Static_assert(UPD_V5F_ADDR % 1024u == 0, "NVIC_WakeUp_V5F takes a 1 KB aligned address");

/* Write protection as a locked-down board has it (stage1.c's check; WPR has
   one bit per 8 KB sector, sectors 31-119 sharing bit 31, and a 0 means
   protected): the V3F's own image protected, and everything that updates
   and LiteForth write (the records, the V5F slot, LiteForth's flash)
   writable. */
#define STAGE1_WRP_PROTECT  ((1u << (STAGE1_V3F_END / UPD_SECTOR)) - 1u)
#define STAGE1_WRP_WRITABLE (~STAGE1_WRP_PROTECT)

#endif /* STAGE1_LAYOUT_H */
