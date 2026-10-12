/* A minimal runtime for difftest.c in rv32emu: start-up, output, exit and
   the memory functions the compiler may call. */
#include <stdint.h>
#include <stddef.h>

int main(void);
void out(const char* s, int n);

void out(const char* s, int n) {
    register int a0 __asm__("a0") = 1;
    register const char* a1 __asm__("a1") = s;
    register int a2 __asm__("a2") = n;
    register int a7 __asm__("a7") = 64;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

void rt_exit(int code);
void rt_exit(int code) {
    register int a0 __asm__("a0") = code;
    register int a7 __asm__("a7") = 93;
    __asm__ volatile("ecall" : : "r"(a0), "r"(a7));
    for (;;) { }
}

__attribute__((naked, section(".text.start"))) void _start(void) {
    __asm__ volatile("call main\n\tcall rt_exit");
}

void* memset(void* d, int c, size_t n);
void* memset(void* d, int c, size_t n) {
    unsigned char* p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}
void* memcpy(void* d, const void* s, size_t n);
void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* p = d;
    const unsigned char* q = s;
    while (n--) *p++ = *q++;
    return d;
}
