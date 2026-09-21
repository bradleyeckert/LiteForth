#include "memalloc.h"
#include "errcodes.h"
#include "options.h"

/*
* Bump Allocator for memory management
*
* You were expecting a real `malloc` and `free`? They invite trouble.
* Instead, manage a static pool by freeing memory regions in reverse order
* of how you allocated them. If you mess up the order, pool_free says so.
*/

static uint32_t pool[POOL_CAPACITY];
static size_t stack_top = 0;

void* pool_alloc(int size) {
    if (size <= 0 || stack_top + size + 1 > POOL_CAPACITY) {
        return NULL;
    }

    // The data block starts at the current stack top
    void* ptr = &pool[stack_top];
    
    // Move past the data block, then store the size at the trailing slot
    stack_top += size;
    pool[stack_top] = (uint32_t)size; 
    
    // Move past the metadata slot
    stack_top += 1;
    
    return ptr;
}

int pool_free(void* ptr) {
    if (!ptr) return 0;
    if (stack_top == 0) return ERR_FREE_FAILED; // Stack is empty

    // The trailing metadata slot is always exactly 1 slot below stack_top
    uint32_t size = pool[stack_top - 1];
    
    // Calculate where the data block started based on its size
    uint32_t* actual_top_ptr = &pool[stack_top - 1 - size];

    if (ptr != actual_top_ptr) {
        return ERR_FREE_FAILED; // Misordered free violation
    }

    // Rewind the stack pointer to completely reclaim the data and metadata
    stack_top -= (size + 1); 
    return 0;
}

void pool_reset(void) {
    stack_top = 0;
}

int pool_unused(void) {
    return (int)(POOL_CAPACITY - stack_top);
}
