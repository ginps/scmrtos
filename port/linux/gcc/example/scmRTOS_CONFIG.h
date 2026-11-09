//******************************************************************************
//*
//*     scmRTOS project configuration: Linux test port example
//*
//******************************************************************************
#ifndef scmRTOS_CONFIG_H
#define scmRTOS_CONFIG_H

#ifndef __ASSEMBLER__
#include <stdint.h>
typedef uint16_t  timeout_t;
typedef uint32_t  tick_count_t;
#endif

#ifndef scmRTOS_PROCESS_COUNT
#define scmRTOS_PROCESS_COUNT                   3
#endif

#define scmRTOS_SYSTIMER_NEST_INTS_ENABLE       0
#define scmRTOS_SYSTEM_TICKS_ENABLE             1
#define scmRTOS_SYSTIMER_HOOK_ENABLE            0
#ifndef scmRTOS_IDLE_HOOK_ENABLE
#define scmRTOS_IDLE_HOOK_ENABLE                0
#endif
#define scmRTOS_CONTEXT_SWITCH_USER_HOOK_ENABLE 0

// Process stacks hold the frames of signal handlers ("interrupts") too:
// keep them at scmRTOS_LINUX_MIN_STACK_SIZE (16 KiB) or more.
#define scmRTOS_IDLE_PROCESS_STACK_SIZE         (64*1024)

#ifndef scmRTOS_DEBUG_ENABLE
#define scmRTOS_DEBUG_ENABLE                    1
#endif
#ifndef scmRTOS_SUSPENDED_PROCESS_ENABLE
#define scmRTOS_SUSPENDED_PROCESS_ENABLE        1
#endif

#endif // scmRTOS_CONFIG_H
