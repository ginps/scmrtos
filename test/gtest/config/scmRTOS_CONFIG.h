//******************************************************************************
//*
//*     scmRTOS GoogleTest suite: project configuration
//*
//*     The build matrix (CMakeLists.txt) sets scmRTOS_CONTEXT_SWITCH_SCHEME,
//*     scmRTOS_PRIORITY_ORDER and scmRTOS_PROCESS_RESTART_ENABLE.
//*
//******************************************************************************
#ifndef scmRTOS_CONFIG_H
#define scmRTOS_CONFIG_H

#ifndef __ASSEMBLER__
#include <stdint.h>
typedef uint16_t  timeout_t;
typedef uint32_t  tick_count_t;
#endif

#define scmRTOS_PROCESS_COUNT                   5
#define scmRTOS_SYSTIMER_NEST_INTS_ENABLE       0
#define scmRTOS_SYSTEM_TICKS_ENABLE             1
#define scmRTOS_SYSTIMER_HOOK_ENABLE            1
#define scmRTOS_IDLE_HOOK_ENABLE                1
#define scmRTOS_CONTEXT_SWITCH_USER_HOOK_ENABLE 1
#define scmRTOS_IDLE_PROCESS_STACK_SIZE         (128*1024)
#define scmRTOS_DEBUG_ENABLE                    1
#define scmRTOS_SUSPENDED_PROCESS_ENABLE        1

#ifndef scmRTOS_PROCESS_RESTART_ENABLE
#define scmRTOS_PROCESS_RESTART_ENABLE          0
#endif

#endif // scmRTOS_CONFIG_H
