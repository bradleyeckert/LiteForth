#ifndef _VM_LABELS_H_
#define _VM_LABELS_H_

// These are internal labels and macros used by vm.c and vm_rv32.S, so
// everything here must be plain #defines (vm_rv32.S includes this file).

/* Forces data straight into the Data Tightly Coupled Memory section */
#define PLACE_IN_DTCM /* __attribute__((section(".dtcm"))) */

/* Forces code straight into the Instruction Tightly Coupled Memory section */
#define PLACE_IN_ITCM /* __attribute__((section(".itcm"))) */

// Data stack moves on lf_vm (vm.c, used by vmPeek and vmPoke). With
// TWO_REGISTER_TOS (vm.h) the top two items are cached in T and N: a push
// moves N to memory and T to N. Without it, only T is cached. Either way
// the caller then sets T.
#ifdef TWO_REGISTER_TOS
#define VM_DDUP do {                            \
    lf_vm.sp = (lf_vm.sp + 1) & STACK_MASK;     \
    lf_vm.datastack[lf_vm.sp] = lf_vm.N;        \
    lf_vm.N = lf_vm.T;                          \
} while(0)
#define VM_DDROP do {                           \
    lf_vm.T = lf_vm.N;                          \
    lf_vm.N = lf_vm.datastack[lf_vm.sp];        \
    lf_vm.sp = (lf_vm.sp - 1) & STACK_MASK;     \
} while(0)
#else
#define VM_DDUP do {                            \
    lf_vm.sp = (lf_vm.sp + 1) & STACK_MASK;     \
    lf_vm.datastack[lf_vm.sp] = lf_vm.T;        \
} while(0)
#define VM_DDROP do {                           \
    lf_vm.T = lf_vm.datastack[lf_vm.sp];        \
    lf_vm.sp = (lf_vm.sp - 1) & STACK_MASK;     \
} while(0)
#endif

#define VM_PAGE_MASK    ((1 << (22 - VM_LOG2_PAGES)) - 1)

// Byte offsets of the VM state in lf_vm (vm_state_t in vm.c, which checks
// them). vm_rv32.S reaches the state through one base register.
#define VMS_T           0
#define VMS_N           4
#define VMS_PC          8
#define VMS_R           12
#define VMS_A           16
#define VMS_B           20
#define VMS_U           24
#define VMS_X           28
#define VMS_Y           32
#define VMS_PREFIX      36
#define VMS_SHIFT       40
#define VMS_SP          44
#define VMS_RP          48
#define VMS_CY          52
#define VMS_DSTACK      64
#define VMS_RSTACK      (VMS_DSTACK + 4 * STACK_CAPACITY)

// slot assignments for 16-bit instruction
#define VM_UOPS         0x8000 // uops bit location
#define VM_RET          0x4000 // return bit location
#define SLOT0_POSITION  9      // [13:9] is the first 5-bit slot
#define LAST_SLOT_WIDTH (14 % 5)
#define LAST_SLOT_MASK  ((1 << LAST_SLOT_WIDTH) - 1)

// register assignments for vmPeek and vmPoke
#define VM_REG_PC       0x100
#define VM_REG_R        0x101
#define VM_REG_A        0x102
#define VM_REG_B        0x103
#define VM_REG_U        0x104
#define VM_REG_Y        0x105
#define VM_REG_cy       0x106
#define VM_REG_sp       0x107
#define VM_REG_rp       0x108

// micro opcodes (5 bit instructions)
#define UOP_NAMES { \
    "nop",   "inv",   "over",  "a!",    "xor",   "+",     "and",   ">r", \
    "unext", "2*",    "dup",   "drop",  "@a",    "@a+",   "r@",    "r>", \
    "2/c",   "2/",    "@as",   "px!",   "!a",    "!a+",   "!b",    "!b+", \
    "swap",  "+*",    "b",     "b!",    "@b",    "@b+",   "a",     "cy"}

#define VM_STACKEFFECTS { /* 0=none, 1=dup, 2=drop */ \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01, \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01}

