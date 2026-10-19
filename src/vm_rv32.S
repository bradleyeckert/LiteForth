/*
 * vm_rv32.S: vmExec for RV32IMC, in assembly. Define VM_ASM to use it in
 * place of the C vmExec in vm.c (vm.c then calls vmExecAsm). It must do
 * exactly what the C version does: unit_tests/vmasm runs both on random
 * code and compares the results.
 *
 * int32_t vmExecAsm(int once, uint32_t inst, uint32_t address)
 *
 * The VM registers live in CPU registers while it runs, and in lf_vm
 * (vm.h) otherwise. Instructions are decoded like this:
 *   - Micro-op groups: the slots are lined up at the top of `slots` and
 *     shifted out. Each of the 32 micro-ops has a 64-byte handler at
 *     uop_table + 64*n, which ends by dispatching the next one, so there is
 *     no jump table to read and no loop branch.
 *   - The 16 instructions with a 9-bit immediate have 32-byte handlers at
 *     imm_table + 32*n.
 *   - Jump, call and literal are tested for directly.
 * Each instruction ends with `jr TAIL`: fetch (calling a word),
 * step_tail (stepping the app) or once_tail (one instruction).
 * Memory access, vmFieldPlus and the stack moves are inline or local
 * subroutines; the only calls into C are API calls, `usec` and `break`.
 *
 * The layout of lf_vm is in vm_labels.h (VMS_*). Code must stay at fixed
 * offsets in the tables, so linker relaxation is off for this file, and
 * `.org` stops the build if a handler outgrows its slot.
 */
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"

#if (VMS_RSTACK + 4 * STACK_CAPACITY) > 2047
#error "vm_rv32.S: the stacks must fit in 12-bit offsets (STACK_CAPACITY <= 256)"
#endif

#define MASK4       (4 * (STACK_CAPACITY - 1))  // stack pointers are byte offsets
#define PAGE_SHIFT  (22 - VM_LOG2_PAGES)        // data address -> page
#define CODE_SHIFT  (23 - VM_LOG2_PAGES)        // instruction address -> page
#define TERMINATOR  0xDEADC0DE                  // return address that ends a word call
#define UOP_LOG2    6                           // micro-op handler size 64 bytes
#define IMM_LOG2    5                           // 9-bit immediate handler size 32 bytes

// VM registers. x8-x15 (s0, s1, a0-a5) can use compressed instructions.
#define rT      s0      // T
#define rDSP    s1      // sp * 4
#define rSLOTS  a0      // micro-ops still to run, at the top
#define rN      a2      // N (TWO_REGISTER_TOS)
#define rINST   a4      // the instruction, sign-extended from 16 bits
#define rR      s2      // R
#define rA      s3      // A
#define rB      s4      // B
#define rPC     s5      // PC
#define rRSP    s6      // rp * 4
#define rBASE   s7      // &lf_vm
#define rPFX    s8      // prefix
#define rCY     s9      // cy
#define rCBASE  s10     // first PC of the code page being run
#define rCSPAN  s11     // its number of executable instructions (0: look it up)
#define rCODE   t0      // its memory
#define rUB     t1      // uop_table
#define rIB     t2      // imm_table
#define rSTEPS  t3      // steps left, 0 when not stepping
#define rTAIL   t4      // where each instruction ends
#define rMERR   t5      // ior of a failed memory access, else 0
// Scratch: a1 (used by the stack macros), a3 (ior on the way to `fail`),
// a5 (subroutine link), a6, a7, t6.

// Frame: spill area for calls into C, then the saved registers.
#define FRAME   96

        .option push
        .option norelax

/* ---- stack moves --------------------------------------------------------
 * DDUP makes room for a new T (the caller sets T); DDROP pops T; NIP drops
 * the second item. As in vm.c: with TWO_REGISTER_TOS, T and N are the top
 * two items and datastack[sp] the third, else datastack[sp] is the second.
 * They use a1. */

#ifdef TWO_REGISTER_TOS
.macro DDUP
        addi    rDSP, rDSP, 4
        andi    rDSP, rDSP, MASK4
        add     a1, rBASE, rDSP
        sw      rN, VMS_DSTACK(a1)
        mv      rN, rT
