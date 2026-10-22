/* memalloc.c for the simulation: the V5F's options.h, but the pool in
   .bss (inside _data_vma.._susrstack, which Guarded DMA allows) rather
   than in the board's .pool section, which a Linux build doesn't have */
#include "options.h"
#undef POOL_SECTION
#include "../../../../memalloc.c"
