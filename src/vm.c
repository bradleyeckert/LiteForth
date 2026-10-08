#include <stdint.h>
#include <stddef.h>
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"
#include "api0.h"
#include "lftime.h"

uint32_t* vm_memory[VM_MEM_PAGES] = { NULL };
uint32_t vm_memory_rd_limit[VM_MEM_PAGES] = { 0 };
uint32_t vm_memory_wp_limit[VM_MEM_PAGES] = { 0 };
uint32_t vm_memory_executable[VM_MEM_PAGES] = { 0 };
char* vm_memory_name[VM_MEM_PAGES] = { NULL };

/*
 * The VM's registers and stacks are unsigned: cells are bit patterns, and
 * unsigned arithmetic wraps without undefined behavior. A value is cast to
 * signed only where the VM treats it as signed: sign extension, the `-if`
 * test, and iors, which are negative numbers. Memory (vm_memory) and the
 * cell values that vmFetch and vmStore pass are unsigned too.
 */
PLACE_IN_DTCM;
static uint32_t datastack[STACK_CAPACITY];
static uint32_t returnstack[STACK_CAPACITY];
static uint32_t T = 0;  // Top of Data Stack
#ifdef TWO_REGISTER_TOS
static uint32_t N = 0;  // Next on the Data Stack (datastack holds the rest)
#endif
static uint32_t PC = 0;  // Program Counter
static uint32_t R = 0;  // Top of Return Stack
static uint32_t A = 0;  // Address A
static uint32_t B = 0;  // Address B
static uint32_t U = 0;  // User pointer
static uint32_t X = 0;  // GP register
static uint32_t Y = 0;  // GP register
static uint8_t  cy = 0;  // Carry
static uint8_t  sp = 0;  // Data Stack Pointer
static uint8_t  rp = 0;  // Return Stack Pointer
static uint32_t prefix = 0;  // Literal prefix

uint32_t vmFieldPlus(uint32_t addr) {
    uint32_t bsize = (addr >> 27) & 0x1F;
    if (bsize == 0) {
        return addr + 1;
    }
    uint32_t bshift = ((addr >> 22) & 0x1F) + bsize;
    if ((bshift + bsize) > 32) {
        bshift = (bshift - 32) & 0x1F;
        addr++;
    }
    return (bsize << 27) | (bshift << 22) | (addr & 0x3FFFFF);
}

PLACE_IN_ITCM;
int vmFetch(uint32_t addr, uint32_t* data) {
    uint32_t bitfield_size = addr >> 27;
    uint32_t page = (addr >> (22 - VM_LOG2_PAGES)) & (VM_MEM_PAGES - 1);
    uint32_t a = addr & VM_PAGE_MASK;
    if (a >= vm_memory_rd_limit[page]) {
        return ERR_INVALID_ADDRESS;
    }
    uint32_t res = vm_memory[page][a];
    if (bitfield_size) {
        uint32_t bshift = (addr >> 22) & 0x1F;
        res = (res >> bshift) & ((1u << bitfield_size) - 1);
    }
    *data = res;
    return 0;
}

int vmStore(uint32_t addr, uint32_t data) {
    uint32_t bitfield_size = addr >> 27;
    uint32_t page = (addr >> (22 - VM_LOG2_PAGES)) & (VM_MEM_PAGES - 1);
    uint32_t a = addr & VM_PAGE_MASK;
    if (a >= vm_memory_rd_limit[page]) { // must be below the read limit
        return ERR_INVALID_ADDRESS;
    }
    if (a < vm_memory_wp_limit[page]) { // and above the write protect limit
        return ERR_WRITE_PROTECTED;
    }
    if (bitfield_size) { // bit fields need a RMW operation
        uint32_t bshift = (addr >> 22) & 0x1F;
        uint32_t mask = (1u << bitfield_size) - 1;
        data = ((data & mask) << bshift) |
            (vm_memory[page][a] & ~(mask << bshift));
    }
    vm_memory[page][a] = data;
    return 0;
}

/*
 * vmExec runs VM code with the VM registers in local variables, so the
 * compiler can keep them in CPU registers. VM_LOAD copies the registers in
 * and VM_SAVE copies them back: at every exit, and around calls into C that
 * use the VM state (API calls, which pop and push, and can run more VM code
 * through vmRun).
 */
