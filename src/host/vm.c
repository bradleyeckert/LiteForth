#include <stdint.h>
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"

int32_t* vm_memory[VM_SEGMENTS];
uint32_t vm_memory_rd_limit[VM_SEGMENTS];
uint32_t vm_memory_wp_limit[VM_SEGMENTS];
uint32_t vm_memory_executable[VM_SEGMENTS];

PLACE_IN_DTCM;
static const uint8_t stackeffects[32] = VM_STACKEFFECTS;
static int32_t datastack[STACK_CAPACITY];
static int32_t returnstack[STACK_CAPACITY];
static int32_t T = 0;  // Top of Data Stack
static int32_t PC = 0;  // Program Counter
static int32_t R = 0;  // Top of Return Stack
static int32_t A = 0;  // Address A
static int32_t B = 0;  // Address B
static int32_t X = 0;  // Scratchpad X
static int32_t Y = 0;  // Scratchpad Y
static int32_t lex = 0;  // Literal extension
static int32_t depth = 0;  // Depth counter
static int8_t  cy = 0;  // Carry
static int8_t  sp = 0;  // Data Stack Pointer
static int8_t  rp = 0;  // Return Stack Pointer

static vm_postincA(void) {
    int bsize = (A >> 27) & 0x1F;
    if (bsize == 0) { A++; }
    else {
        int bshift = ((A >> 22) & 0x1F) + bsize;
        if ((bshift + bsize) > 32) {
            bshift = (bshift - 32) & 0x1F;
            A++;
        }
        A = (bsize << 27) | (bshift << 22) | (A & 0x3FFFFF);
    }
}

static vm_postincB(void) {
    int bsize = (B >> 27) & 0x1F;
    if (bsize == 0) { B++; }
    else {
        int bshift = ((B >> 22) & 0x1F) + bsize;
        if ((bshift + bsize) > 32) {
            bshift = (bshift - 32) & 0x1F;
            B++;
        }
        B = (bsize << 27) | (bshift << 22) | (B & 0x3FFFFF);
    }
}

static int vmLitIns9(uint16_t inst, int32_t imm) {
    imm &= 0x1FF;       // u9
    int32_t simm = imm; // s9
    if (simm & 0x100) {
        simm |= ~0x100;
    }
    int imm9opcode = (inst >> 9) & 0x0F;
    switch (imm9opcode) {
    case VMO_API0: return VMapi0Call(imm); 
    case VMO_API1: return VMapi1Call(imm); 
    case VMO_LEX: lex = (lex << 9) | imm; break;
    case VMO_ZOO:
        if (inst & 0x100) { VM_DDUP; }
        switch (inst & 0x3F) {
        case VMZ_XSTORE:  X = T;   break;
        case VMZ_YSTORE:  Y = T;   break;
        case VMZ_THROW:   return T;
        case VMZ_XFETCH:  T = X;   break;
        case VMZ_YFETCH:  T = Y;   break;
        default:                   break;
        }
        if (inst & 0x80) { VM_DDROP; }
        break;
    case VMO_AX: A = X + imm;  break;
    case VMO_BY: B = Y + imm;  break;
    case VMO_ZBRAN: {
        int32_t t = T;
        VM_DDROP;
        if (t == 0) goto qbranch;
    } break;
    case VMO_RCALL: VM_RDUP; R = PC;
    case VMO_BRAN:
    qbranch:
        PC = PC + simm; break;
    case VMO_PBRAN:
        if ((T & 0x80000000) == 0) {
            goto qbranch;
        } break;
    case VMO_NEXT:
        R--;
        if (R) goto qbranch;
        VM_RDROP;  break;
    default: return ERR_INVALID_OPCODE;
    }
    return 0;
}