.endm
.macro DDROP
        mv      rT, rN
        add     a1, rBASE, rDSP
        lw      rN, VMS_DSTACK(a1)
        addi    rDSP, rDSP, -4
        andi    rDSP, rDSP, MASK4
.endm
.macro NIP
        add     a1, rBASE, rDSP
        lw      rN, VMS_DSTACK(a1)
        addi    rDSP, rDSP, -4
        andi    rDSP, rDSP, MASK4
.endm
#else
.macro DDUP
        addi    rDSP, rDSP, 4
        andi    rDSP, rDSP, MASK4
        add     a1, rBASE, rDSP
        sw      rT, VMS_DSTACK(a1)
.endm
.macro DDROP
        add     a1, rBASE, rDSP
        lw      rT, VMS_DSTACK(a1)
        addi    rDSP, rDSP, -4
        andi    rDSP, rDSP, MASK4
.endm
.macro NIP
        addi    rDSP, rDSP, -4
        andi    rDSP, rDSP, MASK4
.endm
#endif

.macro RDUP
        addi    rRSP, rRSP, 4
        andi    rRSP, rRSP, MASK4
        add     a1, rBASE, rRSP
        sw      rR, VMS_RSTACK(a1)
.endm
.macro RDROP
        add     a1, rBASE, rRSP
        lw      rR, VMS_RSTACK(a1)
        addi    rRSP, rRSP, -4
        andi    rRSP, rRSP, MASK4
.endm

/* Runs the next micro-op of the group, or ends the instruction. */
.macro NEXT_UOP
        beqz    rSLOTS, uops_done
        srli    a1, rSLOTS, 27 - UOP_LOG2
        andi    a1, a1, 31 << UOP_LOG2
        slli    rSLOTS, rSLOTS, 5
        add     a1, a1, rUB
        jr      a1
.endm

/* Copies the VM registers to lf_vm and back. U, X, Y and shift_size
   stay in lf_vm. */
.macro VM_SAVE
        sw      rT, VMS_T(rBASE)
#ifdef TWO_REGISTER_TOS
        sw      rN, VMS_N(rBASE)
#endif
        sw      rR, VMS_R(rBASE)
        sw      rPC, VMS_PC(rBASE)
        sw      rA, VMS_A(rBASE)
        sw      rB, VMS_B(rBASE)
        srli    a1, rDSP, 2
        sw      a1, VMS_SP(rBASE)
        srli    a1, rRSP, 2
        sw      a1, VMS_RP(rBASE)
        sw      rCY, VMS_CY(rBASE)
        sw      rPFX, VMS_PREFIX(rBASE)
.endm
.macro VM_LOAD
        lw      rT, VMS_T(rBASE)
#ifdef TWO_REGISTER_TOS
        lw      rN, VMS_N(rBASE)
#endif
        lw      rR, VMS_R(rBASE)
        lw      rPC, VMS_PC(rBASE)
        lw      rA, VMS_A(rBASE)
        lw      rB, VMS_B(rBASE)
        lw      a1, VMS_SP(rBASE)
        andi    a1, a1, STACK_CAPACITY - 1
        slli    rDSP, a1, 2
        lw      a1, VMS_RP(rBASE)
        andi    a1, a1, STACK_CAPACITY - 1
        slli    rRSP, a1, 2
        lw      rCY, VMS_CY(rBASE)
        andi    rCY, rCY, 1
        lw      rPFX, VMS_PREFIX(rBASE)
.endm

/* Saves the caller-saved registers that hold state around a call into C. */
.macro SPILL
        sw      rN, 0(sp)
        sw      rCODE, 4(sp)
        sw      rUB, 8(sp)
        sw      rIB, 12(sp)
        sw      rSTEPS, 16(sp)
        sw      rTAIL, 20(sp)
.endm
.macro UNSPILL
        lw      rN, 0(sp)
        lw      rCODE, 4(sp)
        lw      rUB, 8(sp)
        lw      rIB, 12(sp)
        lw      rSTEPS, 16(sp)
        lw      rTAIL, 20(sp)
        li      rMERR, 0
.endm

        .section .text.vmExecAsm, "ax", @progbits
        .globl  vmExecAsm
        .type   vmExecAsm, @function
        .balign 4
