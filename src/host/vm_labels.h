#ifndef _VM_LABELS_H_
#define _VM_LABELS_H_

// These are internal labels and macros used by vm.c.

/* Forces data straight into the Data Tightly Coupled Memory section */
#define PLACE_IN_DTCM // __attribute__((section(".dtcm")))

/* Forces code straight into the Instruction Tightly Coupled Memory section */
#define PLACE_IN_ITCM // __attribute__((section(".itcm")))

#define VM_DDUP do {                    \
    datastack[sp] = T;                  \
    sp = (sp + 1) & STACK_MASK;         \
} while(0)      

#define VM_RDUP do {                    \
    returnstack[rp] = R;                \
    rp = (rp + 1) & STACK_MASK;         \
} while(0)      

#define VM_DDROP do {                   \
    sp = (sp - 1) & STACK_MASK;         \
    T = datastack[sp];                  \
} while(0)      

#define VM_RDROP do {                   \
    rp = (rp - 1) & STACK_MASK;         \
    R = returnstack[rp];                \
} while(0)    

#define NOS datastack[sp]

#define VM_UOPS       0x8000 // uops bit location
#define VM_RET        0x4000 // return bit location
#define SLOT0_POSITION   9
#define LAST_SLOT_WIDTH (14 % 5)
#define LAST_SLOT_MASK  ((1 << LAST_SLOT_WIDTH) - 1)

#define UOP_NAMES { \
    "nop",   "inv",   "over",  "a!",    "xor",   "+",    "and",   ">r", \
    "unext", "2*",    "dup",   "drop",  "@a",    "@a+",   "r@",    "r>", \
    "2/c",   "2/",    "?",     "?",     "!a",    "!a+",   "!b",    "!b+", \
    "swap",  "+*",    "b",     "b!",    "@b",    "@b+",   "a",     "cy"}

#define VM_STACKEFFECTS { /* 0=none, 1=dup, 2=drop */ \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01, \
    0x00,    0x00,    0x01,    0x02,    0x02,    0x02,    0x02,    0x02, \
    0x00,    0x00,    0x01,    0x02,    0x01,    0x01,    0x01,    0x01}

#define API_NAMES { \
    "NVM@[", "NMV![", "NVM@", "NVM!", "]NVM", "semit", "um*", "mu/mod", \
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

#define OP_NAMES  {"call", "jump", "lit"}

#define VM_IMMBITS              13
#define VMO_CALL                0
#define VMO_JUMP                1
#define VMO_LIT                 2
#define VMO_PFX                 3
#define VMI_CALL                (VMO_CALL << VM_IMMBITS)
#define VMI_JUMP                (VMO_JUMP << VM_IMMBITS)
#define VMI_LIT                 (VMO_LIT << VM_IMMBITS)
#define VMI_PFX                 (VMO_PFX << VM_IMMBITS)
#define VMI_ZOODUP              (1 << 8)
#define VMI_ZOODROP             (1 << 7)
#define VMI_ZOO                 (VMI_PFX + (1 << 9))

#define ZOO_NAMES  {"bcisync", "err!", "x!", "y!", "x@", "y@"}

#define VMZ_BCISYNC             0
#define VMZ_THROW               1
#define VMZ_XSTORE              2
#define VMZ_YSTORE              3
#define VMZ_XFETCH              4
#define VMZ_YFETCH              5

#define VMI_BCISYNC             (VMI_ZOO + VMZ_BCISYNC)
#define VMI_THROW               (VMI_ZOO + VMZ_THROW   + VMI_ZOODROP)
#define VMI_XSTORE              (VMI_ZOO + VMZ_XSTORE  + VMI_ZOODROP)
#define VMI_YSTORE              (VMI_ZOO + VMZ_YSTORE  + VMI_ZOODROP)
#define VMI_XFETCH              (VMI_ZOO + VMZ_XFETCH  + VMI_ZOODUP)
#define VMI_YFETCH              (VMI_ZOO + VMZ_YFETCH  + VMI_ZOODUP)

#define IMM_NAMES { \
    "pfx", "zoo", "ax", "by", "if", "bran", "-if", "next", \
    "py!", "?", "?", "?", "APIcall", "APIcall+", "APIcall-", "APIcall--"}

#define VMO_LEX                 0
#define VMO_ZOO                 1
#define VMO_AX                  2
#define VMO_BY                  3
#define VMO_ZBRAN               4
#define VMO_BRAN                5
#define VMO_PBRAN               6
#define VMO_RCALL               7
#define VMO_NEXT                8
#define VMO_API0                14
#define VMO_API1                15

#define VMI_AX                 (VMI_PFX + (VMO_AX       << 9))
#define VMI_BY                 (VMI_PFX + (VMO_BY       << 9))
#define VMI_ZBRAN              (VMI_PFX + (VMO_ZBRAN    << 9))
#define VMI_BRAN               (VMI_PFX + (VMO_BRAN     << 9))
#define VMI_PBRAN              (VMI_PFX + (VMO_PBRAN    << 9))
#define VMI_RCALL              (VMI_PFX + (VMO_RCALL    << 9))
#define VMI_NEXT               (VMI_PFX + (VMO_NEXT     << 9))
#define VMI_API0               (VMI_PFX + (VMO_API0     << 9))
#define VMI_API1               (VMI_PFX + (VMO_API1     << 9))

#endif /* _VM_LABELS_H_ */