PLACE_IN_ITCM;
int32_t vmRun(int once, uint32_t inst, int32_t address) {

    int32_t ior = 0;                    // 0 = okay
    uint32_t steps = 0;
    int dirty = 1;

    if (once) {
        goto execute;                   // vmRun(1, inst, 0)
    }
    else {                              // RUN 'inst' steps of code

        steps = inst;                   // vmRun(0,steps,0) or
        int page;                       // vmRun(0,0,address)

        if (steps == 0) {               // run a word indefinitely
            VM_RDUP;                    // launch it with a terminator
            R = 0xDEADC0DE;             // on the return stack
            PC = address;
        }

    fetch:                              // outer loop starts here...
        if (dirty) {                    // fetch inst pair regardless
            dirty = 0;
        }
        else if (PC & 1) {              // 2nd instruction in pair
            inst = inst >> 16;
        }
        else {
            page = PC >> (24 - VM_SEGMENT_BITS);
            if (page >= VM_SEGMENTS) {
                if (PC == (int32_t)0xDEADC0DE) return 0;
                return ERR_EXEC_PROTECTED;
            }
            uint32_t a = (PC >> 1) & VM_SEGMASK;
            if (a >= vm_memory_executable[page]) return ERR_EXEC_PROTECTED;
            inst = vm_memory[page][a];
            if (!(PC & 1)) {
                inst = inst >> 16;
            }
        }
        PC++;
        // Run a 16-bit instruction or instruction group using the lower half
        // of `inst`. The upper half of 'inst' is a cache for the next one.
    execute:
        if (inst & VM_UOPS) {
            if (inst & VM_RET) {
                PC = R;
                VM_RDROP;
                dirty = 1;
            }
            int i = SLOT0_POSITION + 5;
            int bumpa = 0;
            while (i > 0) { // Execute a group of 5-bit MISC instructions
                i -= 5;
                int uop;
                if (i < 0) uop = inst & LAST_SLOT_MASK;
                else uop = (inst >> i) & 0x1F;
                int se = stackeffects[uop];
                int32_t n = T;
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
                case VMU_SWAP: {
                    int32_t temp = datastack[sp];
                    datastack[sp] = T;
                    T = temp;
                }                                                   break;
                case VMU_CY:        T = cy;                         break;
                case VMU_B:         T = B;                          break;
                case VMU_OVER:      T = datastack[(sp - 1) & STACK_MASK]; break;
                case VMU_PUSH:      VM_RDUP;  R = n;                break;
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
                    int page = (maddr >> (22 - VM_SEGMENT_BITS)) & (VM_SEGMENTS - 1);
                    uint32_t a = maddr & VM_SEGMASK;
                    if (a >= vm_memory_rd_limit[page]) {
                        return ERR_INVALID_ADDRESS;
                    }
                    T = vm_memory[page][a];
                    if (bitfield_size) {
                        int bshift = (a >> 22) & 0x1F;
                        T = (T >> bshift) & ~(0xFFFFFFFF << bitfield_size);
                    }
                    if (bumpa & 1) { vm_postincA(); }
                    if (bumpa & 2) { vm_postincB(); }
                    break;
                }

                case VMU_STOREA:     maddr = A; bumpa = 0; goto memstore;
                case VMU_STOREAPLUS: maddr = A; bumpa = 1; goto memstore;
                case VMU_STOREB:     maddr = B; bumpa = 0; goto memstore;
                case VMU_STOREBPLUS: maddr = B; bumpa = 2; goto memstore;

                memstore: {
                    int bitfield_size = maddr >> 27;
                    int page = (maddr >> (22 - VM_SEGMENT_BITS)) & (VM_SEGMENTS - 1);
                    uint32_t a = maddr & VM_SEGMASK;
                    if (a >= vm_memory_rd_limit[page]) { // must be below the read limit
                        return ERR_INVALID_ADDRESS;
                    }
                    if (a < vm_memory_wp_limit[page]) { // and above the write protect limit
                        return ERR_WRITE_PROTECTED;
                    }
                    if (bitfield_size) { // bit fields need a RMW operation
                        int bshift = (a >> 22) & 0x1F;
                        uint32_t mask = ~(0xFFFFFFFF << bitfield_size);
                        n = (vm_memory[page][a] & (~(mask << bshift))) | ((n >> bshift) & mask);
                    }
                    vm_memory[page][a] = n;
                    if (bumpa & 1) { vm_postincA(); break; }
                    if (bumpa & 2) { vm_postincB(); }
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
                    ior = vmLitIns9((uint16_t)inst, imm);
                }
            }
        }

        if (steps) {
            steps--;
            if (steps == 0) return 0;
        }
        if (ior) return ior;
        if (once == 0) goto fetch;
    }
    return ior;
}



int32_t vmPeek(int reg) {
    if (reg < 0) {
        int32_t tos = T;
        VM_DDROP;
        return tos;
    }
    switch (reg) {
        case 0:         return T;
        case VM_REG_depth: return depth;
        case VM_REG_PC: return PC;
        case VM_REG_R : return R;
        case VM_REG_A : return A;
        case VM_REG_B : return B;
        case VM_REG_X : return X;
        case VM_REG_Y : return Y;
        case VM_REG_cy: return cy;
        case VM_REG_sp: return sp;
        case VM_REG_rp: return rp;
        default:
        if (reg < STACK_MASK) {
            int index = (sp + 1 - reg) & STACK_MASK;
            return datastack[index];
        }   return -1;
    }
}

int32_t vmPoke(int reg, int32_t data) {
    if (reg < 0) {
        reg = 0;
        VM_DDUP;
    }
    switch (reg) {
        case 0:         T = data; break;
        case VM_REG_depth: depth = data; break;
        case VM_REG_PC: PC = data; break;
        case VM_REG_R : R = data; break;
        case VM_REG_A : A = data; break;
        case VM_REG_B : B = data; break;
        case VM_REG_X : X = data; break;
        case VM_REG_Y : Y = data; break;
        case VM_REG_cy: cy = data & 1; break;
        case VM_REG_sp: sp = data & STACK_MASK; break;
        case VM_REG_rp: rp = data & STACK_MASK; break;
        default:
        if (reg < STACK_MASK) {
            int index = (sp + 1 - reg) & STACK_MASK;
            datastack[index] = data;
            break;
        }   return -1;
    }
    return 0;
}

int32_t vmReset(void) {
    for (int i = VM_REG_depth; i <= VM_REG_rp; i++) {
        vmPoke(i, 0);
    }
    return 0;
}

