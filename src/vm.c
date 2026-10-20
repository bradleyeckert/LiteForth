#include <stdint.h>
#include <stddef.h>
#ifndef VM_NO_TRAPS
#include <setjmp.h>
#endif
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
uint32_t vm_io_page = VM_MEM_PAGES;     // the guarded page: none
const vm_io_range* vm_io_ranges = NULL;

/*
 * The VM's registers and stacks are unsigned: cells are bit patterns, and
 * unsigned arithmetic wraps without undefined behavior. A value is cast to
 * signed only where the VM treats it as signed: sign extension, the `-if`
 * test, and iors, which are negative numbers. Memory (vm_memory) and the
 * cell values that vmFetch and vmStore pass are unsigned too.
 */
// All the VM state, checked against the VMS_* offsets that vm_rv32.S uses
PLACE_IN_DTCM;
vm_state_t lf_vm;

#define VMS_CHECK(field, offset) \
    typedef char check_##field[(offsetof(vm_state_t, field) == (offset)) ? 1 : -1]
VMS_CHECK(T, VMS_T);
VMS_CHECK(N, VMS_N);
VMS_CHECK(PC, VMS_PC);
VMS_CHECK(R, VMS_R);
VMS_CHECK(A, VMS_A);
VMS_CHECK(B, VMS_B);
VMS_CHECK(U, VMS_U);
VMS_CHECK(X, VMS_X);
VMS_CHECK(Y, VMS_Y);
VMS_CHECK(prefix, VMS_PREFIX);
VMS_CHECK(shift_size, VMS_SHIFT);
VMS_CHECK(sp, VMS_SP);
VMS_CHECK(rp, VMS_RP);
VMS_CHECK(cy, VMS_CY);
VMS_CHECK(datastack, VMS_DSTACK);
VMS_CHECK(returnstack, VMS_RSTACK);

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

/*
 * Past a page's read limit: on the guarded page, the allowlist decides.
 * Returns 0 if the access (VM_IO_READ or VM_IO_WRITE) is allowed.
 */
static int vmGuarded(uint32_t page, uint32_t a, uint32_t need) {
    if (page != vm_io_page) return ERR_INVALID_ADDRESS;
    for (const vm_io_range* r = vm_io_ranges; r->end; r++) {
        if ((a >= r->end) || (a < r->first)) continue;
        if (r->flags & need) return 0;
        return (need == VM_IO_WRITE) ? ERR_WRITE_PROTECTED : ERR_INVALID_ADDRESS;
    }
    return ERR_INVALID_ADDRESS;
}

PLACE_IN_ITCM;
int vmFetch(uint32_t addr, uint32_t* data) {
    uint32_t bitfield_size = addr >> 27;
    uint32_t page = (addr >> (22 - VM_LOG2_PAGES)) & (VM_MEM_PAGES - 1);
    uint32_t a = addr & VM_PAGE_MASK;
    if (a >= vm_memory_rd_limit[page]) {
        int ior = vmGuarded(page, a, VM_IO_READ);
        if (ior) {
            *data = 0;                  // a failed @ leaves 0 in T
            return ior;
        }
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
        int ior = vmGuarded(page, a, VM_IO_WRITE);
        if (ior) return ior;
    } else if (a < vm_memory_wp_limit[page]) { // and above the write protect limit
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
 *
 * vm_rv32.S is the same function in RV32 assembly (vmExecAsm, used when
 * VM_ASM is defined). A change here needs the same change there:
 * unit_tests/vmasm runs both on random code and compares the results.
 */

/*
 * `nos` is the second data stack item: with TWO_REGISTER_TOS a local copy
 * of N, otherwise datastack[dsp] itself. The micro-ops use it either way.
 */
#ifdef TWO_REGISTER_TOS
#define LOAD_N()  nos = lf_vm.N
#define SAVE_N()  lf_vm.N = nos
#else
#define nos       lf_vm.datastack[dsp]
#define LOAD_N()  ((void)0)
#define SAVE_N()  ((void)0)
#endif

#define VM_LOAD() do {                                          \
    t = lf_vm.T;  LOAD_N();  r = lf_vm.R;  pc = lf_vm.PC;       \
    a = lf_vm.A;  b = lf_vm.B;  u = lf_vm.U;                    \
    dsp = lf_vm.sp & STACK_MASK;  rsp = lf_vm.rp & STACK_MASK;  \
    c = lf_vm.cy & 1;  pfx = lf_vm.prefix;                      \
} while (0)

#define VM_SAVE() do {                                          \
    lf_vm.T = t;  SAVE_N();  lf_vm.R = r;  lf_vm.PC = pc;       \
    lf_vm.A = a;  lf_vm.B = b;  lf_vm.U = u;                    \
    lf_vm.sp = dsp;  lf_vm.rp = rsp;                            \
    lf_vm.cy = c;  lf_vm.prefix = pfx;                          \
} while (0)