static uint32_t shift_size = 0;              // set by shft[ for ]shl and ]shr

/*
 * `nos` is the second data stack item: with TWO_REGISTER_TOS a local copy
 * of N, otherwise datastack[dsp] itself. The micro-ops use it either way.
 */
#ifdef TWO_REGISTER_TOS
#define LOAD_N()  nos = N
#define SAVE_N()  N = nos
#else
#define nos       datastack[dsp]
#define LOAD_N()  ((void)0)
#define SAVE_N()  ((void)0)
#endif

#define VM_LOAD() do {                                          \
    t = T;  LOAD_N();  r = R;  a = A;  b = B;  u = U;  pc = PC; \
    dsp = sp & STACK_MASK;  rsp = rp & STACK_MASK;              \
    c = cy & 1;  pfx = prefix;                                  \
} while (0)

#define VM_SAVE() do {                                          \
    T = t;  SAVE_N();  R = r;  A = a;  B = b;  U = u;  PC = pc; \
    sp = (uint8_t)dsp;  rp = (uint8_t)rsp;                      \
    cy = (uint8_t)c;  prefix = pfx;                             \
} while (0)

// Stack moves on the local copies (VM_DDUP etc. in vm_labels.h use globals).
// DDUP makes room for a new T (the caller sets t); DDROP pops T. With
// TWO_REGISTER_TOS, t and nos are the top two items and datastack[dsp] the
// third; otherwise datastack[dsp] is the second.
#ifdef TWO_REGISTER_TOS
#define DDUP()  do { dsp = (dsp + 1) & STACK_MASK; datastack[dsp] = nos; nos = t; } while (0)
#define DDROP() do { t = nos; nos = datastack[dsp]; dsp = (dsp - 1) & STACK_MASK; } while (0)
#define NIP()   do { nos = datastack[dsp]; dsp = (dsp - 1) & STACK_MASK; } while (0)
#else
#define DDUP()  do { dsp = (dsp + 1) & STACK_MASK; datastack[dsp] = t; } while (0)
#define DDROP() do { t = datastack[dsp]; dsp = (dsp - 1) & STACK_MASK; } while (0)
#define NIP()   do { dsp = (dsp - 1) & STACK_MASK; } while (0)
#endif
#define RDUP()  do { rsp = (rsp + 1) & STACK_MASK; returnstack[rsp] = r; } while (0)
#define RDROP() do { r = returnstack[rsp]; rsp = (rsp - 1) & STACK_MASK; } while (0)

// A switch default that can't be reached: lets the compiler drop the range
// check before the jump table.
#if defined(__GNUC__)
#define UNREACHABLE() __builtin_unreachable()
#else
#define UNREACHABLE() break
#endif

// A micro-op that reads or writes memory ends its group if the access fails.
#define MEMOP(x) do { x; if (ior) slots = 0; } while (0)

#define TERMINATOR 0xDEADC0DEu          // return address that ends a word call

