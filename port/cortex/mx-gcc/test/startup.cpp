// Startup code and vector table for the cm4sim model
#include <stdint.h>
#include <stddef.h>
extern "C" void *memcpy(void *, const void *, size_t);
extern "C" void *memset(void *, int, size_t);

int main();
extern "C" {
extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
extern void (*__init_array_start[])(), (*__init_array_end[])();
void PendSV_Handler();
void SysTick_Handler();
void IRQ0_Handler();
void IRQ1_Handler();
void IRQ2_Handler();
void IRQ3_Handler();

__attribute__((noreturn)) static void dev_exit(uint32_t code) { *(volatile uint32_t *)0x40000004UL = code; for(;;) { } }
static void dev_puts(const char *s) { while(*s) *(volatile uint32_t *)0x40000000UL = *s++; }

__attribute__((noreturn)) void Default_Handler()
{
    uint32_t ipsr; __asm__ volatile ("MRS %0, IPSR" : "=r"(ipsr));
    dev_puts("unexpected exception\n");
    dev_exit(100 + ipsr);
}

__attribute__((noreturn)) void Reset_Handler()
{
    memcpy(&_sdata, &_sidata, (char *)&_edata - (char *)&_sdata);
    memset(&_sbss, 0, (char *)&_ebss - (char *)&_sbss);
#ifndef __SOFTFP__
    *(volatile uint32_t *)0xE000ED88UL |= 0xFu << 20;      // CPACR: enable CP10/CP11
    __asm__ volatile ("DSB\n ISB");
#endif
    for(void (**f)() = __init_array_start; f < __init_array_end; f++) (*f)();
    main();
    dev_exit(99);
}

// simple freestanding library
void *memcpy(void *d, const void *s, size_t n) { char *dd = (char *)d; const char *ss = (const char *)s; while(n--) *dd++ = *ss++; return d; }
void *memset(void *d, int c, size_t n) { char *dd = (char *)d; while(n--) *dd++ = (char)c; return d; }
void *memmove(void *d, const void *s, size_t n) { char *dd = (char *)d; const char *ss = (const char *)s; if(dd < ss) while(n--) *dd++ = *ss++; else while(n--) dd[n] = ss[n]; return d; }
void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }
int __cxa_guard_acquire(uint8_t *g) { return !*g; }
void __cxa_guard_release(uint8_t *g) { *g = 1; }
void __cxa_pure_virtual() { dev_exit(98); }
int __aeabi_atexit(void *, void (*)(void *), void *) { return 0; }
void *__dso_handle = 0;

#define W(x) __attribute__((weak, alias("Default_Handler"))) void x();
W(NMI_Handler) W(HardFault_Handler) W(MemManage_Handler) W(BusFault_Handler) W(UsageFault_Handler)
W(SVC_Handler) W(DebugMon_Handler)

__attribute__((section(".isr_vector"), used))
void (* const vectors[16 + 32])() =
{
    (void (*)())&_estack, Reset_Handler, NMI_Handler, HardFault_Handler,
    MemManage_Handler, BusFault_Handler, UsageFault_Handler, 0, 0, 0, 0,
    SVC_Handler, DebugMon_Handler, 0, PendSV_Handler, SysTick_Handler,
    IRQ0_Handler, IRQ1_Handler, IRQ2_Handler, IRQ3_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
    Default_Handler, Default_Handler, Default_Handler, Default_Handler,
};
}
