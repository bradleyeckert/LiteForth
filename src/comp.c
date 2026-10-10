#include "forth.h"
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "serial_io.h"
#include "options.h"
#include "tools.h"
#include "comp.h"
#include "api0.h"
//#include <stdio.h> // remove

// globals
uint32_t lfCreatedName; // used by api0.c

/*===========================================================================
* Dictionary pointer functions (F_PTRS in RAM page)
===========================================================================*/

static int cpFetch(uint32_t* cp) {
    uint32_t* mem = vm_memory[RAM_PAGE];
    *cp = mem[F_PTRS_CP];
    return 0;
}

static int cpStore(uint32_t cp) {
    uint32_t* mem = vm_memory[RAM_PAGE];
    uint32_t cp_max = mem[F_PTRS_CP + 1];
    if (((unsigned)cp & 0x3FFFFF) >= cp_max) return ERR_DICTIONARY_OVERFLOW;
    mem[F_PTRS_CP] = cp;
    return 0;
}

// Code space is a half-cell-addressed, convert to linear address.
static uint32_t cpPC(void) {
    uint32_t cp = 0;
    cpFetch(&cp);
    cp = lfSetSliceWidth(cp, 16);
    cpStore(cp); // enforce 16-bit addressing
    int lsb = (cp >> 26) & 1;
    return ((cp & 0x3FFFFF) << 1) | lsb;
}

/*=========================================================================
* Compiling
=========================================================================*/

static const uint8_t returnOps[] = { VMU_PUSH, VMU_R, VMU_POP, VMU_UNEXT };

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
static uint32_t instruction;
static uint32_t lastcall = 0;

static void freshSlots(void) {
    instruction = 0;
    slot = SLOT0_POSITION;
}

// Compile to code space. The data size is determined by the upper bits of cp.
// cp is assumed to address 16-bit words.
static int commaCode(uint32_t inst) {
    uint32_t cp = 0;
    int ior = cpFetch(&cp);
    if (ior) return ior;
    ior = vmStore(cp, inst);
    if (ior) return ior;
    cp = vmFieldPlus(cp);
    return cpStore(cp);
}

// Start a new instruction group, flushing the current one if needed.
static void NewInst(void) {
    if (slot != SLOT0_POSITION) {
        commaCode(VM_UOPS | instruction);
    }
    freshSlots();
    lastcall = 0;
}

int lfAPI_newinst(void) {
    NewInst();
    return 0;
}

// Compile a 16-bit instruction
int lfAPI_inst(void) {
    NewInst(); // flush any uops
    return commaCode(vmPop());
}

// Compile a call
static int CompCall(uint32_t xt) {
    NewInst();
    uint32_t notail = (xt & W_NO_TAIL_CALL);
    xt &= 0x7FFFFF;
    if (xt & ~VM_LIMM_MASK) {           // address needs an extension
        commaCode(VMI_PFX + (xt >> VM_IMM_BITS));
        xt &= VM_LIMM_MASK;
    }
    if (notail == 0) {
        cpFetch(&lastcall);             // used by ; for tail calls
    }
    return commaCode(VMI_CALL + (xt & VM_LIMM_MASK));
}

// Compile a 5-bit micro-op
static int CompUop(uint8_t uop) {
    uop &= 0x1F; // slots = 9, 4, -1
    int ior = 0;
    if (slot < -4) NewInst(); // no room left
    if (slot < 0) { // last slot
        slot = 0;
        if (uop >= (1 << LAST_SLOT_WIDTH)) {
            ior = commaCode(VM_UOPS | instruction);
            freshSlots();
        }
    }
    instruction |= uop << slot; // 9, 4, or 0
    slot -= 5;
    return ior;
}