vmExecAsm:
        addi    sp, sp, -FRAME
        sw      ra, FRAME-4(sp)
        sw      s0, FRAME-8(sp)
        sw      s1, FRAME-12(sp)
        sw      s2, FRAME-16(sp)
        sw      s3, FRAME-20(sp)
        sw      s4, FRAME-24(sp)
        sw      s5, FRAME-28(sp)
        sw      s6, FRAME-32(sp)
        sw      s7, FRAME-36(sp)
        sw      s8, FRAME-40(sp)
        sw      s9, FRAME-44(sp)
        sw      s10, FRAME-48(sp)
        sw      s11, FRAME-52(sp)
        mv      a6, a0                  // once
        mv      a7, a1                  // inst or steps
        mv      t6, a2                  // address
        lui     rBASE, %hi(lf_vm)
        addi    rBASE, rBASE, %lo(lf_vm)
        VM_LOAD
        la      rUB, uop_table
        la      rIB, imm_table
        li      rCSPAN, 0
        li      rCBASE, 0
        li      rMERR, 0
        li      rSTEPS, 0
        beqz    a6, 1f
        la      rTAIL, once_tail        // vmRun(1, inst, 0)
        slli    rINST, a7, 16
        srai    rINST, rINST, 16
        j       execute
1:      mv      rSTEPS, a7
        bnez    rSTEPS, 2f
        RDUP                            // vmRun(0, 0, address): run a word
        li      rR, TERMINATOR          // with a terminator on the return stack
        mv      rPC, t6
        la      rTAIL, fetch
        j       fetch
2:      la      rTAIL, step_tail        // vmRun(0, steps, 0)
        j       fetch

/* ---- the main loop ------------------------------------------------------ */

step_tail:
        addi    rSTEPS, rSTEPS, -1
        beqz    rSTEPS, timeout
fetch:
        sub     a1, rPC, rCBASE
        bgeu    a1, rCSPAN, fetch_slow
fetch_go:
        slli    a1, a1, 1               // two instructions per cell, the even
        add     a1, a1, rCODE           // one in the lower half
        lh      rINST, 0(a1)
        addi    rPC, rPC, 1
execute:
        bltz    rINST, uops
        slli    a1, rINST, 17           // bit 14 -> 31, bit 13 -> 30
        bgez    a1, jump_call
        slli    a1, a1, 1
        bltz    a1, immop
        DDUP                            // literal
        slli    a1, rINST, 19
        srli    a1, a1, 19
        slli    rT, rPFX, 13
        or      rT, rT, a1
        li      rPFX, 0
        jr      rTAIL

jump_call:
        slli    a1, a1, 1
        bgez    a1, 1f
        RDUP                            // call: push PC
        mv      rR, rPC
1:      slli    a1, rINST, 19
        srli    a1, a1, 19
        slli    rPC, rPFX, 13
        or      rPC, rPC, a1
        li      rPFX, 0
        jr      rTAIL

immop:
        srli    a1, rINST, VM_IMM_BITS - IMM_LOG2
        andi    a1, a1, 15 << IMM_LOG2
        add     a1, a1, rIB
        jr      a1

uops:
        slli    a1, rINST, 17
        bgez    a1, group
        mv      rPC, rR                 // return first, then the micro-ops
        RDROP
group:                                  // slots 13:9, 8:4, 3:0 -> 31:27, 26:22, 21:17
        slli    rSLOTS, rINST, 18
        andi    a1, rINST, 15
        slli    a1, a1, 17
        sub     rSLOTS, rSLOTS, a1
        srli    a1, rSLOTS, 27 - UOP_LOG2
        andi    a1, a1, 31 << UOP_LOG2
        slli    rSLOTS, rSLOTS, 5
        add     a1, a1, rUB
        jr      a1

fetch_slow:                             // PC is outside the code page
        li      a1, TERMINATOR
        beq     rPC, a1, terminated
        srli    a1, rPC, CODE_SHIFT
        li      a3, VM_MEM_PAGES
        bgeu    a1, a3, exec_error
        slli    a3, a1, 2
        lui     rCODE, %hi(vm_memory)
        add     rCODE, rCODE, a3
        lw      rCODE, %lo(vm_memory)(rCODE)
        lui     rCSPAN, %hi(vm_memory_executable)
        add     rCSPAN, rCSPAN, a3
        lw      rCSPAN, %lo(vm_memory_executable)(rCSPAN)
        slli    rCSPAN, rCSPAN, 1
        slli    rCBASE, a1, CODE_SHIFT
        sub     a1, rPC, rCBASE
        bltu    a1, rCSPAN, fetch_go
