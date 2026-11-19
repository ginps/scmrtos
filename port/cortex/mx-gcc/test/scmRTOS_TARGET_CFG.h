// Target configuration for the Cortex-M port test (cm4sim model)
#ifndef scmRTOS_TARGET_CFG_H
#define scmRTOS_TARGET_CFG_H

#define SYSTICKFREQ     2000000UL       // model: 1 cycle per instruction
#define SYSTICKINTRATE  1000UL          // tick every 2000 instructions

#define CORE_PRIORITY_BITS  4           // as STM32F4

// TEST_DI_LEVEL: 0 - old scheme (PRIMASK), otherwise scmRTOS_OS_DI_LEVEL
#if defined TEST_DI_LEVEL && TEST_DI_LEVEL != 0
#define scmRTOS_OS_DI_LEVEL TEST_DI_LEVEL
#endif

#ifdef TEST_CHECK_ISR_PRIORITY
#define scmRTOS_CHECK_ISR_PRIORITY 1
extern "C" [[noreturn]] void test_isr_priority_error();
#define scmRTOS_ISR_PRIORITY_ERROR() test_isr_priority_error()
#endif

#endif