// Stack moves on the local copies (VM_DDUP etc. in vm_labels.h use lf_vm).
// DDUP makes room for a new T (the caller sets t); DDROP pops T. With
// TWO_REGISTER_TOS, t and nos are the top two items and datastack[dsp] the
// third; otherwise datastack[dsp] is the second.
#ifdef TWO_REGISTER_TOS
#define DDUP()  do { dsp = (dsp + 1) & STACK_MASK; lf_vm.datastack[dsp] = nos; nos = t; } while (0)
#define DDROP() do { t = nos; nos = lf_vm.datastack[dsp]; dsp = (dsp - 1) & STACK_MASK; } while (0)
#define NIP()   do { nos = lf_vm.datastack[dsp]; dsp = (dsp - 1) & STACK_MASK; } while (0)
#else
#define DDUP()  do { dsp = (dsp + 1) & STACK_MASK; lf_vm.datastack[dsp] = t; } while (0)
#define DDROP() do { t = lf_vm.datastack[dsp]; dsp = (dsp - 1) & STACK_MASK; } while (0)
#define NIP()   do { dsp = (dsp - 1) & STACK_MASK; } while (0)
#endif
#define RDUP()  do { rsp = (rsp + 1) & STACK_MASK; lf_vm.returnstack[rsp] = r; } while (0)
#define RDROP() do { r = lf_vm.returnstack[rsp]; rsp = (rsp - 1) & STACK_MASK; } while (0)

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

int32_t vmApiCall(uint32_t inst) {
    uint32_t imm = inst & VM_IMM_MASK;
    int api0 = (((inst >> VM_IMM_BITS) & 0x0F) == VMO_API0);
    if (api0 && (g_lf_sys_options & SYS_OPTION_ONLY_TERM) && (imm > API_T_TXQ)) {
        return ERR_NO_API_CALL_ALLOWED;
    }
    if (g_lf_sys_options & SYS_OPTION_NO_API) {
        return ERR_NO_API_CALL_ALLOWED;
    }
    return api0 ? VMapi0Call((int)imm) : VMapi1Call((int)imm);
}

// vmExec mustn't be inlined into vmRun, which calls setjmp (vmTrap): the
// compiler may keep VM state in registers across the API calls a longjmp
// can return from. (vm_rv32.S's vmExecAsm is never inlined.)
#if defined(__GNUC__)
#define VM_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define VM_NOINLINE __declspec(noinline)
#else
#define VM_NOINLINE
#endif

#ifdef VM_ASM
#define vmExec vmExecAsm                // vm_rv32.S
#else
static VM_NOINLINE int32_t vmExec(int once, uint32_t inst, uint32_t address) {
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
        uint32_t page = pc >> (23 - VM_LOG2_PAGES);
        if (page >= VM_MEM_PAGES) {
            ior = ERR_EXEC_PROTECTED;  goto byee;
        }
        code = vm_memory[page];
        cbase = page << (23 - VM_LOG2_PAGES);
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
            case VMU_PXSTORE:    DDROP();   break;
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
            case VMS_SHR: t >>= lf_vm.shift_size;  break;
            case VMS_SHL: t <<= lf_vm.shift_size;  break;
            case VMS_ASR: t = (uint32_t)((int32_t)t >> lf_vm.shift_size);  break;
            case VMS_FIELDPLUS: t = vmFieldPlus(t);  break;
            case VMS_GETUSEC: {
                uint64_t usec = lfGetTimeMicroSec();
                lf_vm.Y = (uint32_t)(usec >> 32);
                lf_vm.X = (uint32_t)usec;
                break;
            }
            case VMS_TASK: // ]task
                dsp = t & STACK_MASK;
                rsp = (t >> 16) & STACK_MASK;
#ifdef TWO_REGISTER_TOS
                nos = lf_vm.datastack[(dsp + 1) & STACK_MASK];  // N, saved by task[
#endif
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
                lf_vm.shift_size = tos & 0x1F;  break;
            case VMSTO_USTORE: u = tos;  break;
            default: break;
            }
        } break;
        case VMO_USER: a = u + imm;  break;
        case VMO_FROMSYS:
            DDUP();
            switch (imm) {
            case VMSFROM_TASK:  // task[
                RDUP();
#ifdef TWO_REGISTER_TOS
                // N goes in the cell above sp, where ]task reloads it, so
                // rp:sp captures the whole stack as with one register
                lf_vm.datastack[(dsp + 1) & STACK_MASK] = nos;
#endif
                t = (rsp << 16) | dsp;
                break;
            case VMSFROM_X: t = lf_vm.X; break;
            case VMSFROM_Y: t = lf_vm.Y; break;
            default: break;
            } break;
        case VMO_PFX:
            pfx = (pfx << (VM_IMM_BITS + 1)) | imm; break;
        case VMO_PFX1:
            pfx = (pfx << (VM_IMM_BITS + 1)) | imm | (1 << VM_IMM_BITS); break;
        case VMO_API0:
        case VMO_API1:
            VM_SAVE();                  // the API sees and changes VM state,
            ior = vmApiCall(inst);
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
            lf_vm.X = pc;
            lf_vm.Y = (uint32_t)ior;
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
#endif /* VM_ASM */