// Compile an unsigned literal
static int CompUlit(uint32_t x) { 
    NewInst();
    int ior = 0;
    vmPush(-1); // terminator
    vmPush(VMI_LIT | (x & VM_LIMM_MASK));
    x = x >> VM_LIMM_BITS;
    while (x) {
        if (x & (1 << VM_IMM_BITS)) {
            vmPush(VMI_PFX1 | (x & VM_IMM_MASK));
        }
        else {
            vmPush(VMI_PFX | (x & VM_IMM_MASK));
        }
        x = x >> 10;
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

int lfAPI_literal(void) {
    return lfCompileLit(vmPop());
}


// Execute using the VM, assume xt is not a constant
int lfExecuteXT(uint32_t xt) {
    if (xt & W_PRIMITIVE) {
        return vmRun(1, xt & 0xFFFF, 0);
    }
    return vmRun(0, 0, xt & 0x7FFFFF);
}

// Execute using the VM
int lfExecuteWord(const struct s_head* word) {
    if (word->aux & A_UNRESOLVED) return ERR_UNRESOLVED_LATER; // no code yet
    if (word->aux & A_CONSTANT) return vmPush(word->w);
    return lfExecuteXT(word->w);
}

// Compile a word
static int lfCompileXT(uint32_t w) {
    int ior = 0;
    if (w & W_PRIMITIVE) {
        if (w & W_WIDE_INST) {
            NewInst();
            return commaCode(w);
        }
        else {
            ior = CompUop(w >> SLOT0_POSITION); // slot 0 always compiles
            if (ior) return ior;
            if ((w & W_MACRO) == 0) return 0;
            int uop = w >> (SLOT0_POSITION - 5);
            ior = CompUop(uop);                 // a macro always has a slot 1
            uop = w & ((1 << LAST_SLOT_WIDTH) - 1);
            if (uop != VMU_NOP) CompUop(uop);   // maybe not a slot 2
            return ior;
        }
    }
    ior = CompCall(w);
    return ior;
}
int lfCompileWord(const struct s_head* word) {
    uint32_t w = word->w;
    if (word->aux & A_CONSTANT) return lfCompileLit(w);
    return lfCompileXT(word->w);
}


// Compile an EXIT (or ;)
static int CompExit(void) {
    if (slot != SLOT0_POSITION) {
        if (UsesRetStack(instruction)) NewInst();
        goto ex;
    }
    if (lastcall) {
        int ior = 0;
        uint32_t callInst = 0;
        vmFetch(lastcall, &callInst);
        callInst &= ~VMI_CALL; // call -> jump
        ior = vmStore(lastcall, callInst);
        lastcall = 0;
        return ior;
    }
ex: instruction |= VM_UOPS | VM_RET;
    slot = -10;
    NewInst();
    return 0;
}

/*=========================================================================
* Words
=========================================================================*/

static int noname = 0;  // the definition being compiled has no header (:noname)

// The 16-bit slot address of code address pc (see cpPC)
static int32_t pcSlot(uint32_t pc) {
    return (int32_t)((16u << 27) | ((pc & 1) << 26) | (pc >> 1));
}

// Store a jump to code address pc at slot, with a prefix in the slot before
// it if pc needs one. Two slots are needed when it does.
static int storeJump(int32_t slot, uint32_t pc) {
    if (pc >= VM_LIMM_MASK) { // 10-bit pfx + 13-bit imm = 32-bit code addr
        int inst = (pc & (1 << 22)) ? VMI_PFX1 : VMI_PFX;
        int ior = vmStore(slot, (inst + ((pc >> VM_LIMM_BITS) & VM_IMM_MASK)));
        if (ior) return ior;
        slot = vmFieldPlus(slot); // next 16-bit slot
    }
    return vmStore(slot, (VMI_JUMP + (pc & VM_LIMM_MASK)));
}

/* :  ( <name> -- ) */
int lfAPI_colon(void) {
    freshSlots();               // start a new instruction
    noname = 0;
    const struct s_head* label = lfParseLabel();
    if (label != NULL) {        // resolve a `label`: jump from it to here
        int ior = storeJump(pcSlot(label->w), cpPC());
        if (ior) return ior;
        ((struct s_head*)label)->aux &= ~A_UNRESOLVED;
        noname = 1;             // no new header for ; to reveal
    } else {
        lfHeader(cpPC(), A_SMUDGED, NULL);
    }
    return lfSTATEstore(1);
}

// An instruction the VM rejects: opcode 5 of the 9-bit-immediate group is
// unused, so vmExec returns ERR_INVALID_OPCODE.
#define VMI_INVALID  (VMI_OTHER + (5 << VM_IMM_BITS))

/* LABEL  ( <name> -- ) */
int lfAPI_label(void) {
    NewInst();                  // nothing pending, no tail call to patch
    int ior = lfHeader(cpPC(), A_UNRESOLVED, NULL);
    if (ior) return ior;
    // Room for a jump, with a prefix if needed. Until `:` resolves the label,
    // calling it from code runs an invalid opcode (-105) instead.
    ior = commaCode(VMI_INVALID);
    if (ior) return ior;
    return commaCode(VMI_INVALID);
}

/* :NONAME  ( -- xt ) */
int lfAPI_noname(void) {
    freshSlots();               // start a new instruction
    noname = 1;
    int ior = vmPush((int32_t)cpPC());
    if (ior) return ior;
    return lfSTATEstore(1);
}

/* exit  ( -- ) */
int lfAPI_exit(void) {
    return CompExit();
}

/* ;  ( -- ) */
int lfAPI_semicolon(void) {
    if (!noname) {          // reveal the word; :noname made no header
        int ior = lfToHeader(0, A_SMUDGED);
        if (ior) return ior;
    }
    noname = 0;
    lfSTATEstore(0);
    lfCreatedName = 0; // WORDLIST not used yet
    return CompExit();
}

/* CONSTANT  ( n <name> -- ) */
int lfAPI_constant(void) {
    int32_t n = vmPop();
    return lfHeader(n, A_CONSTANT, NULL);
}

// point to the current HERE pointer
static uint32_t* herePtr(void) {
    uint32_t space = 0; // ud,id,c,h
    vmFetch(LF_MSPACE, &space);
    return &vm_memory[RAM_PAGE][F_PTRS + (space << 1)];
}

/* BITS  ( n <name> -- ) */
int lfAPI_bits(void) {
    uint32_t* ptr = herePtr(); 
    int32_t here = *ptr;
    int32_t bits = vmPop();
    if ((bits < 1) || (bits > 32)) return ERR_TOO_MANY_BITS;
    here = lfSetSliceWidth(here, bits);
    int ior = lfHeader(here, A_CONSTANT, NULL);
    here = vmFieldPlus(here);
    *ptr = here;
    return ior;
}

/* CREATE  ( <name> -- ) */
int lfAPI_dotCreate(void) {
    freshSlots();           // start a new instruction
    int ior = lfHeader(cpPC(), 0, &lfCreatedName);
    if (ior) return ior;
    uint32_t here = *herePtr();
    ior = CompUlit(here);
    if (ior) return ior;
	uint32_t cp = 0;
	cpFetch(&cp);           // tag the created word with the current code pointer, for DOES> to patch
	vmStore(LF_CREATED, cp);
    CompExit();
    return commaCode(0);    // leave space for patch
}

/* BIT  ( addr n -- addr' ) 
 * Change the the slice size and align it to the next slice position
 */
int lfAPI_bit(void) {
    int32_t bits = vmPop();
    int32_t addr = vmPop();
    return vmPush(lfSetSliceWidth(addr, bits));
}

/* POSTPONE  ( <name> -- )  */
int lfAPI_postpone(void) {
    const struct s_head* word = lfTickWord();
    if (word == NULL) return ERR_UNDEFINED_WORD;
    if (word->aux & A_CONSTANT) return ERR_POSTPONING_CONSTANT;
    int ior = 0;
    if (word->aux & A_IMMEDIATE) {
        ior = lfCompileWord(word);
    }
    else {
        ior = CompUlit(word->w);
        commaCode(W_PRIMITIVE | VMI_API0 | API_COMPILE);
    }
    return ior;
}

// COMPILE  ( xt -- )
int lfAPI_compile(void) {
    return lfCompileXT(vmPop());
}
