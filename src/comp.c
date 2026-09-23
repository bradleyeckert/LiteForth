#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include "comp.h"
#include <stdio.h> // remove for production code

int vmPush(int32_t x) {
    return vmPoke(-1, x);
}

int32_t vmPop(void) {
    return vmPeek(-1);
}

int lfSTATEfetch(void) {
    int32_t result;
    vmFetch(LF_STATE, &result);
    return result;
}

int lfSTATEstore(int state) {
    return vmStore(LF_STATE, state);
}

/*===========================================================================
* Dictionary pointer functions (F_PTRS in RAM page: dp, cp, hp, dp1, cp1, hp1
===========================================================================*/

static int cpFetch(int32_t* cp) {
    int32_t* mem = vm_memory[RAM_PAGE];
    *cp = mem[F_PTRS + 1];
    return 0;
}

static int cpStore(int32_t cp) {
    int32_t* mem = vm_memory[RAM_PAGE];
    uint32_t cp_max = mem[F_PTRS + 4];
    if (((unsigned)cp & 0x3FFFFF) >= cp_max) return ERR_DICTIONARY_OVERFLOW;
    mem[F_PTRS + 1] = cp;
    return 0;
}

// Code space is a half-cell-addressed, convert to linear address.
static uint32_t cpPC(void) {
    int32_t pc = 0;
    cpFetch(&pc);
    int lsb = (pc >> 26) & 1;
    return (pc << 1) | lsb;
}

// point to the current HERE pointer
static int32_t* herePtr(void) {
    int32_t space = 0; // d,c,h,-
    vmFetch(LF_MSPACE, &space);
    return vm_memory[RAM_PAGE + space];
}

// update the current HERE pointer
static void toHere(int32_t addr) {
    int32_t* ptr = herePtr();
    *ptr = addr;
}

/* HERE  ( -- n ) */
int lfAPI_here(void) {
    return vmPush(*herePtr());
}

/* ,  ( n -- ) */
int lfAPI_comma(void) {
    uint32_t here = *herePtr();
    int ior = vmStore(here, vmPop());
    here = vmCharPlus(here);
    toHere(here);
    return ior;
}

/*=========================================================================
* Compiling
=========================================================================*/

static const uint8_t returnOps[] = { VMU_PUSH, VMU_R, VMU_POP };

// Check if any of the slots touch the return stack
static int UsesRetStack(uint16_t inst) {
    for (int i = SLOT0_POSITION; i >= -5; i -= 5) {
        uint8_t uop;
        if (i < 0) uop = inst & LAST_SLOT_MASK;
        else uop = (inst >> i) & 0x1F;
        for (int j = 0; j < (int)sizeof(returnOps); j++) {
            if (returnOps[j] == uop) return 1;
        }
    }
    return 0;
}

static int slot = SLOT0_POSITION;
static int32_t instruction;
static int32_t lastcall = 0;

// Compile to code space. The data size is determined by the upper bits of cp.
static int commaCode(uint32_t inst) {
    int32_t cp = 0;
    int ior = cpFetch(&cp);
    if (ior) return ior;
    ior = vmStore(cp, inst);
    if (ior) return ior;
    printf("mem[%x]=%x ", cp & 0x3FFFFF, inst);
    cp = vmCharPlus(cp);
    return cpStore(cp);
    instruction = 0;
    slot = SLOT0_POSITION;
}

// Start a new instruction group, flushing the current one if needed.
static void NewInst(void) {
    if (slot != SLOT0_POSITION) commaCode(VM_UOPS | instruction);
    instruction = 0;
    lastcall = 0;
}

// Compile a 16-bit instruction
static int InstCompile(uint16_t inst) { // compile instruction
    NewInst();                          // flush any uops
    return commaCode(inst);
}

// Compile a call
static int CompCall(uint32_t addr, uint32_t aux) {
    NewInst();
    if (addr & ~VM_LIMM_MASK) commaCode(VMI_PFX + (addr >> VM_IMM_BITS));
    if ((aux & A_NO_TAIL_CALL) == 0) {
        cpFetch(&lastcall);             // used by ; for tail calls
    }
    return commaCode(VMI_CALL + (addr & VM_LIMM_MASK));
}

// Compile a 5-bit micro-op
static int CompUop(uint8_t uop) {
    uop &= 0x1F; // slots = 9, 4, -1
    int ior = 0;
    if (slot < -4) commaCode(VM_UOPS | instruction);
    if (slot < 0) { // last slot
        slot = 0;
        if (uop >= (1 << LAST_SLOT_WIDTH)) {
            ior = commaCode(VM_UOPS | instruction);
        }
    }
    instruction |= uop << slot;
    slot -= 5;
    return ior;
}

// Compile an unsigned literal
static int CompUlit(uint32_t x) { 
    NewInst();
    int ior = 0;
    vmPush(-1);
    vmPush(VMI_LIT | (x & VM_LIMM_MASK));
    x = x >> VM_LIMM_BITS;
    while (x) {
        vmPush(VMI_PFX | (x & VM_IMM_MASK));
        x = x >> VM_IMM_BITS;
    }
    while (1) {
        int32_t n = vmPop();
        if (n < 0) break;
        ior = commaCode(n);
        if (ior) return ior;
    }
    return 0;
}

