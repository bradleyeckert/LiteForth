/*
 * Prints the API 0 table as "index id function", one entry per line.
 * The makefile compares this with expected.txt, so reordering, removing or
 * inserting entries in api0_list.h fails the test. Compiled code saved in
 * flash depends on these indices.
 * After deliberately appending an entry, update expected.txt:
 *   make update
 */
#include <stdio.h>
#include "api0.h"

#define API0_PRINT(id, fn) printf("%3d %-20s %s\n", id, #id, #fn);

int main(void) {
    API0_LIST(API0_PRINT)
    API0_TOOLS_LIST(API0_PRINT)
    printf("count %d\n", API0_COUNT);
    return 0;
}
