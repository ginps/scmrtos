//******************************************************************************
//*
//*     scmRTOS 16-bit target check: compile-only stub port
//*
//*     Not a real port: nothing here can run. It only lets the kernel headers
//*     be compiled by clang for a target with a 16-bit int (MSP430, AVR), so
//*     that the compiler evaluates the kernel's process-map expressions with
//*     the integer promotion rules of those targets (see check.cpp, run.sh).
//*
//*     get_prio_tag() and highest_priority() repeat the code of the
//*     AVR/MSP430/STM8 ports.
//*
//******************************************************************************
#ifndef scmRTOS_TARGET16_H
#define scmRTOS_TARGET16_H

#define INLINE      __attribute__((__always_inline__)) inline
#define NOINLINE    __attribute__((__noinline__))
#define NORETURN    __attribute__((__noreturn__))

typedef uint16_t stack_item_t;
typedef uint16_t status_reg_t;

#define OS_PROCESS   __attribute__((__noreturn__))
#define OS_INTERRUPT extern "C"
#define DUMMY_INSTR()
#define SEPARATE_RETURN_STACK          0
#define scmRTOS_CONTEXT_SWITCH_SCHEME  0
#ifndef scmRTOS_PRIORITY_ORDER
#define scmRTOS_PRIORITY_ORDER         0
#endif

typedef uint16_t timeout_t;
typedef uint32_t tick_count_t;
#ifndef scmRTOS_PROCESS_COUNT
#define scmRTOS_PROCESS_COUNT                   16      // 16 user processes + idle: 17-bit map
#endif
#define scmRTOS_SYSTIMER_NEST_INTS_ENABLE       0
#define scmRTOS_SYSTEM_TICKS_ENABLE             1
#define scmRTOS_SYSTIMER_HOOK_ENABLE            0
#define scmRTOS_IDLE_HOOK_ENABLE                0
#define scmRTOS_CONTEXT_SWITCH_USER_HOOK_ENABLE 0
#define scmRTOS_IDLE_PROCESS_STACK_SIZE         64

#include <scmRTOS_defs.h>

#define scmRTOS_STACK_PATTERN 0xABBA

class TCritSect { public: TCritSect() { } ~TCritSect() { } };
#define SYS_TIMER_CRIT_SECT()
#define LOCK_SYSTEM_TIMER()
#define UNLOCK_SYSTEM_TIMER()

namespace OS
{
    // as in the AVR/MSP430 ports, but a shift instead of the run-time table,
    // so that the compiler can evaluate it
    INLINE TProcessMap get_prio_tag(const uint_fast8_t pr) { return static_cast<TProcessMap>(1) << pr; }

#if scmRTOS_PRIORITY_ORDER == 0
    INLINE uint_fast8_t highest_priority(TProcessMap pm)
    {
        uint_fast8_t pr = 0;

        while( !(pm & 0x0001) )
        {
            pr++;
            pm >>= 1;
        }
        return pr;
    }
#else
    INLINE uint_fast8_t highest_priority(TProcessMap pm)
    {
        uint_fast8_t pr = scmRTOS_PROCESS_COUNT;

    #ifdef TARGET16_PORT_SHIFT_FIXED
        while( !(pm & (static_cast<TProcessMap>(1) << scmRTOS_PROCESS_COUNT) ) )
    #else
        while( !(pm & (1 << scmRTOS_PROCESS_COUNT) ) )          // verbatim from the ports
    #endif
        {
            pr--;
            pm <<= 1;
        }
        return pr;
    }
#endif
    INLINE void enable_context_switch()  { }
    INLINE void disable_context_switch() { }
}

#include <os_kernel.h>

namespace OS
{
    class TISRW { public: TISRW() { } ~TISRW() { } };
    #define TISRW_SS TISRW
}

#endif // scmRTOS_TARGET16_H
