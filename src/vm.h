#ifndef _VM_H_
#define _VM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "options.h"

#define VM_MEM_PAGES (1 << VM_LOG2_PAGES)

/*
The virtual machine's entire 22-bit memory space is divided into segments.
VM_SEGMENTS is the number of memory segments in the virtual machine.
Each segment can have its own read, write-protect, and executable limits.
- Code reads are valid from 0 to `vm_memory_executable`-1.
- Data reads are valid from 0 to `vm_memory_rd_limit`-1.
- Data writes are valid from `vm_memory_wp_limit` to `vm_memory_rd_limit`-1.
These pointers take 256 bytes of RAM if VM_SEGMENTS = 8.
*/

extern uint32_t* vm_memory[VM_MEM_PAGES];           
extern uint32_t vm_memory_rd_limit[VM_MEM_PAGES];  
extern uint32_t vm_memory_wp_limit[VM_MEM_PAGES];  
extern uint32_t vm_memory_executable[VM_MEM_PAGES];
extern char* vm_memory_name[VM_MEM_PAGES];

/**
 * @name VM Execution and State Control
 * @{
 */

 /**
  * @brief Simulates a Forth CPU
  * 
  * - vmRun(1, inst, 0)
  * - vmRun(0, steps, 0)
  * - vmRun(0, 0, address)
  *
  * @param once `1` to execute the single instruction `inst`,
  * `0` to execute code from memory.
  * @param inst 16-bit instruction to execute, or number of steps to run
  * from the current PC. If 0 steps, calls the word at `address` and runs
  * until it returns.
  * @param address Instruction address of the word to run (0 steps only).
  * @return Return code IOR, see errcodes.h.
  *
  * Stepping (steps > 0) runs the app from PC. If it fails (anything but
  * ERR_VM_BREAK or ERR_VM_TIMEOUT), X gets the PC just after the failing
  * instruction, Y the error code, and PC the yeet handler at cell
  * VM_YEET_ADDRESS, so the app handles its own errors. The error is still
  * returned.
  *
  * Calling a word (0 steps) runs the terminal's code and is re-entrant: a
  * word may make an API call that runs other words through vmRun. PC is
  * always restored to its value before the call, and errors are only
  * returned, so they can't disturb the app.
  */
int32_t vmRun(int once, uint32_t inst, int32_t address);

/**
 * @brief Reads from the memory space
 *
 * A slice address returns just that bit field, zero-extended.
 *
 * @param addr Cell or slice address to fetch
 * @param data Pointer to destination of the read data
 * @return ior, 0 if okay, or ERR_INVALID_ADDRESS at or past the page's read limit
 */
int vmFetch(uint32_t addr, uint32_t* data);

/**
 * @brief Writes to the memory space
 *
 * A slice address writes just that bit field, leaving the rest of the cell unchanged.
 *
 * @param addr Cell or slice address to store
 * @param data 32-bit data to store (truncated to the slice width)
 * @return ior, 0 if okay, ERR_INVALID_ADDRESS at or past the page's read limit,
 *         or ERR_WRITE_PROTECTED below the page's write-protect limit
 */
int vmStore(uint32_t addr, uint32_t data);

/**
 * @brief Calculates the next RAM address
 *
 * A cell address advances by one cell. A slice address advances to the next
 * slice of the same width, moving to the next cell when the slice would not fit.
 *
 * @param addr Cell or bitfield address
 * @return Next cell or bitfield address
 */
uint32_t vmFieldPlus(uint32_t addr);

/**
 * @brief Reads a VM register or data stack item.
 *
 * @param reg -1 to pop the data stack; 0 for the top of stack; 1 to
 *        STACK_MASK-1 for the item that many places below the top; or a
 *        VM_REG_* register number from vm_labels.h.
 * @return The value, or -1 if reg is not valid.
 */
int32_t vmPeek(int reg);

/**
 * @brief Writes a VM register or data stack item.
 *
 * @param reg -1 to push data onto the data stack; otherwise the same
 *        numbering as vmPeek. Writes to cy, sp and rp are masked to their width.
 * @param data 32-bit signed value to store.
 * @return 0 on success, or -1 if reg is not valid.
 */
