//******************************************************************************
//*
//*     FULLNAME:  Single-Chip Microcontroller Real-Time Operating System
//*
//*     NICKNAME:  scmRTOS
//*
//*     PROCESSOR: ARM Cortex-M0(+), Cortex-M1, Cortex-M3, Cortex-M4(F)
//*
//*     TOOLKIT:   ARM IAR
//*
//*     PURPOSE:   Target Dependent Stuff Header. Declarations And Definitions
//*
//*     Version: v5.2.0
//*
//*
//*     Copyright (c) 2003-2021, scmRTOS Team
//*
//*     Permission is hereby granted, free of charge, to any person
//*     obtaining  a copy of this software and associated documentation
//*     files (the "Software"), to deal in the Software without restriction,
//*     including without limitation the rights to use, copy, modify, merge,
//*     publish, distribute, sublicense, and/or sell copies of the Software,
//*     and to permit persons to whom the Software is furnished to do so,
//*     subject to the following conditions:
//*
//*     The above copyright notice and this permission notice shall be included
//*     in all copies or substantial portions of the Software.
//*
//*     THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//*     EXPRESS  OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//*     MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//*     IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//*     CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//*     TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
//*     THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//*
//*     =================================================================
//*     Project sources: https://github.com/scmrtos/scmrtos
//*     Documentation:   https://github.com/scmrtos/scmrtos/wiki/Documentation
//*     Wiki:            https://github.com/scmrtos/scmrtos/wiki
//*     Sample projects: https://github.com/scmrtos/scmrtos-sample-projects
//*     =================================================================
//*
//******************************************************************************
//*     Cortex-M3/M4(F) GCC port by Anton B. Gusev aka AHTOXA, Copyright (c) 2012-2021
//*     Cortex-M0 port by Sergey A. Borshch, Copyright (c) 2011-2021
//*     Cortex-M0/M3/M4(F) IAR port by Yury A. Yakimov aka haker_fox, Copyright (c) 2020-2021

#ifndef scmRTOS_CORTEXMX_H
#define scmRTOS_CORTEXMX_H

//------------------------------------------------------------------------------
//
//    Compiler and Target checks
//
//
#ifndef __IAR_SYSTEMS_ICC__
#error "This file should only be compiled with IAR C/EC++ Compiler"
#endif

#ifndef __ICCARM__
#error "This file must be compiled for ARM processor only"
#endif // __GNUC__

#if (__CORE__ != __ARM6M__ && __CORE__ != __ARM7M__ && __CORE__ != __ARM7EM__)
#error "This file must be compiled for ARMv6-M (Cortex-M0(+)), ARMv7-M (Cortex-M3) and ARMv7E-M (Cortex-M4(F)) processors only."
#endif

#include <intrinsics.h>

//------------------------------------------------------------------------------
//
//    Compiler specific attributes
//
//
#ifndef INLINE
#define INLINE      __attribute__((__always_inline__)) inline
#endif

#ifndef NOINLINE
#define NOINLINE    __attribute__((__noinline__))
#endif

#ifndef NORETURN
#define NORETURN    __attribute__((__noreturn__))
#endif

//------------------------------------------------------------------------------
//
//    Target specific types
//
//
typedef uint32_t stack_item_t;
typedef uint32_t status_reg_t;

//-----------------------------------------------------------------------------
//
//    Configuration macros
//
//
#define OS_PROCESS __attribute__((__noreturn__))
#define OS_INTERRUPT extern "C"

#define DUMMY_INSTR() asm volatile ("nop")
#define INLINE_PROCESS_CTOR INLINE

//-----------------------------------------------------------------------------
// Separate return stack not required
#define SEPARATE_RETURN_STACK   0

//-----------------------------------------------------------------------------
// Software interrupt stack switching not supported in Cortex-M port
// because processor implements hardware stack switching.
// So, system timer ISR wrapper can't be chosen at project level
//
#define scmRTOS_ISRW_TYPE       TISRW

//-----------------------------------------------------------------------------
//
//    scmRTOS Context Switch Scheme
//
//    The macro defines a context switch manner. Value 0 sets direct context
//    switch in the scheduler and in the OS ISRs. This is the primary method.
//    Value 1 sets the second way to switch context - by using of software
//    interrupt. See documentation for details.
//    Cortex-M port supports software interrupt switch method only.
//
#define  scmRTOS_CONTEXT_SWITCH_SCHEME 1

