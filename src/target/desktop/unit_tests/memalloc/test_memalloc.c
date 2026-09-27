#include "../../memalloc.h"
#include "../../errcodes.h"
#include "../../options.h"
#include <stdio.h>
#include <assert.h>

void test_basic_alloc_free(void) {
    pool_reset();
    int initial_unused = pool_unused();
    assert(initial_unused == POOL_CAPACITY);

    int size1 = 10;
    uint32_t* ptr1 = (uint32_t*)pool_alloc(size1);
    assert(ptr1 != NULL);
    
    // Check unused space drops by size + 1 metadata slot
    assert(pool_unused() == initial_unused - (size1 + 1));

    int size2 = 20;
    uint32_t* ptr2 = (uint32_t*)pool_alloc(size2);
    assert(ptr2 != NULL);

    // Correct LIFO free order (ptr2 then ptr1)
    assert(pool_free(ptr2) == 0);
    assert(pool_free(ptr1) == 0);
    assert(pool_unused() == POOL_CAPACITY);
    printf("test_basic_alloc_free passed!\n");
}

void test_lifo_violation(void) {
    pool_reset();
    
    uint32_t* ptr1 = (uint32_t*)pool_alloc(5);
    uint32_t* ptr2 = (uint32_t*)pool_alloc(10);

    // Freeing ptr1 out of order must fail
    assert(pool_free(ptr1) == ERR_FREE_FAILED);

    // Freeing ptr2 should still work
    assert(pool_free(ptr2) == 0);
    // Now ptr1 is the top, so freeing it works
    assert(pool_free(ptr1) == 0);
    
    printf("test_lifo_violation passed!\n");
}

void test_reset(void) {
    pool_reset();
    pool_alloc(10);
    pool_alloc(20);
    
    assert(pool_unused() < POOL_CAPACITY);
    pool_reset();
    assert(pool_unused() == POOL_CAPACITY);
    
    printf("test_reset passed!\n");
}

void test_oom(void) {
    pool_reset();
    // Allocate almost everything
    void* ptr = pool_alloc(POOL_CAPACITY - 1);
    assert(ptr != NULL);
    assert(pool_unused() == 0);

    // Next one must fail
    void* ptr_fail = pool_alloc(1);
    assert(ptr_fail == NULL);

    printf("test_oom passed!\n");
}

int main(void) {
    printf("Running unit tests for memalloc...\n");
    test_basic_alloc_free();
    test_lifo_violation();
    test_reset();
    test_oom();
    printf("All tests passed successfully!\n");
    return 0;
}
