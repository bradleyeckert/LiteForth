#include <stdint.h>
#include <stdio.h>
#include "vm.h"
#include "vm_labels.h"

#define TEST_MEM_SIZE 1024

int main(void) {
    // 1. Allocate buffer for page 0
    int32_t code_page0[TEST_MEM_SIZE];

    // 2. Set up page 0 limits and pointers
    vm_memory[0] = code_page0;
    vm_memory_rd_limit[0] = TEST_MEM_SIZE;
    vm_memory_wp_limit[0] = 0;
    vm_memory_executable[0] = TEST_MEM_SIZE;

    // Zero out remaining pages
    for (int page = 1; page < 8; page++) {
        vm_memory[page] = NULL;
        vm_memory_rd_limit[page] = 0;
        vm_memory_wp_limit[page] = 0;
        vm_memory_executable[page] = 0;
    }

    // 3. Construct a test program in code_page0
    // Each 32-bit word packs two 16-bit instructions (high word first, then low word)

    // Word 0: Push literal 42 to stack, then NOP micro-ops
    // High 16 bits: Literal instruction (0x6000 | 42) -> VMI_LIT + 42
    // Low 16 bits:  UOP group containing NOPs (0x8000)
    uint16_t inst_lit = VMI_LIT | 42;
    uint16_t inst_uop_nop = VM_UOPS | (VMU_NOP << SLOT0_POSITION);
    code_page0[0] = ((uint32_t)inst_lit << 16) | inst_uop_nop;

    // Word 1: Jump back to PC 0 (Infinite loop)
    // High 16 bits: Branch back by -2 half-words
    // Low 16 bits:  NOP UOP group
    int16_t branch_offset = -2 & 0x1FF;
    uint16_t inst_branch = VMI_PFX | (VMO_BRAN << 9) | branch_offset;
    code_page0[1] = ((uint32_t)inst_branch << 16) | inst_uop_nop;

    // 4. Reset the VM state (mode 4)
    printf("Resetting VM state...\n");
    int32_t status = vmRun(4, 0, 0);
    if (status != 0) {
        fprintf(stderr, "VM Reset failed with status: %d\n", status);
        free(code_page0);
        return 1;
    }

    // 5. Execute 100 steps starting at PC 0 (mode 0)
    printf("Executing 100 VM steps...\n");
    status = vmRun(0, 100, 0);

    printf("Execution completed with status / IOR code: 0x%X\n", status);

    // 6. Inspect final registers (mode 3)
    int32_t final_T = vmRun(3, 0, 0); // Reg 0: Top of Stack
    int32_t final_PC = vmRun(3, 1, 0); // Reg 1: Program Counter
    int32_t final_SP = vmRun(3, 8, 0); // Reg 8: Stack Pointer

    printf("Final VM State -> PC: %d, SP: %d, Top of Stack (T): %d\n",
        final_PC, final_SP, final_T);

    return 0;
}