//-----------------------------------------------------------------------------
//
//    scmRTOS Priority Order
//
//    This macro defines the order of the process's priorities. Default,
//    the ascending order is used. Alternatively, the descending priority
//    order can be used. On some platforms the descending order is preferred
//    because of performance.
//
//    Default (corresponding to ascending order) value of macro is 0.
//    Alternative (corresponding to descending order) value of macro is 1.
//
//    On Cortex-M3/M4 the descending order is used for performance reason.
//    Cortex-M0 lacks CLZ instruction, so only ascending order is implemented for it.
//
#if (__CORE__ == __ARM6M__)
#define  scmRTOS_PRIORITY_ORDER             0
#else
#define  scmRTOS_PRIORITY_ORDER             1
#endif

//-----------------------------------------------------------------------------
//
//     Include project-level configurations
//    !!! The order of includes is important !!!
//
#include "scmRTOS_CONFIG.h"
#include "scmRTOS_TARGET_CFG.h"
#include <scmRTOS_defs.h>

//-----------------------------------------------------------------------------
//
//    Target-specific configuration macros
//
#ifdef scmRTOS_USER_DEFINED_STACK_PATTERN
#define scmRTOS_STACK_PATTERN scmRTOS_USER_DEFINED_STACK_PATTERN
#else
#define scmRTOS_STACK_PATTERN 0xABBA
#endif


//-----------------------------------------------------------------------------
//
//     Interrupt masking by the OS
//
//     By default the OS masks all interrupts (PRIMASK) in its critical
//     sections and in the context switch ISR.
//
//     On ARMv7-M (Cortex-M3/M4) the OS can mask only the interrupts
//     that use OS services, by raising BASEPRI (as the MB91 port does with
//     ILM). To enable it, define in scmRTOS_TARGET_CFG.h:
//
//         #define CORE_PRIORITY_BITS    4    // __NVIC_PRIO_BITS, 4 for STM32F4
//         #define scmRTOS_OS_DI_LEVEL   5    // first priority level masked by the OS
//
//     Levels are given as for NVIC_SetPriority() (preemption priority,
//     not shifted to the register's upper bits), 0 is the highest:
//
//       0 .. scmRTOS_OS_DI_LEVEL-1   never masked by the OS. Such ISRs must
//                                    not use OS services, TISRW, TCritSect
//                                    or OS objects; their latency does not
//                                    depend on the OS.
//       scmRTOS_OS_DI_LEVEL .. max   masked in OS critical sections; such
//                                    ISRs may use OS services (with TISRW).
//
//     The two lowest levels are used by the OS itself (system timer and
//     PendSV), so 1 <= scmRTOS_OS_DI_LEVEL <= 2^CORE_PRIORITY_BITS - 2.
//     Priority grouping: BASEPRI compares only the group (preemption)
//     priority, so with subpriority bits the subpriority of the level is
//     ignored. NVIC_PRIORITYGROUP_4 (no subpriority) is recommended.
//
//     Cortex-M7 r0p1 (e.g. STM32F74x/F75x) has erratum 837070: a write to
//     BASEPRI may take effect one instruction late. Define
//     scmRTOS_CM7_R0P1_ERRATUM_837070 to 1 for the workaround.
//
//     enable_interrupts()/disable_interrupts() always control all
//     interrupts (PRIMASK); the OS itself does not use them in this mode.
//
#if (defined scmRTOS_OS_DI_LEVEL)

#if (__CORE__ == __ARM6M__)
#error "scmRTOS_OS_DI_LEVEL: ARMv6-M (Cortex-M0(+)/M1) has no BASEPRI register."
#endif

#if (!defined CORE_PRIORITY_BITS)
#error "scmRTOS_OS_DI_LEVEL requires CORE_PRIORITY_BITS (number of implemented priority bits, 4 for STM32F4)."
#endif

#if (CORE_PRIORITY_BITS < 2) || (CORE_PRIORITY_BITS > 8)
#error "CORE_PRIORITY_BITS must be in range 2..8."
#endif

#if (scmRTOS_OS_DI_LEVEL < 1) || (scmRTOS_OS_DI_LEVEL > ((1 << (CORE_PRIORITY_BITS)) - 2))
#error "scmRTOS_OS_DI_LEVEL must be in range 1 .. 2^CORE_PRIORITY_BITS - 2."
#endif

#define scmRTOS_CRITSECT_BASEPRI  1

// BASEPRI value: level shifted to the implemented (upper) priority bits
#define scmRTOS_OS_DI_BASEPRI   (((scmRTOS_OS_DI_LEVEL) << (8 - (CORE_PRIORITY_BITS))) & 0xFF)

#else

#define scmRTOS_CRITSECT_BASEPRI  0

#endif // scmRTOS_OS_DI_LEVEL

