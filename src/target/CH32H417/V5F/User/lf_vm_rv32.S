/* Assembles LiteForth's src/vm_rv32.S, the RV32 assembly vmExec, into the
   V5F project when options.h defines VM_ASM (vm.c then calls it). Its
   includes find the core headers beside it in src/, and options.h here
   (V5F/User is on the assembler's include path). */
#include "options.h"
#ifdef VM_ASM
#include "../../../../vm_rv32.S"
#endif