exec_error:
        li      a3, ERR_EXEC_PROTECTED  // no step count, as in vm.c
        bnez    rSTEPS, yeet
        j       exit_save
terminated:
        li      a3, 0
        j       exit_save

once_tail:
        li      a3, 0
        j       exit_save
timeout:
        li      a3, ERR_VM_TIMEOUT
        j       exit_save

fail:                                   // the instruction failed with ior a3
        beqz    rSTEPS, exit_save
        addi    rSTEPS, rSTEPS, -1      // it still counts as a step
        beqz    rSTEPS, timeout
yeet:                                   // send a stepped app to its yeet handler
        li      a1, ERR_VM_BREAK
        beq     a3, a1, exit_save
        sw      rPC, VMS_X(rBASE)
        sw      a3, VMS_Y(rBASE)
        li      rPC, VM_YEET_ADDRESS << 1
exit_save:
        VM_SAVE
        mv      a0, a3
        lw      ra, FRAME-4(sp)
        lw      s0, FRAME-8(sp)
        lw      s1, FRAME-12(sp)
        lw      s2, FRAME-16(sp)
        lw      s3, FRAME-20(sp)
        lw      s4, FRAME-24(sp)
        lw      s5, FRAME-28(sp)
        lw      s6, FRAME-32(sp)
        lw      s7, FRAME-36(sp)
        lw      s8, FRAME-40(sp)
        lw      s9, FRAME-44(sp)
        lw      s10, FRAME-48(sp)
        lw      s11, FRAME-52(sp)
        addi    sp, sp, FRAME
        ret

/* ---- memory ------------------------------------------------------------
 * Called with `jal a5`. Like vmFetch and vmStore in vm.c; on an error they
 * set rMERR (and mem_fetch returns 0). Scratch: a1, a6, t6.
 *
 * Past a page's read limit, on the guarded page (vm_io_page), the
 * vm_io_ranges allowlist decides (vmGuarded in vm.c): GUARD walks it,
 * keeping the address a3 at GUARD_SPILL(sp) meanwhile, and leaves the
 * flags of the first range holding cell t6 in a6, or goes to `none`. */
#define GUARD_SPILL 24                  // free in the frame: calls into C use 0-23

.macro GUARD none
        lui     a6, %hi(vm_io_page)
        lw      a6, %lo(vm_io_page)(a6)
        slli    a6, a6, 2
        bne     a6, a1, \none           // not the guarded page
        sw      a3, GUARD_SPILL(sp)
        lui     a6, %hi(vm_io_ranges)
        lw      a6, %lo(vm_io_ranges)(a6)
91:     lw      a3, 4(a6)               // end: 0 ends the list
        beqz    a3, 93f
        bgeu    t6, a3, 92f
        lw      a3, 0(a6)               // first
        bgeu    t6, a3, 94f
92:     addi    a6, a6, 12              // sizeof(vm_io_range)
        j       91b
93:     lw      a3, GUARD_SPILL(sp)
        j       \none
94:     lw      a6, 8(a6)               // its flags
        lw      a3, GUARD_SPILL(sp)
.endm

/* a3 = the cell or slice at address a3. Returns the slice size in a1. */
mem_fetch:
        srli    a1, a3, PAGE_SHIFT - 2
        andi    a1, a1, (VM_MEM_PAGES - 1) << 2
        slli    t6, a3, 32 - PAGE_SHIFT
        srli    t6, t6, 32 - PAGE_SHIFT
        lui     a6, %hi(vm_memory_rd_limit)
        add     a6, a6, a1
        lw      a6, %lo(vm_memory_rd_limit)(a6)
        bgeu    t6, a6, 2f
mf_ok:  lui     a6, %hi(vm_memory)
        add     a6, a6, a1
        lw      a6, %lo(vm_memory)(a6)
        slli    t6, t6, 2
        add     t6, t6, a6
        lw      a6, 0(t6)
        srli    a1, a3, 27
        bnez    a1, 1f
        mv      a3, a6
        jr      a5