// Compile a literal
int lfCompileLit(int32_t x) {
    if (x < 0) {
        CompUlit(~x);
        return CompUop(VMU_INV);
    }
    return CompUlit(x);
}

// Execute using the VM
int lfExecuteWord(const struct s_head* word) {
    if (word->aux & A_NOTHING)  return 0;
    if (word->aux & A_CONSTANT) return vmPush(word->w);
    int ior = 0;

    if (word->w & W_PRIMITIVE) {
        ior = vmRun(1, word->w & 0xFFFF, 0);
    }
    else {
        ior = vmRun(0, 0, word->w & 0x7FFFFF);
    }
    return ior;
}

// Compile a word
int lfCompileWord(const struct s_head* word) {
    if (word->aux & A_NOTHING)  return 0;
    uint32_t w = word->w;
    if (word->aux & A_CONSTANT) return lfCompileLit(w);
    int ior = 0;
    if (w & W_PRIMITIVE) {
        ior = CompUop(w >> SLOT0_POSITION); // slot 0 always compiles
        if (ior) return ior;
        if ((w & W_MACRO) == 0) return 0;
        int uop = w >> (SLOT0_POSITION - 5);
        ior = CompUop(uop);                 // a macro always has a slot 1
        uop = w & ((1 << LAST_SLOT_WIDTH) - 1);
        if (uop != VMU_NOP) CompUop(uop);   // maybe not a slot 2
        return ior;
    }
    ior = CompCall(cpPC(), word->aux);
    return ior;
}

// Compile an EXIT (or ;)
static int CompExit(void) {
    if (slot != SLOT0_POSITION) {
        if (UsesRetStack(instruction)) NewInst();
        goto ex;
    }
    if (lastcall) {
        int ior = 0;
        int32_t callInst = 0;
        vmFetch(lastcall, &callInst);
        callInst &= ~VMI_CALL; // call -> jump
        ior = vmStore(lastcall, callInst);
        lastcall = 0;
        return ior;
    }
ex: instruction |= VM_UOPS | VM_RET;
    slot = 0;
    NewInst();
    return 0;
}

static int32_t created = 0;

/* :  ( <name> -- ) */
int lfAPI_colon(void) {
    created = 0;
    lfHeader(cpPC(), A_SMUDGED);
    return lfSTATEstore(1);
}

/* exit  ( -- ) */
int lfAPI_exit(void) {
    return CompExit();
}

/* ;  ( -- ) */
int lfAPI_semicolon(void) {
    lfToHeader(0, A_SMUDGED);
    lfSTATEstore(0);
    return CompExit();
}

/* CONSTANT  ( n <name> -- ) */
int lfAPI_constant(void) {
    int32_t n = vmPop();
    return lfHeader(n, A_CONSTANT);
}

/* BITS  ( n <name> -- ) */
int lfAPI_bits(void) {
    int32_t here = *herePtr(); 
    uint32_t bits = vmPop();
    if (bits > 32) return ERR_TOO_MANY_BITS;
    int position = (here >> 22) & 0x1F;
    // align to cell if crossing cell boundaries
    if ((position + bits) > 32) {
        here = (here & ~(0x1F << 22)) + 1;
    }
    here = (here & ~(0x1F << 27)) | (bits << 27);
    int ior = lfHeader(here, A_CONSTANT);
    here = vmCharPlus(here);
    toHere(here);
    return ior;
}

/* CREATE  ( <name> -- ) */
int lfAPI_dotCreate(void) {
    lfHeader(cpPC(), 0);
    int32_t here = *herePtr();
    int ior = CompUlit(here);
    if (ior) return ior;
    cpFetch(&created);
    CompExit();
    return commaCode(-1); // leave space for patch
}

/* DOES>  ( xt -- )  immediate
* 
* CREATE compiles a literal followed by a ;.
* DOES> replaces the ; with a jump.
*/
int lfAPI_dotDoes(void) {
    int32_t cp = 0;
    cpFetch(&cp);
    uint32_t pc = (cp << 1) | (cp >> 26);
    int ior = vmStore(created, (VMI_JUMP + (pc & VM_LIMM_MASK)));
    created = 0;
    return ior;
}

/* >BODY  ( xt -- addr )  
*
* Analyzes the code created by CREATE (pfx ... lit) to extract addr.
*/
int lfAPI_toBody(void) {
    uint32_t xt = vmPop();
    uint32_t cp = (16 << 27) | (xt >> 1) | ((xt & 1) << 26);
    int32_t inst = 0;
    int32_t acc = 0;
    int ior = 0;
    int i = 4;
    while (i--) {
        ior = vmFetch(cp, &inst);   // either pfx or lit expected
        if (ior) return ior;
        cp = vmCharPlus(cp);
        if ((inst & VMI_MASK) == VMI_PFX) {
            acc = (acc << VM_IMM_BITS) | (inst & VM_IMM_MASK);
        }
        else if ((inst & 0xE000) == VMI_LIT) {
            acc = (acc << VM_LIMM_BITS) | (inst & VM_LIMM_MASK);
            return vmPush(acc);
        }
        else break;
    }
    return ERR_BODY_ON_NON_CREATE;
}
