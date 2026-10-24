/* Tests vmTrap (src/vm.c): a CPU exception during a VM run ends the
   innermost vmRun with its ior. A stepped app goes to its yeet handler,
   a word call gets its PC back, nested runs unwind one level, and
   vmTrapArmed says when a trap may be routed. API calls stand in for the
   CPU exception, calling vmTrap as the CH32H417's trap_resume does. */
#include <stdio.h>
#include <stdint.h>
#include "vm.h"
#include "vm_labels.h"
#include "errcodes.h"

uint32_t g_lf_sys_options;
uint64_t lfGetTimeMicroSec(void) { return 0; }
void lfWatchdogPing(void) { }
int iomap_rd(int addr, uint32_t *data) { (void)addr; *data = 0; return -9; }   /* no I/O ports here */
int iomap_wr(int addr, uint32_t data) { (void)addr; (void)data; return -9; }

#define PAGE 1
#define CODE(i) (((uint32_t)PAGE << (23 - VM_LOG2_PAGES)) + (i))  /* instruction address */
#define API(n)  ((uint16_t)(VMI_API0 | (n)))

static uint32_t code[16];
static int failures;
static int armed_in_api, depth_in_api;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void put(int i, uint16_t inst) {   /* instruction i: even ones low */
    uint32_t shift = (i & 1) ? 16 : 0;
    code[i >> 1] = (code[i >> 1] & ~(0xFFFFu << shift)) | ((uint32_t)inst << shift);
}

int VMapi0Call(int fn) {
    armed_in_api = vmTrapArmed();
    depth_in_api = vmRunning();
    switch (fn) {
    case 1: vmTrap(ERR_BUS_FAULT);          /* the CPU faults here */
    case 2: {                               /* a nested word call that faults */
        int32_t ior = vmRun(0, 0, (int32_t)CODE(8));
        check(ior == ERR_BUS_FAULT, "nested: the inner run returns the trap's ior");
        check(vmRunning() == 1 && vmTrapArmed(), "nested: back at depth 1, still armed");
        return ior;
    }
    case 3: return 0;                       /* an API call that works */
    default: return ERR_INVALID_API_CALL;
    }
}
int VMapi1Call(int fn) { (void)fn; return ERR_INVALID_API_CALL; }

int main(void) {
    vm_memory[PAGE] = code;
    vm_memory_rd_limit[PAGE] = 16;
    vm_memory_executable[PAGE] = 16;
    put(0, API(3));                         /* the app: works once, */
    put(1, API(1));                         /* then faults */
    put(4, API(1));                         /* a word that faults */
    put(6, API(2));                         /* a word whose API call faults in a nested run */
    put(8, API(1));

    check(!vmTrapArmed() && vmRunning() == 0, "not armed outside vmRun");

    int32_t ior = vmRun(1, API(1), 0);      /* one instruction */
    check(ior == ERR_BUS_FAULT, "once: the trap's ior");
    check(armed_in_api && depth_in_api == 1, "once: armed inside");
    check(!vmTrapArmed() && vmRunning() == 0, "once: disarmed after");

    lf_vm.PC = 0x1234;                      /* the app's PC, during a word call */
    ior = vmRun(0, 0, (int32_t)CODE(4));
    check(ior == ERR_BUS_FAULT, "word call: the trap's ior");
    check(lf_vm.PC == 0x1234, "word call: the app's PC back");
    check(!vmTrapArmed() && vmRunning() == 0, "word call: disarmed after");

    lf_vm.PC = CODE(0);                     /* the app, stepped */
    lf_vm.X = lf_vm.Y = 0;
    ior = vmRun(0, 100, 0);
    check(ior == ERR_BUS_FAULT, "app: the trap's ior");
    check(lf_vm.PC == VM_YEET_ADDRESS << 1, "app: PC at the yeet handler");
    check(lf_vm.Y == (uint32_t)ERR_BUS_FAULT, "app: Y = the ior");
    check(lf_vm.X == CODE(2), "app: X = the PC last saved (just after the API call that faulted)");
    check(!vmTrapArmed() && vmRunning() == 0, "app: disarmed after");

    lf_vm.PC = 0x5678;
    ior = vmRun(0, 0, (int32_t)CODE(6));    /* nested: the outer run carries on */
    check(ior == ERR_BUS_FAULT, "nested: the outer word gets the API call's ior");
    check(depth_in_api == 2, "nested: the trap was two runs deep");
    check(lf_vm.PC == 0x5678, "nested: the app's PC back");
    check(!vmTrapArmed() && vmRunning() == 0, "nested: disarmed after");

    ior = vmRun(1, API(3), 0);              /* and the VM still works */
    check(ior == 0, "an API call after the traps");

    if (failures) {
        printf("traps: %d checks FAILED\n", failures);
        return 1;
    }
    printf("traps: passed\n");
    return 0;
}