1:      srli    t6, a3, 22              // a slice: shift and mask
        srl     a6, a6, t6
        li      a3, 1
        sll     a3, a3, a1
        addi    a3, a3, -1
        and     a3, a3, a6
        jr      a5
2:      GUARD   3f                      // past the read limit
        andi    a6, a6, VM_IO_READ
        bnez    a6, mf_ok
3:      li      a3, 0
        li      rMERR, ERR_INVALID_ADDRESS
        jr      a5

/* Stores a7 at the cell or slice address a3. */
mem_store:
        srli    a1, a3, PAGE_SHIFT - 2
        andi    a1, a1, (VM_MEM_PAGES - 1) << 2
        slli    t6, a3, 32 - PAGE_SHIFT
        srli    t6, t6, 32 - PAGE_SHIFT
        lui     a6, %hi(vm_memory_rd_limit)
        add     a6, a6, a1
        lw      a6, %lo(vm_memory_rd_limit)(a6)
        bgeu    t6, a6, 2f
        lui     a6, %hi(vm_memory_wp_limit)
        add     a6, a6, a1
        lw      a6, %lo(vm_memory_wp_limit)(a6)
        bltu    t6, a6, 3f
ms_ok:  lui     a6, %hi(vm_memory)
        add     a6, a6, a1
        lw      a6, %lo(vm_memory)(a6)
        slli    t6, t6, 2
        add     t6, t6, a6
        srli    a1, a3, 27
        bnez    a1, 1f
        sw      a7, 0(t6)
        jr      a5
1:      srli    a6, a3, 22              // a slice: read, modify, write
        li      a3, 1
        sll     a3, a3, a1
        addi    a3, a3, -1              // mask
        and     a7, a7, a3
        sll     a7, a7, a6
        sll     a3, a3, a6
        not     a3, a3
        lw      a1, 0(t6)
        and     a1, a1, a3
        or      a7, a7, a1
        sw      a7, 0(t6)
        jr      a5
2:      GUARD   4f                      // past the read limit
        andi    a6, a6, VM_IO_WRITE
        bnez    a6, ms_ok
3:      li      rMERR, ERR_WRITE_PROTECTED
        jr      a5
4:      li      rMERR, ERR_INVALID_ADDRESS
        jr      a5

/* a7 = vmFieldPlus(a7). fp_slice expects a1 = the slice size, not 0.
   Scratch: a1, a6, t6. */
fp_full:
        srli    a1, a7, 27
        bnez    a1, fp_slice
        addi    a7, a7, 1
        jr      a5
fp_slice:
        srli    t6, a7, 22
        andi    t6, t6, 31
        add     t6, t6, a1              // the next slice's shift
        add     a6, t6, a1
        addi    a6, a6, -33
        bltz    a6, 1f                  // it fits in this cell
        addi    t6, t6, -32             // else it starts the next cell
        andi    t6, t6, 31
        addi    a7, a7, 1
1:      slli    a7, a7, 10
        srli    a7, a7, 10
        slli    t6, t6, 22
        or      a7, a7, t6
        slli    a1, a1, 27
        or      a7, a7, a1
        jr      a5

/* The slice cases of @a+ !a+ @b+ !b+, which finish the micro-op here. */
fpA_slow:
        mv      a7, rA
        jal     a5, fp_slice
        mv      rA, a7
        bnez    rMERR, mem_error
        NEXT_UOP
fpB_slow:
        mv      a7, rB
        jal     a5, fp_slice
        mv      rB, a7
        bnez    rMERR, mem_error
        NEXT_UOP

/* ---- 9-bit immediate instructions: the parts that don't fit ------------ */

branch:                                 // add the s9 offset
        slli    a1, rINST, 32 - VM_IMM_BITS
        srai    a1, a1, 32 - VM_IMM_BITS
        add     rPC, rPC, a1
        jr      rTAIL