static int32_t vmExec(int once, uint32_t inst, uint32_t address) {
#ifdef TWO_REGISTER_TOS
    uint32_t nos;                       // N
#endif
    uint32_t t, r, a, b, u, pc, pfx;    // T, R, A, B, U, PC and prefix
    uint32_t dsp, rsp, c;               // sp, rp and cy
    VM_LOAD();

    int32_t ior = 0;                    // 0 = okay
    uint32_t steps = 0;
    // The code page being run: PC values cbase .. cbase+cspan-1 are in it and
    // executable. cspan = 0 forces a lookup. Anything outside, including the
    // 0xDEADC0DE terminator, takes the slow path in fetch.
    const uint32_t* code = NULL;
    uint32_t cbase = 0, cspan = 0;
    int slow = once;                    // check after each instruction:
                                        // once, or stepping (not word calls)

    if (once) {
        goto execute;                   // vmRun(1, inst, 0)
    }
    steps = inst;                       // vmRun(0,steps,0) or
    if (steps == 0) {                   // vmRun(0,0,address):
        RDUP();                         // run a word with a terminator
        r = TERMINATOR;                 // on the return stack
        pc = address;
    }
    slow = (steps != 0);                // (once is 0 here)

fetch:                                  // outer loop starts here...
    if (pc - cbase >= cspan) {
        if (pc == TERMINATOR) {         // hit ;
            VM_SAVE();
            return 0;
        }
        uint32_t page = pc >> (24 - VM_LOG2_PAGES);
        if (page >= VM_MEM_PAGES) {
            ior = ERR_EXEC_PROTECTED;  goto byee;
        }
        code = vm_memory[page];
        cbase = page << (24 - VM_LOG2_PAGES);
        cspan = vm_memory_executable[page] << 1;
        if (pc - cbase >= cspan) {
            ior = ERR_EXEC_PROTECTED;  goto byee;
        }
    }
    // Two instructions per cell, the even one in the lower half. Only the
    // lower 16 bits of inst are used below.
    inst = code[(pc - cbase) >> 1] >> ((pc & 1) << 4);
    pc++;
execute:
    if (inst & VM_UOPS) {
        if (inst & VM_RET) {            // return first, then the micro-ops
            pc = r;
            RDROP();
        }
        // Up to three micro-ops: bits 13:9, 8:4 and 3:0. They are lined up
        // as 5-bit fields at the top of `slots` and shifted out, so trailing
        // nops are skipped. unext runs the group again.
        uint32_t slots;
    group:
        slots = ((inst << 18) & 0xFFC00000u) | ((inst & LAST_SLOT_MASK) << 17);
        do {
            uint32_t n;
            uint32_t uop = slots >> 27;
            slots <<= 5;
            switch (uop) {
            case VMU_NOP:                                           break;
            case VMU_INV:       t = ~t;                             break;
            case VMU_OVER:      n = nos;  DDUP();  t = n;           break;
            case VMU_ASTORE:    n = t;  DDROP();  a = n;            break;
            case VMU_XOR:       n = t;  DDROP();  t ^= n;           break;
            case VMU_PLUS: {    // cy = carry out of bit 31
                uint32_t sum = t + nos;
                c = sum < t;
                NIP();
                t = sum;
            }                                                       break;
            case VMU_AND:       n = t;  DDROP();  t &= n;           break;
            case VMU_PUSH:      n = t;  DDROP();  RDUP();  r = n;   break;
            case VMU_UNEXT:
                if (--r) goto group;    // run the group again
                RDROP();                                            break;
            case VMU_TWOSTAR:   t <<= 1;                            break;
            case VMU_DUP:       DDUP();                             break;
            case VMU_DROP:      DDROP();                            break;
            case VMU_FETCHA:    DDUP();  MEMOP(ior = vmFetch(a, &n));  t = n;  break;
            case VMU_FETCHAPLUS:
                DDUP();  MEMOP(ior = vmFetch(a, &n));  t = n;  a = vmFieldPlus(a);  break;
            case VMU_R:         DDUP();  t = r;                     break;
            case VMU_POP:       DDUP();  t = r;  RDROP();           break;
            case VMU_TWODIVC: { // rotate right through carry
                uint32_t v = t;
                t = (c << 31) | (v >> 1);
                c = v & 1;
            }                                                       break;
            case VMU_TWODIV:    // arithmetic shift right
                t = (t >> 1) | (t & 0x80000000u);
                break;
            case VMU_FETCHASIGN: {  // @a, sign-extending a slice
                DDUP();
                MEMOP(ior = vmFetch(a, &n));
                uint32_t bsize = a >> 27;
                if (bsize) {
                    uint32_t sign = 1u << (bsize - 1);
                    if (n & sign) n |= (~(sign - 1));   // set the bits above it
                }
                t = n;
            }                                                       break;
            case VMU_USTORE:    n = t;  DDROP();  u = n;            break;
            case VMU_STOREA:    n = t;  DDROP();  MEMOP(ior = vmStore(a, n));  break;
            case VMU_STOREAPLUS:
                n = t;  DDROP();  MEMOP(ior = vmStore(a, n));  a = vmFieldPlus(a);  break;
            case VMU_STOREB:    n = t;  DDROP();  MEMOP(ior = vmStore(b, n));  break;
            case VMU_STOREBPLUS:
                n = t;  DDROP();  MEMOP(ior = vmStore(b, n));  b = vmFieldPlus(b);  break;
            case VMU_SWAP:
                n = nos;  nos = t;  t = n;                          break;
            case VMU_PLUSSTAR: { // multiply step: T:A >> 1, adding N if A odd
                uint64_t sum = t;           // the adder inputs are T and N
                if (a & 1) {
                    sum += nos;             // result = carry:sum[31:0]:a
                }
                t = (uint32_t)(sum >> 1);
                a = ((uint32_t)sum << 31) | (a >> 1);
            }                                                       break;
            case VMU_B:         DDUP();  t = b;                     break;
            case VMU_BSTORE:    n = t;  DDROP();  b = n;            break;
            case VMU_FETCHB:    DDUP();  MEMOP(ior = vmFetch(b, &n));  t = n;  break;
            case VMU_FETCHBPLUS:
                DDUP();  MEMOP(ior = vmFetch(b, &n));  t = n;  b = vmFieldPlus(b);  break;
            case VMU_A:         DDUP();  t = a;                     break;
            case VMU_CY:        DDUP();  t = c;                     break;
            default:            UNREACHABLE();  // all 32 are cases
            }
        } while (slots);
    } else if (!(inst & 0x4000)) {      // jump or call
        uint32_t immex = (pfx << 13) | (inst & 0x1FFF);
        if (inst & 0x2000) {            // call: push PC
            RDUP();  r = pc;
        }
        pc = immex;
        pfx = 0;
    } else if (!(inst & 0x2000)) {      // literal
        DDUP();
        t = (pfx << 13) | (inst & 0x1FFF);
        pfx = 0;
    } else {                            // instructions with 9-bit immediate data
        uint32_t imm = inst & VM_IMM_MASK;  // u9
        switch ((inst >> VM_IMM_BITS) & 0x0F) {
        case VMO_ZBRAN: {
            uint32_t tos = t;
            DDROP();
            if (tos == 0) goto branch;
        } break;
        case VMO_BRAN:
        branch:                         // add the s9 offset
            pc += (uint32_t)((int32_t)(inst << (32 - VM_IMM_BITS)) >> (32 - VM_IMM_BITS));
            break;
        case VMO_PBRAN:
            if ((int32_t)t >= 0) goto branch;
            break;
        case VMO_RCALL: RDUP();  r = pc;
            goto branch;
        case VMO_NEXT:
            if (--r) goto branch;
            RDROP();  break;
        case VMO_SYS:
            switch (imm) {
            case VMS_SHR: t >>= shift_size;  break;
            case VMS_SHL: t <<= shift_size;  break;
            case VMS_FIELDPLUS: t = vmFieldPlus(t);  break;
            case VMS_GETUSEC: {
                uint64_t usec = lfGetTimeMicroSec();
                Y = (uint32_t)(usec >> 32);
                X = (uint32_t)usec;
                break;
            }
            case VMS_TASK: // ]task
                dsp = t & STACK_MASK;
                rsp = (t >> 16) & STACK_MASK;
                break;
            case VMS_BREAK:
                lfWatchdogPing();   // the app is alive: it reached a `break`
                ior = ERR_VM_BREAK;
                break;
            default: break;
            } break;
        case VMO_TOSYS: {
            uint32_t tos = t;
            DDROP();
            switch (imm) {
            case VMSTO_YEET: if (tos) ior = (int32_t)tos;  break;
            case VMSTO_SHIFT:
                shift_size = tos & 0x1F; break;
            default: break;
            }
        } break;
        case VMO_USER: a = u + imm;  break;
        case VMO_FROMSYS:
            DDUP();
            switch (imm) {
            case VMSFROM_TASK:  // task[
                RDUP();
                t = (rsp << 16) | dsp;
                break;
            case VMSFROM_X: t = X; break;
            case VMSFROM_Y: t = Y; break;
            default: break;
            } break;
        case VMO_PFX:
            pfx = (pfx << (VM_IMM_BITS + 1)) | imm; break;
        case VMO_PFX1:
            pfx = (pfx << (VM_IMM_BITS + 1)) | imm | (1 << VM_IMM_BITS); break;
        case VMO_API0:
            if (g_lf_sys_options & SYS_OPTION_ONLY_TERM) {
                if (imm > API_T_TXQ) {
                    ior = ERR_NO_API_CALL_ALLOWED;
                    break;
                }
            }
            FALLTHROUGH;
        case VMO_API1:
            if (g_lf_sys_options & SYS_OPTION_NO_API) {
                ior = ERR_NO_API_CALL_ALLOWED;
                break;
            }
            VM_SAVE();                  // the API sees and changes VM state,
            ior = (((inst >> VM_IMM_BITS) & 0x0F) == VMO_API0)
                ? VMapi0Call(imm) : VMapi1Call(imm);
            VM_LOAD();                  // and may run more VM code
            cspan = 0;                  // memory may have been remapped
            break;
        case 5:
        case 7:
        case 11:  ior = ERR_INVALID_OPCODE;  break;
        default: UNREACHABLE();         // all 16 are cases
        }
    }

    if ((ior | slow) == 0) goto fetch;   // the usual case
    if (steps) {
        steps--;
        if (steps == 0) {
            VM_SAVE();
            return ERR_VM_TIMEOUT;
        }
    }
byee:
    if (ior) {
        if ((ior != ERR_VM_BREAK) && (steps)) {
            // The app is being stepped: send it to its yeet handler.
            // (A word run by the terminal has steps == 0, so its errors
            // only go back to the terminal and the PC is left alone.)
            X = pc;
            Y = (uint32_t)ior;
            pc = VM_YEET_ADDRESS << 1;
        }
        VM_SAVE();
        return ior;
    }
    if (once == 0) goto fetch;
    VM_SAVE();
    return ior;
}

