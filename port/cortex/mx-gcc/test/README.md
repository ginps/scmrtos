# Cortex-M port test in an ARMv7-M model

Checks the GCC Cortex-M port (`port/cortex/mx-gcc`) on an instruction-level
model, in both interrupt masking schemes: all interrupts (PRIMASK, default)
and OS interrupts only (BASEPRI, `scmRTOS_OS_DI_LEVEL`).

## Files

| File | Purpose |
| --- | --- |
| `cm4sim.c` | Model: Thumb-2 integer instructions, FPv4-SP subset, NVIC with 4 priority bits and grouping, PRIMASK, BASEPRI/BASEPRI_MAX, PendSV, SysTick, MSP/PSP, basic and FP exception frames, random asynchronous interrupts |
| `test.cpp` | Stress test: 6 processes, 2 OS interrupts, 2 interrupts above the OS level |
| `startup.cpp`, `cm4sim.ld` | Vector table, startup code, memory map |
| `scmRTOS_CONFIG.h`, `scmRTOS_TARGET_CFG.h` | Test configuration (`TEST_DI_LEVEL` selects the scheme) |
| `build.sh` | Builds an image with clang for Cortex-M4 |
| `run.sh` | Test matrix |
| `mutate.py` | Mutation testing of the interrupt masking code |

## What is checked

Interrupts: A (priority 6) and B (9) use OS services; H (1) and M (3) do not.

- Services under random interrupts: event flag, channel order, mutex
  exclusion, message exchange, sleep, nested critical sections, process
  integer and FP registers across context switches.
- BASEPRI scheme, H above the level: H latency is constant (it is never
  delayed by the OS); H preempts OS critical sections, PendSV and OS ISRs;
  M preempts OS critical sections; H preempts M (software pend inside M).
- A critical section inside a higher user mask level does not lower it.
- Processes start with interrupts unmasked even if BASEPRI was raised
  before `OS::run()`.
- Priority check (`scmRTOS_CHECK_ISR_PRIORITY`): silent for correct ISRs
  (also at the level itself and with priority grouping), fires for TISRW
  above the level and for a level that falls into group priority 0.
- PRIMASK scheme: the same program; H latency depends on the OS.

## Running

```sh
./run.sh quick     # 23 configurations, 1 run each
./run.sh           # -O0/-Os/-O2 x levels 0,2,5,6 x grouping x soft/FPU
                   # x 3 seeds x 3 interrupt rates: 69 configurations, 621 runs
python3 mutate.py  # 21 mutants
```

Needs clang with the ARM target and ld.lld. The model is a host C program.

## Limitations of the model

- One instruction per cycle; MSR to PRIMASK/BASEPRI takes effect for the
  next instruction, so a missing ISB is not observable.
- FP context is stacked eagerly; lazy stacking is not modelled.
- No caches, bus timing, late arrival or tail-chaining.
