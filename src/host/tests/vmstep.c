// --- Cross-Compiler Code Alignment Macros ---
#if defined(_MSC_VER)
    // Microsoft Visual Studio (MSVC)
    #define CACHE_ALIGN __declspec(code_seg(".text")) __declspec(align(64))
#elif defined(__GNUC__) || defined(__clang__)
    // GCC (STM32CubeIDE) and Clang
    #define CACHE_ALIGN __attribute__((aligned(64)))
#else
    #define CACHE_ALIGN
#endif

// --- Performance-Critical Generic C Function ---
// This function will sit perfectly at a 64-byte boundary in both IDEs
CACHE_ALIGN void Process_Generic_Core_Loop(void) {
    // Your processing, matrix math, or bytecode evaluation loop here
}