#ifndef scmRTOS_CM7_R0P1_ERRATUM_837070
#define scmRTOS_CM7_R0P1_ERRATUM_837070 0
#endif

#ifndef scmRTOS_CHECK_ISR_PRIORITY
#define scmRTOS_CHECK_ISR_PRIORITY  scmRTOS_DEBUG_ENABLE
#endif

//-----------------------------------------------------------------------------
//
//     Interrupt and Interrupt Service Routines support
//
//     enable_interrupts()/disable_interrupts() - all interrupts (PRIMASK).
//
//     enable_os_interrupts()/disable_os_interrupts() and
//     get_interrupt_state()/set_interrupt_state() - interrupts masked by
//     the OS: PRIMASK by default, BASEPRI with scmRTOS_OS_DI_LEVEL.
//
#define enable_interrupts() asm volatile ("cpsie i" ::: "memory")
#define disable_interrupts() asm volatile ("cpsid i" ::: "memory")

#if scmRTOS_CRITSECT_BASEPRI == 1

INLINE void set_interrupt_state(status_reg_t status)
{
    asm volatile ("MSR BASEPRI, %0" : : "r"(status) : "memory");
}

INLINE status_reg_t get_interrupt_state()
{
    status_reg_t sr;
    asm volatile ("MRS %0, BASEPRI" : "=r"(sr) : : "memory");
    return sr;
}

// BASEPRI_MAX only raises the mask level, so nesting inside a section
// with a higher mask level (or in a high priority ISR) changes nothing.
INLINE void disable_os_interrupts()
{
#if scmRTOS_CM7_R0P1_ERRATUM_837070 == 1
    // PRIMASK is restored, not just cleared: the caller may have set it
    status_reg_t primask;
    asm volatile (
        "MRS     %0, PRIMASK    \n"
        "CPSID   I              \n"
        "MSR     BASEPRI_MAX, %1\n"
        "ISB                    \n"
        "MSR     PRIMASK, %0    \n"
        : "=&r"(primask) : "r"(scmRTOS_OS_DI_BASEPRI) : "memory"
    );
#else
    asm volatile (
        "MSR     BASEPRI_MAX, %0\n"
        "ISB                    \n"
        : : "r"(scmRTOS_OS_DI_BASEPRI) : "memory"
    );
#endif
}

INLINE void enable_os_interrupts()
{
    asm volatile (
        "MSR     BASEPRI, %0    \n"
        "ISB                    \n"
        : : "r"(0) : "memory"
    );
}

#else  // scmRTOS_CRITSECT_BASEPRI == 0

INLINE void set_interrupt_state(status_reg_t status)
{
    asm volatile (
        "MSR PRIMASK, %0\n"
        : : "r"(status)
        :"memory"
    );
}

INLINE status_reg_t get_interrupt_state()
{
    status_reg_t sr;
    asm volatile ("MRS %0, PRIMASK" : "=r"(sr) : : "memory");
    return sr;
}

INLINE void disable_os_interrupts() { disable_interrupts(); }
INLINE void enable_os_interrupts()  { enable_interrupts();  }

#endif // scmRTOS_CRITSECT_BASEPRI

//-----------------------------------------------------------------------------
//
//     The Critical Section Wrapper
//
//
#if scmRTOS_USER_DEFINED_CRITSECT_ENABLE == 0
class TCritSect
{
public:
    INLINE TCritSect () : StatusReg(get_interrupt_state()) { disable_os_interrupts(); }
    INLINE ~TCritSect() { set_interrupt_state(StatusReg); }

private:
    status_reg_t StatusReg;
};
#endif // scmRTOS_USER_DEFINED_CRITSECT_ENABLE

//   Uncomment macro value below for system_timer() and
//   context_switch_hook() run in critical section.
//
//   This is useful (and necessary) when target processor has hardware
//   enabled nested interrupts.
//   User can define own macros using user-defined TCritSect capabilities.
//
//   Cortex-M have nested interrupts but OS interrupts are masked
//   during context switch ISR. So, critical section is needed
//   for system timer routine and not needed for context switcher.
//
#define SYS_TIMER_CRIT_SECT() TCritSect cs
#define CONTEXT_SWITCH_HOOK_CRIT_SECT()


//-----------------------------------------------------------------------------
//
//     Lock/unlock system timer.
//
//
void LOCK_SYSTEM_TIMER();
void UNLOCK_SYSTEM_TIMER();