/*
 * vmRun (documented in vm.h).
 * Calling a word can re-enter vmRun: an API call such as LOAD interprets a
 * block, which executes more words. The nested call leaves PC at the
 * 0xDEADC0DE terminator, so the caller's PC is saved here and put back.
 * (The caller's vmExec refetches its instruction pair after an API call.)
 * The caller's PC is put back even after an error: the terminal's errors
 * must not disturb the app. (vmExec sends a stepped app's errors to its
 * yeet handler.)
 *
 * Each vmRun is also where a CPU exception during the run comes back to
 * (vmTrap): the innermost one returns its ior as if vmExec had, and sends
 * a stepped app to its yeet handler.
 */
static int running;                     // vmRun calls under way (vmRunning)

int vmRunning(void) {
    return running;
}

#ifdef VM_NO_TRAPS                      // no setjmp (vmasm's RV32 build)
int vmTrapArmed(void) {
    return 0;
}

int32_t vmRun(int once, uint32_t inst, int32_t address) {
    int32_t ior;
    uint32_t pc = lf_vm.PC;
    running++;
    ior = vmExec(once, inst, (uint32_t)address);
    if ((once == 0) && (inst == 0)) {
        lf_vm.PC = pc;                  // a word call: the app's PC back
    }
    running--;
    return ior;
}
#else
static jmp_buf *trap_jump;              // the innermost vmRun, NULL if none
static int32_t trap_ior;                // vmTrap's ior, for that vmRun

int vmTrapArmed(void) {
    return trap_jump != NULL;
}

void vmTrap(int32_t ior) {
    trap_ior = ior;
    longjmp(*trap_jump, 1);
}

int32_t vmRun(int once, uint32_t inst, int32_t address) {
    int32_t ior;
    uint32_t pc = lf_vm.PC;             // none of these change after setjmp,
    int depth = running;                // so they survive a longjmp
    jmp_buf here;
    jmp_buf *outer = trap_jump;
    running = depth + 1;
    trap_jump = &here;
    if (setjmp(here) == 0) {
        ior = vmExec(once, inst, (uint32_t)address);
    } else {                            // a CPU exception (vmTrap): the VM
        ior = trap_ior;                 // registers are as last saved
        if ((once == 0) && inst) {      // a stepped app: its yeet handler
            lf_vm.X = lf_vm.PC;
            lf_vm.Y = (uint32_t)ior;
            lf_vm.PC = VM_YEET_ADDRESS << 1;
        }
    }
    if ((once == 0) && (inst == 0)) {
        lf_vm.PC = pc;                  // a word call: the app's PC back
    }
    trap_jump = outer;
    running = depth;
    return ior;
}
#endif

// API access to internal VM state

int32_t vmPeek(int reg) {
    uint32_t x;
    if (reg < 0) {
        x = lf_vm.T;
        VM_DDROP;
        return (int32_t)x;
    }
    switch (reg) {
        case 0:         x = lf_vm.T;  break;
#ifdef TWO_REGISTER_TOS
        case 1:         x = lf_vm.N;  break;
#endif
        case VM_REG_PC: x = lf_vm.PC; break;
        case VM_REG_R : x = lf_vm.R;  break;
        case VM_REG_A : x = lf_vm.A;  break;
        case VM_REG_B : x = lf_vm.B;  break;
        case VM_REG_U : x = lf_vm.U;  break;
        case VM_REG_cy: x = lf_vm.cy; break;
        case VM_REG_sp: x = lf_vm.sp; break;
        case VM_REG_rp: x = lf_vm.rp; break;
        default:
        if (reg < STACK_MASK) {
            x = lf_vm.datastack[(lf_vm.sp + TOS_REGISTERS - reg) & STACK_MASK];
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
        case 0:         lf_vm.T = data; break;
#ifdef TWO_REGISTER_TOS
        case 1:         lf_vm.N = data; break;
#endif
        case VM_REG_PC: lf_vm.PC = data; break;
        case VM_REG_R : lf_vm.R = data; break;
        case VM_REG_A : lf_vm.A = data; break;
        case VM_REG_B : lf_vm.B = data; break;
        case VM_REG_U : lf_vm.U = data; break;
        case VM_REG_cy: lf_vm.cy = data & 1; break;
        case VM_REG_sp: lf_vm.sp = data & STACK_MASK; break;
        case VM_REG_rp: lf_vm.rp = data & STACK_MASK; break;
        default:
        if (reg < STACK_MASK) {
            lf_vm.datastack[(lf_vm.sp + TOS_REGISTERS - reg) & STACK_MASK] = data;
            break;
        }   return -1;
    }
    return 0;
}

int32_t vmReset(void) {
    for (int i = VM_REG_PC; i <= VM_REG_rp; i++) {
        vmPoke(i, 0);
    }
    lf_vm.R = TERMINATOR;    // empty return stack marker
    lf_vm.T = VM_EMPTYSTACK; // empty data stack markers
#ifdef TWO_REGISTER_TOS
    lf_vm.N = VM_EMPTYSTACK;
#endif
    return 0;
}

