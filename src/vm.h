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

extern int32_t* vm_memory[VM_MEM_PAGES];           
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
  * @param once `1` if single instruction, `0` if executing code.
  * @param inst 16-bit instruction to execute, or number of steps.
  * If 0 steps, run until last `;` starting at `address`.
  * @param address Address of word to run.
  * @return Return code IOR, see errcodes.h.
  */
int32_t vmRun(int once, uint32_t inst, int32_t address);

/**
 * @brief Reads from the memory space
 *
 * @param addr Address to fetch
 * @param *data Pointer to destination of the read data
 * @return ior, 0 if okay
 */
int vmFetch(uint32_t addr, int32_t* data);

/**
 * @brief Writes to the memory space
 *
 * @param addr Address to store
 * @param data 32-bit data to store
 * @return ior, 0 if okay
 */
int vmStore(uint32_t addr, int32_t data);

/**
 * @brief Calculates the next RAM address
 *
 * @param addr Cell or bitfield address
 * @return Next cell or bitfield address
 */
int32_t vmFieldPlus(int32_t addr);

/**
 * @brief Reads the contents of a specific VM register.
 *
 * @param reg Register to fetch, pop if -1. See vm_labels.h.
 * @return The 32-bit signed value stored in the specified register.
 */
int32_t vmPeek(int reg);

/**
 * @brief Writes a value directly to a VM register.
 *
 * @param reg Register to store, push if -1. See vm_labels.h.
 * @param data 32-bit signed value to store in the register.
 * @return Previous register value or status indicator.
 */
int32_t vmPoke(int reg, int32_t data);

/**
 * @brief Resets the virtual machine registers, memory limits, and execution state.
 *
 * @return Status code indicating successful reset (0 on success).
 */
int32_t vmReset(void);

/** @} */


/**
 * @name VM API Dispatch Handlers
 * @{
 */

/**
 * @brief Invokes the LiteForth API function handler (API 0).
 *
 * @param fn API function identifier or dispatch ID to execute.
 * @return IOR result code (see errcodes.h) from host function call.
 */
int VMapi0Call(int fn);

/**
 * @brief Invokes a user API function handler (API 1).
 *
 * @param fn API function identifier or dispatch ID to execute.
 * @return IOR result code (see errcodes.h) from host function call.
 */
int VMapi1Call(int fn);

/** @} */

#define STACK_MASK            (STACK_CAPACITY - 1)
#if (STACK_CAPACITY <= 0) || ((STACK_CAPACITY & STACK_MASK) != 0)
#error "STACK_CAPACITY must be a non-zero power of 2 for masking to work."
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
#define FALLTHROUGH __attribute__((fallthrough))

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
#define SYS_OPTION_USE_COLORS 0x0080	/* use color messages				*/
#define SYS_OPTION_VERBOSE    0x0040	/* echo input lines					*/
#define SYS_OPTION_IGNORE_CR  0x0020	/* ignore CR						*/
#define SYS_OPTION_NO_BLOCK   0x0010	/* do not create blocks file		*/
#define SYS_OPTION_NO_FLASH   0x0008	/* do not create flash file			*/
#define SYS_OPTION_VALIDATION 0x0004	/* quit immediately upon error		*/
#define SYS_OPTION_NO_DOTESS  0x0002	/* do not display the stack			*/
#define SYS_OPTION_NO_OK      0x0001	/* do not display "ok>"				*/

#ifdef __cplusplus
}
#endif

#endif /* _VM_H_ */