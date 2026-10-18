/* Compiles LiteForth's src/ctr.c (software AES-128 and SM4 in CTR mode)
   into the V5F project, with CTR_HW: whole blocks go to the ECDC engine
   (ctr_hw_crypt in ecdc.c), the software being the fallback. */
#define CTR_HW 1
#include "../../../../ctr.c"