sys_op:
        andi    a1, rINST, VM_IMM_MASK
        beqz    a1, sys_task            // VMS_TASK 0
        addi    a1, a1, -1
        beqz    a1, sys_fieldplus       // VMS_FIELDPLUS 1
        addi    a1, a1, -1
        beqz    a1, sys_shr             // VMS_SHR 2
        addi    a1, a1, -1
        beqz    a1, sys_shl             // VMS_SHL 3
        addi    a1, a1, -1
        beqz    a1, sys_usec            // VMS_GETUSEC 4
        addi    a1, a1, -1
        beqz    a1, sys_break           // VMS_BREAK 5
        addi    a1, a1, -1
        beqz    a1, sys_asr             // VMS_ASR 6
        jr      rTAIL
sys_task:                               // ]task: sp and rp from T
        andi    a1, rT, STACK_CAPACITY - 1
        slli    rDSP, a1, 2
        srli    a1, rT, 16
        andi    a1, a1, STACK_CAPACITY - 1
        slli    rRSP, a1, 2
#ifdef TWO_REGISTER_TOS
        addi    a1, rDSP, 4             // N, saved by task[
        andi    a1, a1, MASK4
        add     a1, a1, rBASE
        lw      rN, VMS_DSTACK(a1)
#endif
        jr      rTAIL
sys_fieldplus:
        mv      a7, rT
        jal     a5, fp_full
        mv      rT, a7
        jr      rTAIL
sys_shr:
        lw      a1, VMS_SHIFT(rBASE)
        srl     rT, rT, a1
        jr      rTAIL
sys_asr:
        lw      a1, VMS_SHIFT(rBASE)
        sra     rT, rT, a1
        jr      rTAIL
sys_shl:
        lw      a1, VMS_SHIFT(rBASE)
        sll     rT, rT, a1
        jr      rTAIL
sys_usec:
        SPILL
        call    lfGetTimeMicroSec
        sw      a0, VMS_X(rBASE)
        sw      a1, VMS_Y(rBASE)
        UNSPILL
        jr      rTAIL
sys_break:
        SPILL
        call    lfWatchdogPing          // the app is alive: it reached a `break`
        UNSPILL
        li      a3, ERR_VM_BREAK
        j       fail

tosys_op:
        mv      a3, rT
        DDROP
        andi    a1, rINST, VM_IMM_MASK
        beqz    a1, 1f                  // VMSTO_SHIFT 0
        addi    a1, a1, -1
        beqz    a1, 3f                  // VMSTO_YEET 1
        addi    a1, a1, -1
        bnez    a1, 2f
        sw      a3, VMS_U(rBASE)        // VMSTO_USTORE 2: u = T
2:      jr      rTAIL
3:      bnez    a3, fail                // ior = T if not 0
        jr      rTAIL
1:      andi    a3, a3, 31
        sw      a3, VMS_SHIFT(rBASE)
        jr      rTAIL

fromsys_op:
        DDUP
        andi    a1, rINST, VM_IMM_MASK
        beqz    a1, 1f                  // VMSFROM_TASK 0
        addi    a1, a1, -1
        beqz    a1, 2f                  // VMSFROM_X 1
        addi    a1, a1, -1
        bnez    a1, 3f                  // VMSFROM_Y 2
        lw      rT, VMS_Y(rBASE)
3:      jr      rTAIL
2:      lw      rT, VMS_X(rBASE)
        jr      rTAIL
1:      RDUP                            // task[: rp:sp to T
#ifdef TWO_REGISTER_TOS
        addi    a1, rDSP, 4             // N goes in the cell above sp,
        andi    a1, a1, MASK4           // where ]task reloads it
        add     a1, a1, rBASE
        sw      rN, VMS_DSTACK(a1)
#endif
        slli    rT, rRSP, 14            // (rp << 16) | sp
        srli    a1, rDSP, 2
        or      rT, rT, a1
        jr      rTAIL

api_op:                                 // API and XAPI
        VM_SAVE                         // the API sees and changes VM state,
        SPILL
        mv      a0, rINST
        call    vmApiCall
        mv      a3, a0
        UNSPILL
        VM_LOAD                         // and may run more VM code
        li      rCSPAN, 0               // memory may have been remapped
        bnez    a3, fail
        jr      rTAIL

plusstar_op:                            // multiply step: T:A >> 1, adding N if A odd
        andi    a1, rA, 1
        mv      a3, rT                  // the sum's low 32 bits
        li      a6, 0                   // and its carry
        beqz    a1, 1f
