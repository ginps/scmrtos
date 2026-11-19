// scmRTOS configuration for the Cortex-M port test (cm4sim model)
#ifndef scmRTOS_CONFIG_H
#define scmRTOS_CONFIG_H

#ifndef __IAR_SYSTEMS_ASM__
#include <stdint.h>
typedef uint16_t     timeout_t;
typedef uint_fast32_t tick_count_t;
#endif

#define scmRTOS_PROCESS_COUNT                   6
#define scmRTOS_SYSTIMER_NEST_INTS_ENABLE       1
#define scmRTOS_SYSTEM_TICKS_ENABLE             1
#define scmRTOS_SYSTIMER_HOOK_ENABLE            1
#define scmRTOS_IDLE_HOOK_ENABLE                1   // clang removes the empty idle loop otherwise
#define scmRTOS_IDLE_PROCESS_STACK_SIZE         512     // bytes
#define scmRTOS_CONTEXT_SWITCH_USER_HOOK_ENABLE 1
#define scmRTOS_DEBUG_ENABLE                    0
#define scmRTOS_PROCESS_RESTART_ENABLE          0

#endif
