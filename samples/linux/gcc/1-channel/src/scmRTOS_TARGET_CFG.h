//******************************************************************************
//*
//*     scmRTOS target configuration: Linux test port
//*
//*     Every option may also be given on the compiler command line (-D...).
//*
//******************************************************************************
#ifndef scmRTOS_TARGET_CFG_H
#define scmRTOS_TARGET_CFG_H

// 0 - direct switch in the scheduler, 1 - switch by software interrupt (SIGUSR1),
//     the code path used by the Cortex-M and Zynq-7000 ports
#ifndef scmRTOS_CONTEXT_SWITCH_SCHEME
#define scmRTOS_CONTEXT_SWITCH_SCHEME   0
#endif

// 0 - ascending priorities (pr0 is the highest at bit 0), 1 - descending
#ifndef scmRTOS_PRIORITY_ORDER
#define scmRTOS_PRIORITY_ORDER          0
#endif

// 0 - manual ticks with OS::sim_tick() (deterministic tests), 1 - real time, SIGALRM
#ifndef scmRTOS_LINUX_TICK_MODE
#define scmRTOS_LINUX_TICK_MODE         1
#endif

// system timer period in real-time mode, microseconds
#ifndef scmRTOS_LINUX_TICK_PERIOD_US
#define scmRTOS_LINUX_TICK_PERIOD_US    1000
#endif

#endif // scmRTOS_TARGET_CFG_H