#ifdef TWO_REGISTER_TOS
        add     a3, rT, rN
#else
        add     a1, rBASE, rDSP
        lw      a3, VMS_DSTACK(a1)
        add     a3, a3, rT
#endif
        sltu    a6, a3, rT
1:      slli    a6, a6, 31
        srli    rT, a3, 1
        or      rT, rT, a6
        slli    a3, a3, 31
        srli    rA, rA, 1
        or      rA, rA, a3
        NEXT_UOP

/* Near the handlers, so their branches here stay short. */
mem_error:
        mv      a3, rMERR
        j       fail
uops_done:
        jr      rTAIL

/* ---- micro-op handlers: 64 bytes each ---------------------------------- */

        .balign 1 << UOP_LOG2
uop_table:

.macro UOP n
        .org    uop_table + ((\n) << UOP_LOG2)
.endm

        UOP     VMU_NOP
        NEXT_UOP

        UOP     VMU_INV
        not     rT, rT
        NEXT_UOP

        UOP     VMU_OVER
#ifdef TWO_REGISTER_TOS
        mv      a3, rN
#else
        add     a1, rBASE, rDSP
        lw      a3, VMS_DSTACK(a1)
#endif
        DDUP
        mv      rT, a3
        NEXT_UOP

        UOP     VMU_ASTORE
        mv      rA, rT
        DDROP
        NEXT_UOP

        UOP     VMU_XOR
#ifdef TWO_REGISTER_TOS
        xor     rT, rT, rN
        NIP
#else
        add     a1, rBASE, rDSP
        lw      a3, VMS_DSTACK(a1)
        xor     rT, rT, a3
        NIP
#endif
        NEXT_UOP

        UOP     VMU_PLUS                // cy = carry out of bit 31
#ifdef TWO_REGISTER_TOS
        add     a3, rT, rN
#else
        add     a1, rBASE, rDSP
        lw      a3, VMS_DSTACK(a1)
        add     a3, a3, rT
#endif
        sltu    rCY, a3, rT
        NIP
        mv      rT, a3
        NEXT_UOP

        UOP     VMU_AND
#ifdef TWO_REGISTER_TOS
        and     rT, rT, rN
        NIP
#else
        add     a1, rBASE, rDSP
        lw      a3, VMS_DSTACK(a1)
        and     rT, rT, a3
        NIP
#endif
        NEXT_UOP

        UOP     VMU_PUSH
        mv      a3, rT
        DDROP
        RDUP
        mv      rR, a3
        NEXT_UOP

        UOP     VMU_UNEXT
        addi    rR, rR, -1
        bnez    rR, group               // run the group again
        RDROP
        NEXT_UOP

        UOP     VMU_TWOSTAR
        slli    rT, rT, 1
        NEXT_UOP

        UOP     VMU_DUP
        DDUP
        NEXT_UOP

        UOP     VMU_DROP
        DDROP
        NEXT_UOP

        UOP     VMU_FETCHA
        DDUP
        mv      a3, rA
        jal     a5, mem_fetch
        mv      rT, a3
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_FETCHAPLUS
        DDUP
        mv      a3, rA
        jal     a5, mem_fetch
        mv      rT, a3
        srli    a1, rA, 27
        bnez    a1, fpA_slow
        addi    rA, rA, 1
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_R
        DDUP
        mv      rT, rR
        NEXT_UOP

        UOP     VMU_POP
        DDUP
        mv      rT, rR
        RDROP
        NEXT_UOP

        UOP     VMU_TWODIVC             // rotate right through carry
        andi    a3, rT, 1
        srli    rT, rT, 1
        slli    a1, rCY, 31
        or      rT, rT, a1
        mv      rCY, a3
        NEXT_UOP

        UOP     VMU_TWODIV              // arithmetic shift right
        srai    rT, rT, 1
        NEXT_UOP

        UOP     VMU_FETCHASIGN          // @a, sign-extending a slice
        DDUP
        mv      a3, rA
        jal     a5, mem_fetch
        beqz    a1, 1f
        neg     a6, a1                  // shift by 32 - size, both ways
        sll     a3, a3, a6
        sra     a3, a3, a6
