#ifndef _VM_LABELS_H_
#define _VM_LABELS_H_

// These are internal labels and macros used by vm.c.

/* Forces data straight into the Data Tightly Coupled Memory section */
#define PLACE_IN_DTCM /* __attribute__((section(".dtcm"))) */

/* Forces code straight into the Instruction Tightly Coupled Memory section */
#define PLACE_IN_ITCM /* __attribute__((section(".itcm"))) */

#define VM_DDUP do {                    \
    sp = (sp + 1) & STACK_MASK;         \
    depth++;                            \
    datastack[sp] = T;                  \
} while(0)      

#define VM_RDUP do {                    \
    rp = (rp + 1) & STACK_MASK;         \
    returnstack[rp] = R;                \
} while(0)      

#define VM_DDROP do {                   \
    T = datastack[sp];                  \
    sp = (sp - 1) & STACK_MASK;         \
    depth--;                            \
} while(0)      

#define VM_RDROP do {                   \
    R = returnstack[rp];                \
    rp = (rp - 1) & STACK_MASK;         \
} while(0)    

#define NOS datastack[sp]

#define VM_UOPS         0x8000 // uops bit location
#define VM_RET          0x4000 // return bit location
#define SLOT0_POSITION  9      // [13:9] is the first 5-bit slot
#define LAST_SLOT_WIDTH (14 % 5)
#define LAST_SLOT_MASK  ((1 << LAST_SLOT_WIDTH) - 1)
#define VM_SEGMASK      ((1 << (22 - VM_LOG2_PAGES)) - 1)

#define VM_REG_depth    0x100
#define VM_REG_PC       0x101
#define VM_REG_R        0x102
#define VM_REG_A        0x103
#define VM_REG_B        0x104
#define VM_REG_U        0x105
#define VM_REG_Y        0x106
#define VM_REG_cy       0x107
#define VM_REG_sp       0x108
#define VM_REG_rp       0x109


#define UOP_NAMES { \
    "nop",   "inv",   "over",  "a!",    "xor",   "+",     "and",   ">r", \
    "unext", "2*",    "dup",   "drop",  "@a",    "@a+",   "r@",    "r>", \
    "2/c",   "2/",    "@as",   "u!",    "!a",    "!a+",   "!b",    "!b+", \
    "swap",  "+*",    "b",     "b!",    "@b",    "@b+",   "a",     "cy"}

#define VM_STACKEFFECTS { /* 0=none, 1=dup, 2=drop */ \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01, \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01}

// These need to be fixed...
#define API_NAMES { \
    "semit", "um*", "mu/mod", \
    "LCDraw", "LCDparm!", "LCDparm", "LCDemit"  \
}

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
#define VMU_USTORE              0x13
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

#define OP_NAMES  {"jump", "call", "lit"}

#define VM_IMMBITS              13
#define VMO_JUMP                0
#define VMO_CALL                1
#define VMO_LIT                 2
#define VMO_OTHER               3
#define VMI_CALL                (VMO_CALL << VM_IMMBITS)
#define VMI_JUMP                (VMO_JUMP << VM_IMMBITS)
#define VMI_LIT                 (VMO_LIT << VM_IMMBITS)
#define VMI_OTHER               (VMO_OTHER << VM_IMMBITS)

#define VMSTO_TASK              0
#define VMSTO_BARF              1
#define VMSFROM_TASK            0

/*
*/

#define IMM_NAMES { \
    "if", "bran", "-if", "rcall", "next", "?", "sys", "pfx", \
    ">sys", "user", "sys>", "qlit", "?", "?", "RFcall", "AFcall"}

#define VMO_ZBRAN               0
#define VMO_BRAN                1
#define VMO_PBRAN               2
#define VMO_RCALL               3
#define VMO_NEXT                4
#define VMO_SYS                 6
#define VMO_PFX                 7
#define VMO_TOSYS               8
#define VMO_USER                9
#define VMO_FROMSYS             10
#define VMO_QLIT                11
#define VMO_API0                14
#define VMO_API1                15

#define VMI_ZBRAN              (VMI_OTHER + (VMO_ZBRAN    << 9))
#define VMI_BRAN               (VMI_OTHER + (VMO_BRAN     << 9))
#define VMI_PBRAN              (VMI_OTHER + (VMO_PBRAN    << 9))
#define VMI_RCALL              (VMI_OTHER + (VMO_RCALL    << 9))
#define VMI_NEXT               (VMI_OTHER + (VMO_NEXT     << 9))
#define VMI_SYS                (VMI_OTHER + (VMO_SYS      << 9))
#define VMI_PFX                (VMI_OTHER + (VMO_PFX      << 9))
#define VMI_TOSYS              (VMI_OTHER + (VMO_TOSYS    << 9))
#define VMI_USER               (VMI_OTHER + (VMO_USER     << 9))
#define VMI_FROMSYS            (VMI_OTHER + (VMO_FROMSYS  << 9))
#define VMI_QLIT               (VMI_OTHER + (VMO_QLIT     << 9))
#define VMI_API0               (VMI_OTHER + (VMO_API0     << 9))
#define VMI_API1               (VMI_OTHER + (VMO_API1     << 9))

#endif /* _VM_LABELS_H_ */