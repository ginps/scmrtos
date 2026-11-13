//******************************************************************************
//*
//*     scmRTOS unit tests: target configuration (Linux test port)
//*
//*     The context switch scheme and the priority order are set by the build
//*     (run.sh), the tick mode is always manual: tests are deterministic.
//*
//******************************************************************************
#ifndef scmRTOS_TARGET_CFG_H
#define scmRTOS_TARGET_CFG_H

#undef  scmRTOS_LINUX_TICK_MODE
#define scmRTOS_LINUX_TICK_MODE 0

// The framework can inject interrupts at every critical section exit (ut::on_critsect_exit)
#define scmRTOS_LINUX_CRITSECT_EXIT_HOOK ut_critsect_exit_hook

#endif // scmRTOS_TARGET_CFG_H
