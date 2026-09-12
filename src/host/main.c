#include "forth.h"
#include "vm.h"
#include "serial_io.h"

#define TEST_MEM_SIZE 1024

int main(void) {

    int32_t code_page0[TEST_MEM_SIZE];
    int32_t ram_page[TEST_MEM_SIZE];

    for (int i = 0; i < VM_SEGMENTS; i++) {
        vm_memory[i] = code_page0;
        vm_memory_rd_limit[i] = TEST_MEM_SIZE;
        vm_memory_wp_limit[i] = 0;
        vm_memory_executable[i] = TEST_MEM_SIZE;
    }
    vm_memory[1] = ram_page;
	serial_open(NULL, 0); // Open in terminal mode
    QUIT();
    serial_close();
    return 0;
}