#ifndef TWO_REGISTER_TOS
#undef nos
#endif

/*
 * vmRun (documented in vm.h).
 * Calling a word can re-enter vmRun: an API call such as LOAD interprets a
 * block, which executes more words. The nested call leaves PC at the
 * 0xDEADC0DE terminator, so the caller's PC is saved here and put back.
 * (The caller's vmExec refetches its instruction pair after an API call.)
 * The caller's PC is put back even after an error: the terminal's errors
 * must not disturb the app. (vmExec sends a stepped app's errors to its
 * yeet handler.)
 */
int32_t vmRun(int once, uint32_t inst, int32_t address) {
    if (once || inst) {
        return vmExec(once, inst, (uint32_t)address);
    }
    uint32_t pc = PC;
    int32_t ior = vmExec(0, 0, (uint32_t)address);
    PC = pc;
    return ior;
}

// API access to internal VM state

int32_t vmPeek(int reg) {
    uint32_t x;
    if (reg < 0) {
        x = T;
        VM_DDROP;
        return (int32_t)x;
    }
    switch (reg) {
        case 0:         x = T;  break;
#ifdef TWO_REGISTER_TOS
        case 1:         x = N;  break;
#endif
        case VM_REG_PC: x = PC; break;
        case VM_REG_R : x = R;  break;
        case VM_REG_A : x = A;  break;
        case VM_REG_B : x = B;  break;
        case VM_REG_U : x = U;  break;
        case VM_REG_cy: x = cy; break;
        case VM_REG_sp: x = sp; break;
        case VM_REG_rp: x = rp; break;
        default:
        if (reg < STACK_MASK) {
            x = datastack[(sp + TOS_REGISTERS - reg) & STACK_MASK];
            break;
        }   return -1;
    }
    return (int32_t)x;
}

