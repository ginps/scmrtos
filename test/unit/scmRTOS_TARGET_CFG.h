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

#endif // scmRTOS_TARGET_CFG_H