//-----------------------------------------------------------------------------
//
//     Priority stuff
//
//
namespace OS
{
INLINE OS::TProcessMap get_prio_tag(const uint_fast8_t pr) { return static_cast<OS::TProcessMap> (1 << pr); }

#if scmRTOS_PRIORITY_ORDER == 0
    INLINE uint_fast8_t highest_priority(TProcessMap pm)
    {
        extern TPriority const PriorityTable[];

        #if scmRTOS_PROCESS_COUNT < 6
            return PriorityTable[pm];
        #else
            uint32_t x = pm;
            x = x & -x;                             // Isolate rightmost 1-bit.

                                                // x = x * 0x450FBAF
            x = (x << 4) | x;                       // x = x*17.
            x = (x << 6) | x;                       // x = x*65.
            x = (x << 16) - x;                      // x = x*65535.

            return PriorityTable[x >> 26];
        #endif  // scmRTOS_PROCESS_COUNT < 6
    }
#else
    INLINE uint_fast8_t highest_priority(TProcessMap pm)
    {
        return 31 - __CLZ(pm);
    }
#endif // scmRTOS_PRIORITY_ORDER
}

namespace OS
{
    INLINE void enable_context_switch()  { enable_os_interrupts(); }
    INLINE void disable_context_switch() { disable_os_interrupts(); }
}

//------------------------------------------------------------------------------
//
//       Context Switch ISR stuff
//
//
namespace OS
{
#if scmRTOS_CONTEXT_SWITCH_SCHEME == 1

// 0xE000ED04 - Interrupt Control State Register
// set PENDSVSET bit
INLINE void raise_context_switch() { *((volatile uint32_t*)0xE000ED04) = 0x10000000; }

#define ENABLE_NESTED_INTERRUPTS()

#if scmRTOS_SYSTIMER_NEST_INTS_ENABLE == 0
#define DISABLE_NESTED_INTERRUPTS() TCritSect cs
#else
#define DISABLE_NESTED_INTERRUPTS()
#endif

#else
#error "Cortex-M3 port supports software interrupt switch method only!"

#endif // scmRTOS_CONTEXT_SWITCH_SCHEME

}

#include <os_kernel.h>

namespace OS
{

//--------------------------------------------------------------------------
//
//      NAME       :   OS ISR support
//
//      PURPOSE    :   Implements common actions on interrupt enter and exit
//                     under the OS
//
//      DESCRIPTION:
//
//
#if (scmRTOS_CRITSECT_BASEPRI == 1) && (scmRTOS_CHECK_ISR_PRIORITY == 1)
//  Called by TISRW. Stops on an ISR whose priority is above
//  scmRTOS_OS_DI_LEVEL: the OS does not mask it, so it must not use
//  OS services. os_start() also stops if, with the current priority
//  grouping, the level falls into group priority 0 (the OS would then
//  mask all interrupts). Enabled by scmRTOS_CHECK_ISR_PRIORITY (default:
//  scmRTOS_DEBUG_ENABLE). Default reaction: endless loop;
//  define scmRTOS_ISR_PRIORITY_ERROR() in scmRTOS_TARGET_CFG.h to change.
void os_check_isr_priority();
#endif

class TISRW
{
public:
    INLINE  TISRW()  { ISR_Enter(); }
    INLINE  ~TISRW() { ISR_Exit();  }

private:
    //-----------------------------------------------------
    INLINE void ISR_Enter()
    {
    #if (scmRTOS_CRITSECT_BASEPRI == 1) && (scmRTOS_CHECK_ISR_PRIORITY == 1)
        os_check_isr_priority();
    #endif
        TCritSect cs;
        Kernel.ISR_NestCount = Kernel.ISR_NestCount + 1;
    }
    //-----------------------------------------------------
    INLINE void ISR_Exit()
    {
        TCritSect cs;
        uint_fast8_t cnt = Kernel.ISR_NestCount - 1;
        Kernel.ISR_NestCount = cnt;
        if(cnt) return;
        Kernel.sched_isr();
    }
    //-----------------------------------------------------
};

// No software interrupt stack switching provided,
// TISRW_SS declared to be the same as TISRW for porting compatibility
#define TISRW_SS    TISRW

/*
 * System timer interrupt handler.
 */
INLINE void system_timer_isr()
{
	OS::TISRW ISR;

#if scmRTOS_SYSTIMER_NEST_INTS_ENABLE == 0
    DISABLE_NESTED_INTERRUPTS();
#endif

#if scmRTOS_SYSTIMER_HOOK_ENABLE == 1
    system_timer_user_hook();
#endif

    Kernel.system_timer();
}

} // namespace OS
//-----------------------------------------------------------------------------

#endif // scmRTOS_CORTEXMX_H
//-----------------------------------------------------------------------------

