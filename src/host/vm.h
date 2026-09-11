#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

extern int32_t* vm_memory[8];              // pointer to data for the VM
extern uint32_t vm_memory_rd_limit[8];     // index limits for memory read
extern uint32_t vm_memory_wp_limit[8];     // index limits for memory write-protect
extern uint32_t vm_memory_executable[8];   // execution limit (<= vm_memory_rd_limit)

int32_t vmRun(int mode, uint32_t inst, int32_t data);

#define VM_ENDED_NORMALLY   0x40000000
#define VM_BAD_CODE_ADDR    -100
#define VM_BAD_DATA_ADDR    -101

#define STACK_CAPACITY 64
#define STACK_MASK            (STACK_CAPACITY - 1)
#if (STACK_CAPACITY <= 0) || ((STACK_CAPACITY & STACK_MASK) != 0)
#error "STACK_CAPACITY must be a non-zero power of 2 for masking to work."
#endif

#endif /* _VM_H_ */