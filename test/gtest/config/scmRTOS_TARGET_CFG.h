//******************************************************************************
//*
//*     scmRTOS GoogleTest suite: target configuration (Linux test port)
//*
//*     Manual tick mode: the system timer ticks only when the harness or a
//*     test calls OS::sim_tick(), so every scenario is deterministic.
//*
//******************************************************************************
#ifndef scmRTOS_TARGET_CFG_H
#define scmRTOS_TARGET_CFG_H

#undef  scmRTOS_LINUX_TICK_MODE
#define scmRTOS_LINUX_TICK_MODE 0

#endif // scmRTOS_TARGET_CFG_H