int32_t vmPoke(int reg, int32_t value) {
    uint32_t data = (uint32_t)value;
    if (reg < 0) {
        reg = 0;
        VM_DDUP;
    }
    switch (reg) {
        case 0:         T = data; break;
#ifdef TWO_REGISTER_TOS
        case 1:         N = data; break;
#endif
        case VM_REG_PC: PC = data; break;
        case VM_REG_R : R = data; break;
        case VM_REG_A : A = data; break;
        case VM_REG_B : B = data; break;
        case VM_REG_U : U = data; break;
        case VM_REG_cy: cy = data & 1; break;
        case VM_REG_sp: sp = data & STACK_MASK; break;
        case VM_REG_rp: rp = data & STACK_MASK; break;
        default:
        if (reg < STACK_MASK) {
            datastack[(sp + TOS_REGISTERS - reg) & STACK_MASK] = data;
            break;
        }   return -1;
    }
    return 0;
}

int32_t vmReset(void) {
    for (int i = VM_REG_PC; i <= VM_REG_rp; i++) {
        vmPoke(i, 0);
    }
    R = TERMINATOR;    // empty return stack marker
    T = VM_EMPTYSTACK; // empty data stack markers
#ifdef TWO_REGISTER_TOS
    N = VM_EMPTYSTACK;
#endif
    return 0;
}

