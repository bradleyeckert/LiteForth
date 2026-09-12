#ifndef _VM_H_
#define _VM_H_

#include <stdint.h>

#define VM_SEGMENT_BITS 3
#define VM_SEGMENTS (1 << VM_SEGMENT_BITS)

/*
The virtual machine's entire 22-bit memory space is divided into segments.
VM_SEGMENTS is the number of memory segments in the virtual machine.
Each segment can have its own read, write-protect, and executable limits.
- Code reads are valid from 0 to `vm_memory_executable`-1.
- Data reads are valid from 0 to `vm_memory_rd_limit`-1.
- Data writes are valid from `vm_memory_wp_limit` to `vm_memory_rd_limit`-1.
*/

extern int32_t* vm_memory[VM_SEGMENTS];           
extern uint32_t vm_memory_rd_limit[VM_SEGMENTS];  
extern uint32_t vm_memory_wp_limit[VM_SEGMENTS];  
extern uint32_t vm_memory_executable[VM_SEGMENTS];

int32_t vmRun(int mode, uint32_t inst, int32_t data);

#define VM_ENDED_NORMALLY   0x80000000
#define VM_BAD_CODE_ADDR    -100
#define VM_BAD_DATA_ADDR    -101

#define STACK_CAPACITY 64
#define STACK_MASK            (STACK_CAPACITY - 1)
#if (STACK_CAPACITY <= 0) || ((STACK_CAPACITY & STACK_MASK) != 0)
#error "STACK_CAPACITY must be a non-zero power of 2 for masking to work."
#endif

#endif /* _VM_H_ */