1:      mv      rT, a3
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_PXSTORE             // reserved for the page extension: a drop
        DDROP
        NEXT_UOP

        UOP     VMU_STOREA
        mv      a7, rT
        DDROP
        mv      a3, rA
        jal     a5, mem_store
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_STOREAPLUS
        mv      a7, rT
        DDROP
        mv      a3, rA
        jal     a5, mem_store
        srli    a1, rA, 27
        bnez    a1, fpA_slow
        addi    rA, rA, 1
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_STOREB
        mv      a7, rT
        DDROP
        mv      a3, rB
        jal     a5, mem_store
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_STOREBPLUS
        mv      a7, rT
        DDROP
        mv      a3, rB
        jal     a5, mem_store
        srli    a1, rB, 27
        bnez    a1, fpB_slow
        addi    rB, rB, 1
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_SWAP
#ifdef TWO_REGISTER_TOS
        mv      a3, rN
        mv      rN, rT
#else
        add     a1, rBASE, rDSP
        lw      a3, VMS_DSTACK(a1)
        sw      rT, VMS_DSTACK(a1)
#endif
        mv      rT, a3
        NEXT_UOP

        UOP     VMU_PLUSSTAR
        j       plusstar_op

        UOP     VMU_B
        DDUP
        mv      rT, rB
        NEXT_UOP

        UOP     VMU_BSTORE
        mv      rB, rT
        DDROP
        NEXT_UOP

        UOP     VMU_FETCHB
        DDUP
        mv      a3, rB
        jal     a5, mem_fetch
        mv      rT, a3
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_FETCHBPLUS
        DDUP
        mv      a3, rB
        jal     a5, mem_fetch
        mv      rT, a3
        srli    a1, rB, 27
        bnez    a1, fpB_slow
        addi    rB, rB, 1
        bnez    rMERR, mem_error
        NEXT_UOP

        UOP     VMU_A
        DDUP
        mv      rT, rA
        NEXT_UOP

        UOP     VMU_CY
        DDUP
        mv      rT, rCY
        NEXT_UOP

        .org    uop_table + (32 << UOP_LOG2)

/* ---- 9-bit immediate handlers: 32 bytes each --------------------------- */

        .balign 1 << IMM_LOG2
imm_table:

.macro IMM n
        .org    imm_table + ((\n) << IMM_LOG2)
.endm

        IMM     VMO_ZBRAN               // 0: if
        mv      a3, rT
        DDROP
        beqz    a3, branch
        jr      rTAIL

        IMM     VMO_BRAN                // 1
        j       branch

        IMM     VMO_PBRAN               // 2: -if
        bgez    rT, branch
        jr      rTAIL

        IMM     VMO_RCALL               // 3
        RDUP
        mv      rR, rPC
        j       branch

        IMM     VMO_NEXT                // 4
        addi    rR, rR, -1
        bnez    rR, branch
        RDROP
        jr      rTAIL

        IMM     5
        li      a3, ERR_INVALID_OPCODE
        j       fail

        IMM     VMO_SYS                 // 6
        j       sys_op

        IMM     7
        li      a3, ERR_INVALID_OPCODE
        j       fail

        IMM     VMO_TOSYS               // 8
        j       tosys_op

        IMM     VMO_USER                // 9: a = u + imm
        andi    a1, rINST, VM_IMM_MASK
        lw      rA, VMS_U(rBASE)
        add     rA, rA, a1
        jr      rTAIL

        IMM     VMO_FROMSYS             // 10
        j       fromsys_op

        IMM     11
        li      a3, ERR_INVALID_OPCODE
        j       fail

        IMM     VMO_PFX                 // 12
        andi    a1, rINST, VM_IMM_MASK
        slli    rPFX, rPFX, VM_IMM_BITS + 1
        or      rPFX, rPFX, a1
        jr      rTAIL

        IMM     VMO_PFX1                // 13
        andi    a1, rINST, VM_IMM_MASK
        ori     a1, a1, 1 << VM_IMM_BITS
        slli    rPFX, rPFX, VM_IMM_BITS + 1
        or      rPFX, rPFX, a1
        jr      rTAIL

        IMM     VMO_API0                // 14
        j       api_op

        IMM     VMO_API1                // 15
        j       api_op

        .org    imm_table + (16 << IMM_LOG2)

        .size   vmExecAsm, . - vmExecAsm
        .option pop