int32_t vmPoke(int reg, int32_t data);

/**
 * @brief Resets the VM registers and empties both stacks.
 *
 * Clears PC, R, A, B, U, cy, sp and rp, and sets the empty-stack markers.
 * Memory and memory limits are not changed.
 *
 * @return 0.
 */
int32_t vmReset(void);

/** @} */

// When the VM yeets an error, the PC is loaded with this cell address.
// 0 = cold boot address
// 1 = reserved for address of system data
// 2 = yeet address
#define VM_YEET_ADDRESS  2

// Data stack caching. With TWO_REGISTER_TOS, the top two data stack items
// are kept in registers T and N, and datastack holds the third and below.
// Without it, only T is a register and datastack[sp] is the second item.
// Either way sp counts the items. Comment it out for the one-register VM.
//#define TWO_REGISTER_TOS
#ifdef TWO_REGISTER_TOS
#define TOS_REGISTERS         2
#else
#define TOS_REGISTERS         1
#endif

// Check the stack configuration
#define STACK_MASK            (STACK_CAPACITY - 1)
#if (STACK_CAPACITY <= 0) || ((STACK_CAPACITY & STACK_MASK) != 0)
#error "STACK_CAPACITY must be a non-zero power of 2 for masking to work."
#endif
#if (STACK_CAPACITY < 32)
#error "STACK_CAPACITY must be at least 32."
#endif

#define VM_EMPTYSTACK 0xAAAAAAAA

// FALLTHROUGH at the end of a case statement indicates that fallthrough is
// intentional. It keeps the compiler from issuing a warning.
#ifndef FALLTHROUGH

// 1. C23 Standard Attribute
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define FALLTHROUGH [[fallthrough]]

// 2. C++17 Standard Attribute
#elif defined(__cplusplus) && __cplusplus >= 201703L
#define FALLTHROUGH [[fallthrough]]

// 3. Clang (handles both C and C++ via feature check)
#elif defined(__clang__) && defined(__has_attribute)
#if __has_attribute(fallthrough)
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define FALLTHROUGH ((void)0)
#endif

// 4. GCC 7.0+
#elif defined(__GNUC__) && (__GNUC__ >= 7)
#define FALLTHROUGH __attribute__((fallthrough));

// 5. MSVC (Visual Studio 2015 update 3+ via Code Analysis)
#elif defined(_MSC_VER)
#if _MSC_VER >= 1900
#define FALLTHROUGH __fallthrough
#else
#define FALLTHROUGH ((void)0)
#endif

// 6. Fallback for legacy or unknown compilers
#else
#define FALLTHROUGH ((void)0)
#endif

#endif // FALLTHROUGH

/* ======================================================================== */
/* SYSTEM OPTIONS                                                           */
/* ======================================================================== */

extern uint32_t g_lf_sys_options; // used in forth.c, main.c, vm.c

#define SYS_OPTIONS_LOCKED    0x8000	/* `>options` ignores changes		*/
#define SYS_OPTION_NO_API     0x4000	/* disallow the use of API calls    */
#define SYS_OPTION_ONLY_TERM  0x2000	/* only allow terminal API calls    */
#define SYS_OPTION_NO_AUTORUN 0x0100    /* booting doesn't start the app    */
#define SYS_OPTION_USE_COLORS 0x0080	/* use color messages				*/
#define SYS_OPTION_VERBOSE    0x0040	/* echo input lines					*/
#define SYS_OPTION_IGNORE_CR  0x0020	/* ignore CR						*/
#define SYS_OPTION_RUNNING    0x0010    /* run the VM                       */
#define SYS_OPTION_BOOTING    0x0008    /* boot from Flash                  */
#define SYS_OPTION_VALIDATION 0x0004	/* quit immediately upon error		*/
#define SYS_OPTION_NO_DOTESS  0x0002	/* do not display the stack			*/
#define SYS_OPTION_NO_OK      0x0001	/* do not display "ok>"				*/

#ifdef __cplusplus
}
#endif

#endif /* _VM_H_ */