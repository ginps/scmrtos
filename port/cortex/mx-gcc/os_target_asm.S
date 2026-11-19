//******************************************************************************
//*
//*     FULLNAME:  Single-Chip Microcontroller Real-Time Operating System
//*
//*     NICKNAME:  scmRTOS
//*
//*     PROCESSOR: ARM Cortex-M0(+), Cortex-M1, Cortex-M3, Cortex-M4(F), Cortex-M7(F)
//*
//*     TOOLKIT:   ARM GCC
//*
//*     PURPOSE:   Target Dependent Low-Level Stuff
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
//*     Cortex-M3/M4(F)/M7(F) GCC port by Anton B. Gusev aka AHTOXA, Copyright (c) 2012-2021
//*     Cortex-M0 port by Sergey A. Borshch, Copyright (c) 2011-2021
//*     Cortex-M7 support based on Cortex-M4(F) port

    .syntax unified
    .text
    .align 2

/*
 * PendSV is used to perform a context switch. This is a recommended method for Cortex-M.
 * This is because the Cortex-M automatically saves half of the processor context
 * on any exception, and restores same on return from exception.  So only saving of R4-R11
 * and fixing up the stack pointers is required.  Using the PendSV exception this way means
 * that context saving and restoring is identical whether it is initiated from a thread
 * or occurs due to an interrupt or exception.
 *
 * Since PendSV interrupt has the lowest priority in the system (set by os_start() below),
 * we can be sure that it will run only when no other exception or interrupt is active, and
 * therefore safe to assume that context being switched out was using the process stack (PSP).
 *
 * This file contains PendSV handler for ARMv6-M (Cortex-M0(+)/M1) only.
 * PendSV handler for ARMv7-M (Cortex-M3/M4(F)/M7(F)) is in os_target.cpp:
 * the way it masks interrupts depends on scmRTOS_OS_DI_LEVEL, which is
 * defined in scmRTOS_TARGET_CFG.h and is not visible here.
 */

#if (defined __ARM_ARCH_6M__)

    .extern os_context_switch_hook
    .global PendSV_Handler

.thumb_func
PendSV_Handler:

    // Cortex-M0(+)/Cortex-M1
    CPSID   I                   // Prevent interruption during context switch
    MRS     R0, PSP             // Load process stack pointer to R0
    SUBS    R0, #32             // Adjust R0 to point to top of saved context in stack
    MOV     R1, R0              // Preserve R0 (needed for os_context_switch_hook() call)
    STMIA   R1!, {R4-R7}        // Save low portion of remaining registers (r4-7) on process stack
    MOV     R4, R8              // Move high portion of remaining registers (r8-11) to low registers
    MOV     R5, R9
    MOV     R6, R10
    MOV     R7, R11
    STMIA   R1!, {R4-R7}        // Save high portion of remaining registers (r8-11) on process stack

// At this point, entire context of process has been saved
    PUSH    {LR}                // we must save LR (exc_return value) until exception return
    LDR     R1, =os_context_switch_hook   // call os_context_switch_hook();
    BLX     R1

// R0 is new process SP;
    ADDS    R0, #16            // Adjust R0 to point to high registers (r8-11)
    LDMIA   R0!, {R4-R7}       // Restore r8-11 from new process stack
    MOV     R8, R4             // Move restored values to high registers (r8-11)
    MOV     R9, R5
    MOV     R10, R6
    MOV     R11, R7
    MSR     PSP, R0            // R0 at this point is new process SP
    SUBS    R0, #32            // Adjust R0 to point to low registers
    LDMIA   R0!, {R4-R7}       // Restore r4-7
    CPSIE   I
    POP     {PC}               // Return to saved exc_return. Exception return will restore remaining context


#endif

.end

