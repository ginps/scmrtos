scmRTOS Linux port (test port)
==============================

([Русская версия](README_ru.md))

The port runs the scmRTOS kernel as an ordinary Linux program. It is meant for
debugging and testing the kernel, services, extensions and application logic
without target hardware: under a debugger, with sanitizers, in CI.

The port works with v5.2.0 and with the develop branch.

How it works
------------

The whole RTOS runs in one Linux thread.

| scmRTOS notion | In this port |
| --- | --- |
| Process context | `ucontext_t`, switched with `swapcontext()` |
| Process "stack pointer" | address of its `ucontext_t` |
| Interrupt | POSIX signal |
| System timer | `SIGALRM` (from `setitimer()` or `OS::sim_tick()`) |
| Context switch software interrupt (scheme 1, like PendSV) | `SIGUSR1` |
| User interrupts 0..7 | `SIGRTMIN+0` … `SIGRTMIN+7` |
| Critical section | `sigprocmask()` blocks all port signals |

Interrupt handlers do not nest: all port signals are blocked inside a handler.
A pended context switch (scheme 1) is taken after the handler returns, like
PendSV on Cortex-M.

Configuration
-------------

Set in the project's `scmRTOS_TARGET_CFG.h` or with `-D`.

| Macro | Values | Default |
| --- | --- | --- |
| `scmRTOS_CONTEXT_SWITCH_SCHEME` | 0 - direct switch in the scheduler; 1 - via the software interrupt, the code path of the Cortex-M and Zynq-7000 ports | 0 |
| `scmRTOS_PRIORITY_ORDER` | 0 - ascending, 1 - descending | 0 |
| `scmRTOS_LINUX_TICK_MODE` | 0 - manual ticks with `OS::sim_tick()`, deterministic; 1 - real time, `SIGALRM` | 0 |
| `scmRTOS_LINUX_TICK_PERIOD_US` | system timer period in real-time mode, us | 1000 |
| `scmRTOS_LINUX_PRIO_MASK_TABLE` | 1 - `get_prio_tag()` reads a dynamically initialized table, like the AVR/MSP430/STM8 ports | 0 |
| `scmRTOS_LINUX_MIN_STACK_SIZE` | minimal process stack, bytes; checked when `scmRTOS_DEBUG_ENABLE == 1` | 16384 |

Port API
--------

```cpp
OS::sim_tick();                              // one system timer tick, here and now
OS::host::set_irq_handler(n, handler);       // handler of user interrupt n (0..7)
OS::host::raise_irq(n);                      // request interrupt n
OS::host::irq_signal(n);                     // signal number of interrupt n, e.g. for timer_create()
```

An interrupt requested inside a critical section is taken when the section
ends, as on a real MCU. Handlers are written as in other ports:

```cpp
static void uart_rx_irq()
{
    OS::TISRW isr;               // must come first
    RxFlag.signal_isr();
}
```

Example
-------

`example/` - three processes in real time: a "LED" blinker, a producer driven
by a POSIX timer interrupt and a consumer behind a channel.

```sh
cd example
make run                 # scheme 0
make clean run SCHEME=1 ORDER=1
```

Port self-tests
---------------

```sh
test/run.sh              # kernel from this tree
test/run.sh /path/to/scmrtos
```

`test/run.sh` builds and runs two tests for all four combinations of scheme
and priority order:

- `test_manual.cpp` - manual ticks: the exact switch order at start-up, on
  blocking, on a wake-up from an interrupt with preemption, on `force_wake_up()`
  from an interrupt, on timer preemption and on a channel hand-over; timer lock,
  an interrupt pended inside a critical section, stack slack;
- `test_realtime.cpp` - real time, five processes: an interrupt every 0.5 ms,
  `sleep()` accuracy, channel sequence integrity under preemption, mutex
  contention.

Limitations
-----------

- **glibc only.** `makecontext()` must build the frame from the stack top, as
  glibc does. The port is tested on x86-64.
- **Stack overflow is not detected.** With `scmRTOS_DEBUG_ENABLE == 1` the port
  checks the stack size at start-up and `stack_slack()` shows the margin; without
  it the port does not know the stack bottom.
- **Process stacks of 16 KiB or more.** Signal handlers run on the stack of the
  interrupted process; there is no separate interrupt stack.
- **libc is not reentrant.** In real-time mode a process can be preempted inside
  `printf()` or `malloc()`. Call them under a mutex (`OS::TMutexLocker`) or in a
  critical section.
- **Ticks can be lost.** Standard signals do not queue: when Linux deschedules
  the program for longer than a timer period, several `SIGALRM`s merge into one.
  OS time stays consistent but falls behind wall-clock time.
- **`SIGALRM` is taken.** Do not use `alarm()`, libc `sleep()`,
  `setitimer(ITIMER_REAL)`, `SIGUSR1` or `SIGRTMIN..SIGRTMIN+7` in the application.
- **One thread.** Do not call OS functions from other Linux threads.
- **AddressSanitizer** needs stack-switch annotations
  (`__sanitizer_start_switch_fiber`), which the port does not have yet.
