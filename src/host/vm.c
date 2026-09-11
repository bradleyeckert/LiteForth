#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include "vm.h"
#include "vm_labels.h"

int32_t* vm_memory[8];              // pointer to data for the VM
uint32_t vm_memory_rd_limit[8];     // index limits for memory read
uint32_t vm_memory_wp_limit[8];     // index limits for memory write-protect
uint32_t vm_memory_executable[8];   // execution limit (<= vm_memory_rd_limit)

static int32_t VMapi0Call(int32_t T, int32_t n, int fn);
static int32_t VMapi1Call(int32_t T, int32_t n, int fn);

PLACE_IN_DTCM;
static const uint8_t stackeffects[32] = VM_STACKEFFECTS;

PLACE_IN_ITCM;
int32_t vmRun(int mode, uint32_t inst, int32_t data) {
    // Core Architecture Registers
    static int32_t T = 0;  // Top of Data Stack
    static int32_t PC = 0;  // Program Counter
    static int32_t R = 0;  // Top of Return Stack
    static int32_t A = 0;  // Address A
    static int32_t B = 0;  // Address B
    static int32_t X = 0;  // Scratchpad X
    static int32_t Y = 0;  // Scratchpad Y
    static int32_t lex = 0;  // Literal extension
    static int8_t  cy = 0;  // Carry
    static int8_t  sp = 0;  // Data Stack Pointer
    static int8_t  rp = 0;  // Return Stack Pointer

    // Circular Stack Memory
    static int32_t datastack[STACK_CAPACITY];
    static int32_t returnstack[STACK_CAPACITY];

    int32_t ior = 0;                        // 0 = okay

    switch (mode) {

    case 0: /* RUN 'inst' STEPS (0 = INFINITE) */ {

        uint32_t steps = inst;              // steps or `infinite` flag
        uint32_t dirty = 1;                 // mark inst as dirty

        if (steps == 0) {                   // run a word indefinitely
            VM_RDUP;                        // launch it with a terminator
            R = 0xDEADC0DE;                 // on the return stack
            PC = data;
        }

    fetch:
        if (dirty) {                        // fetch inst pair regardless
            dirty = 0;
            goto prefetch;
        }
        if (PC & 1) {                       // 2nd instruction in pair
            inst = inst >> 16;
        }
        else {
            int page;
        prefetch:
            page = PC >> 21;
            if (page > 3) {
                if (PC == (int32_t)0xDEADC0DE) return VM_ENDED_NORMALLY;
                return VM_BAD_CODE_ADDR;
            }
            int a = (PC >> 1) & 0x3FFFFF;
            if (a >= (int)vm_memory_executable[page]) return VM_BAD_CODE_ADDR;
            inst = vm_memory[page][a];
            if (!(PC & 1)) {
                inst = inst >> 16;
            }
        }
        PC++;
        int bumpa = 0;
        // Run a 16-bit instruction or instruction group using the lower half
        // of `inst`. The upper half of 'inst' is a cache for the next one.
    execute: 
        if (inst & 0x8000) { // Execute a group of 5-bit MISC instructions
            if (inst & 0x4000) {
                PC = R;
                VM_RDROP;
                dirty = 1;
            }
            int i = SLOT0_POSITION + 5;
            while (i > 0) {
                i -= 5;
                int32_t n = T;
                int uop;
                if (i < 0) uop = inst & LAST_SLOT_MASK;
                else uop = (inst >> i) & 0x1F;
                int se = stackeffects[uop];
                if (se & 1) {
                    VM_DDUP;
                }
                else if (se & 2) {
                    VM_DDROP; // `n` is now "next on stack"
                }
                int maddr = 0;

                switch (uop) {
                case VMU_NOP:
                case VMU_DUP:
                case VMU_DROP:                                      break;
                case VMU_INV:       T = ~T;                         break;
                case VMU_TWOSTAR:   T = T * 2;                      break;
                case VMU_TWODIV:    T = T / 2;                      break;
                case VMU_TWODIVC:   cy = (T >> 31);
                    T = (cy << 31) | (T >> 1);                      break;
                case VMU_PLUS: {
                    uint64_t sum;
                    sum = (uint64_t)n + (uint64_t)T;
                    T = (int32_t)sum;
                    cy = (uint8_t)(sum >> 32);
                }                                                   break;
                case VMU_XOR:       T = n ^ T;                      break;
                case VMU_AND:       T = n & T;                      break;
                case VMU_SWAP:      n = datastack[sp];
                    datastack[sp] = T;  T = n;                      break;
                case VMU_CY:        T = cy;                         break;
                case VMU_B:         T = B;                          break;
                case VMU_OVER:      n = datastack[sp];  T = n;      break;
                case VMU_PUSH:      VM_RDUP;  R = T;                break;
                case VMU_R:         T = R;                          break;
                case VMU_POP:       T = R;  VM_RDROP;               break;
                case VMU_UNEXT:     R--;
                    if (R == 0) VM_RDROP;
                    else i = SLOT0_POSITION + 5;
                    break;
                case VMU_PLUSSTAR: {
                    uint64_t sum;
                    if (A & 1) {
                        sum = (uint64_t)T + (uint64_t)n;
                    }
                    else {
                        sum = (uint64_t)T;
                    }
                    sum = (sum << 31) | (A >> 1);
                    T = (sum >> 32);
                    A = (uint32_t)sum;
                }                                                   break;
                case VMU_BSTORE:    B = n;                          break;
                case VMU_A:         T = A;                          break;
                case VMU_ASTORE:    A = n;                          break;
                case VMU_FETCHA:     maddr = A; bumpa = 0; goto memfetch;
                case VMU_FETCHAPLUS: maddr = A; bumpa = 1; goto memfetch;
                case VMU_FETCHB:     maddr = B; bumpa = 0; goto memfetch;
                case VMU_FETCHBPLUS: maddr = B; bumpa = 2; goto memfetch;

                memfetch: {
                    int bitfield_size = maddr >> 27;
                    int page = (maddr >> 19) & 7; // 8 pages of 0-7FFFF
                    uint32_t a = maddr & 0x7FFFF;
                    if (a >= vm_memory_rd_limit[page]) {
                        return VM_BAD_DATA_ADDR;
                    }
                    T = vm_memory[page][a];
                    if (bitfield_size) {
                        int bshift = (a >> 22) & 0x1F;
                        T = (T >> bshift) & ~(0xFFFFFFFF << bitfield_size);
                    }
                    if (bumpa) { goto postinc; }
                    break;
                }

                case VMU_STOREA:     maddr = A; bumpa = 0; goto memstore;
                case VMU_STOREAPLUS: maddr = A; bumpa = 1; goto memstore;
                case VMU_STOREB:     maddr = B; bumpa = 0; goto memstore;
                case VMU_STOREBPLUS: maddr = B; bumpa = 2; goto memstore;

                memstore: {
                    int bitfield_size = maddr >> 27;
                    int page = (maddr >> 19) & 7;
                    uint32_t a = maddr & 0x7FFFF;
                    if (a >= vm_memory_rd_limit[page]) { // must be below the read limit
                        return VM_BAD_DATA_ADDR;
                    }
                    if (a < vm_memory_wp_limit[page]) { // and above the write protect limit
                        return VM_BAD_DATA_ADDR;
                    }
                    if (bitfield_size) { // bit fields need a RMW operation
                        int bshift = (a >> 22) & 0x1F;
                        uint32_t mask = ~(0xFFFFFFFF << bitfield_size);
                        n = (vm_memory[page][a] & (~(mask << bshift))) | ((n >> bshift) & mask);
                    }
                    vm_memory[page][a] = n;
                    if (bumpa) { goto postinc; }
                    break;
                }
                default: break;
                }
            }
        }
        else { // inst = 0...
            int32_t imm = inst & 0x1FFF;
            int32_t immex = (lex << 13) | imm;
            if (!(inst & 0x4000)) {
                if (!(inst & 0x2000)) {     // push PC
                    VM_RDUP; R = PC;
                }
                PC = immex;                 // jump
                dirty = 1;
                lex = 0;
            }
            else {
                if (!(inst & 0x2000)) {
                    VM_DDUP; T = immex;     // literal
                    lex = 0;
                }
                else {
                    imm &= 0x1FF;           // u9
                    int32_t simm = imm;     // s9
                    if (simm & 0x100) {
                        simm |= 0xFFFFFE00;
                    }
                    switch (((inst >> 9) & 0x0F)) {
                    case VMO_LEX: lex = (lex << 9) | imm;       break;
                    case VMO_ZOO:
                        if (inst & 0x100) { VM_DDUP; }
                        switch (inst & 0x3F) {
                        case VMZ_XSTORE:  X = T;                break;
                        case VMZ_YSTORE:  Y = T;                break;
                        case VMZ_BCISYNC: R = 2;                break;
                        case VMZ_THROW:   ior = T;              break;
                        case VMZ_XFETCH:  T = X;                break;
                        case VMZ_YFETCH:  T = Y;                break;
                        default:                                break;
                        }
                        if (inst & 0x80) { VM_DDROP; }
                        break;
                    case VMO_AX: A = X + imm;                   break;
                    case VMO_BY: B = Y + imm;                   break;
                    case VMO_ZBRAN: if (T == 0) {
                        PC = PC + simm;
                    }   VM_DDROP;                               break;                 
                    case VMO_RCALL: VM_RDUP; R = PC;
                    case VMO_BRAN: PC = PC + simm;              break;
                    case VMO_PBRAN: if ((T & 0x80000000) == 0) {
                        PC = PC + simm;
                    }                                           break;
                    case VMO_NEXT:
                        R--;
                        if (R == 0) VM_RDROP;
                        else PC = PC + immex;
                        break;
                    case VMO_API0: T = VMapi0Call(T, NOS, imm); break;
                    case VMO_API1: T = VMapi1Call(T, NOS, imm); break;
                    default:                                    break;
                    }
                }
            }
        }

    postex:     /* Post-execution tests */
        if (steps) {
            steps--;
            if (steps == 0) return VM_ENDED_NORMALLY;
        }
        if (ior) return ior;
        goto fetch;

    postinc: {
        uint8_t bsize;
        uint8_t bshift;
        if (bumpa & 1) {
            bsize = (A >> 27) & 0x1F;
            if (bsize == 0) { A++; }
            else {
                bshift = ((A >> 22) & 0x1F) + bsize;
                if ((bshift + bsize) > 32) {
                    bshift = (bshift - 32) & 0x1F;
                    A++;
                }
                A = (bsize << 27) | (bshift << 22) | (A & 0x3FFFFF);
            }
            goto postex;
        }
        if (bumpa & 2) {
            bsize = (B >> 27) & 0x1F;
            if (bsize == 0) { B++; }
            else {
                bshift = ((B >> 22) & 0x1F) + bsize;
                if ((bshift + bsize) > 32) {
                    bshift = (bshift - 32) & 0x1F;
                    B++;
                }
                B = (bsize << 27) | (bshift << 22) | (B & 0x3FFFFF);
            }
        }
        goto postex;
        }
    }

    case 1: /* EXECUTE ONE INSTRUCTION GROUP 'inst' (NO CODE FETCH) */
        ior = VM_ENDED_NORMALLY;
        goto execute;

    case 2: /* WRITE TO INTERNAL REGISTER */
        if (inst <= 0x0FF) {
            switch (inst) {
            case 0: T = data; return 0;
            case 1: PC = data; return 0;
            case 2: R = data; return 0;
            case 3: A = data; return 0;
            case 4: B = data; return 0;
            case 5: X = data; return 0;
            case 6: Y = data; return 0;
            case 7: cy = data; return 0;
            case 8: sp = data & STACK_MASK; return 0;
            case 9: rp = data & STACK_MASK; return 0;
            default: return -1;
            }
        }
        else if (inst >= 0x100 && inst <= 0x1FF) {
            uint8_t index = (sp - inst) & STACK_MASK;
            datastack[index] = data;
            return 0;
        }
        else if (inst >= 0x200 && inst <= 0x2FF) {
            uint8_t index = (sp - inst) & STACK_MASK;
            returnstack[index] = data;
            return 0;
        }
        return -1;

    case 3: /* READ FROM INTERNAL REGISTER */
        if (inst <= 0x0FF) {
            switch (inst) {
            case 0: return T;
            case 1: return PC;
            case 2: return R;
            case 3: return A;
            case 4: return B;
            case 5: return X;
            case 6: return Y;
            case 7: return cy;
            case 8: return sp;
            case 9: return rp;
            default: return -1;
            }
        }
        else if (inst >= 0x100 && inst <= 0x1FF) {
            uint8_t index = (sp - inst) & STACK_MASK;
            return datastack[index];
        }
        else if (inst >= 0x200 && inst <= 0x2FF) {
            uint8_t index = (sp - inst) & STACK_MASK;
            return returnstack[index];
        }
        return -1;

    case 4: /* RESET SYSTEM STATE */
        T = 0; PC = 0; R = 0;
        A = 0; B = 0; X = 0; Y = 0;
        cy = 0; sp = 0; rp = 0;
        return 0;

    default:
        return -1;
    }
}

static int32_t VMapi0Call(int32_t T, int32_t n, int fn) {
    (void)T; (void)n;
    fn &= 0x7F;
    return -1;
}

static int32_t VMapi1Call(int32_t T, int32_t n, int fn) {
    (void)T; (void)n;
    fn &= 0x7F;
    return -1;
}