#define VMU_NOP                 0x00
#define VMU_INV                 0x01
#define VMU_OVER                0x02
#define VMU_ASTORE              0x03
#define VMU_XOR                 0x04
#define VMU_PLUS                0x05
#define VMU_AND                 0x06
#define VMU_PUSH                0x07
#define VMU_UNEXT               0x08
#define VMU_TWOSTAR             0x09
#define VMU_DUP                 0x0A
#define VMU_DROP                0x0B
#define VMU_FETCHA              0x0C
#define VMU_FETCHAPLUS          0x0D
#define VMU_R                   0x0E
#define VMU_POP                 0x0F
#define VMU_TWODIVC             0x10
#define VMU_TWODIV              0x11
#define VMU_FETCHASIGN          0x12
#define VMU_PXSTORE             0x13
#define VMU_STOREA              0x14
#define VMU_STOREAPLUS          0x15
#define VMU_STOREB              0x16
#define VMU_STOREBPLUS          0x17
#define VMU_SWAP                0x18
#define VMU_PLUSSTAR            0x19
#define VMU_B                   0x1A
#define VMU_BSTORE              0x1B
#define VMU_FETCHB              0x1C
#define VMU_FETCHBPLUS          0x1D
#define VMU_A                   0x1E
#define VMU_CY                  0x1F

// instructions with 13-bit immediate data
#define VM_LIMM_BITS            13
#define VM_LIMM_MASK            ((1 << VM_LIMM_BITS) - 1)
#define VMO_JUMP                0  
#define VMO_CALL                1
#define VMO_LIT                 2
#define VMO_OTHER               3
#define VMI_CALL                (VMO_CALL << VM_LIMM_BITS)
#define VMI_JUMP                (VMO_JUMP << VM_LIMM_BITS)
#define VMI_LIT                 (VMO_LIT << VM_LIMM_BITS)
#define VMI_OTHER               (VMO_OTHER << VM_LIMM_BITS)

// instructions with 9-bit immediate data
#define VM_IMM_BITS             9
#define VM_IMM_MASK             ((1 << VM_IMM_BITS) - 1)

#define IMM_NAMES { \
    "if", "bran", "-if", "rcall", "next", "?", "sys", "?", \
    ">sys", "user", "sys>", "qlit", "pfx", "pfx1", "API", "XAPI"}

#define VMO_ZBRAN               0
#define VMO_BRAN                1
#define VMO_PBRAN               2
#define VMO_RCALL               3
#define VMO_NEXT                4
#define VMO_SYS                 6
#define VMO_TOSYS               8
#define VMO_USER                9
#define VMO_FROMSYS             10
#define VMO_PFX                 12
#define VMO_PFX1                13
#define VMO_API0                14
#define VMO_API1                15

#define VMI_ZBRAN              (VMI_OTHER + (VMO_ZBRAN    << 9))
#define VMI_BRAN               (VMI_OTHER + (VMO_BRAN     << 9))
#define VMI_PBRAN              (VMI_OTHER + (VMO_PBRAN    << 9))
#define VMI_RCALL              (VMI_OTHER + (VMO_RCALL    << 9))
#define VMI_NEXT               (VMI_OTHER + (VMO_NEXT     << 9))
#define VMI_SYS                (VMI_OTHER + (VMO_SYS      << 9))
#define VMI_PFX                (VMI_OTHER + (VMO_PFX      << 9))
#define VMI_PFX1               (VMI_OTHER + (VMO_PFX1     << 9))
#define VMI_TOSYS              (VMI_OTHER + (VMO_TOSYS    << 9))
#define VMI_USER               (VMI_OTHER + (VMO_USER     << 9))
#define VMI_FROMSYS            (VMI_OTHER + (VMO_FROMSYS  << 9))
#define VMI_API0               (VMI_OTHER + (VMO_API0     << 9))
#define VMI_API1               (VMI_OTHER + (VMO_API1     << 9))

#define VMI_MASK               ((3 << VM_LIMM_BITS) | (0x0F << VM_IMM_BITS))

// system instructions VMI_SYS, VMI_TOSYS, and VMI_FROMSYS

#define VMS_NAMES  {"]task", "field+", "]shr", "]shl", "capusec", "break", "", "", ""}
#define VMST_NAMES {"shft[", "yeet", "u!", "", "", "", "", ""}
#define VMSF_NAMES {"task[", "x@", "y@", "", "", "", "", ""}

#define VMS_TASK                0
#define VMS_FIELDPLUS           1
#define VMS_SHR                 2
#define VMS_SHL                 3
#define VMS_GETUSEC             4
#define VMS_BREAK               5

#define VMSTO_SHIFT             0
#define VMSTO_YEET              1
#define VMSTO_USTORE            2

#define VMSFROM_TASK            0
#define VMSFROM_X               1
#define VMSFROM_Y               2


#endif /* _VM_LABELS_H_ */