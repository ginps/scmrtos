scmRTOS unit tests
==================

([Русская версия](README_ru.md))

201 tests of the scmRTOS kernel, services and extensions. They run on Linux
through the test port `port/linux/gcc` in manual tick mode, so every result is
deterministic down to the tick and the order of context switches.

Running
-------

```sh
test/unit/run.sh                 # all 12 configurations (~40 s)
test/unit/run.sh mutex channel   # only tests whose name contains "mutex" or "channel"
test/unit/run.sh -v              # show the output of passed tests too
test/unit/coverage.sh            # line and branch coverage over all configurations
test/unit/coverage.sh --uncovered --branches   # plus the list of what is not covered
```

One binary:

```sh
test/unit/build.sh /tmp/ut -DscmRTOS_CONTEXT_SWITCH_SCHEME=1
/tmp/ut -l                       # list the tests
/tmp/ut kernel.sleep             # substring filter
```

Environment: `SRC` - the scmRTOS tree to test (default: the one the tests are
in), `PORT` - the Linux port directory, `CXX` - compiler, `OPT` - optimization
(default `-O1`).

The suite adapts to the tree: without the develop extensions in `SRC`
(semaphore, event-flags, launch, safe-run, watchdog) their tests and the
develop-only API (`try_push`, `terminate(func)`, `process_profiler`) are left
out, so the same suite also checks v5.2.0: `SRC=/path/to/v5.2.0 test/unit/run.sh`.

Configurations
--------------

| Program | Configurations | Tests |
| --- | --- | --- |
| main | context switch scheme 0/1 x priority order 0/1 x process restart off/on | 192 / 195 |
| `launch` (C++17, 3 processes) | scheme 0/1 x order 0/1 | 6 |

Suites
------

| Suite | Tests | Covers |
| --- | --- | --- |
| `kernel` | 37 (+3 restart) | start-up, `os_running`, start order, `pssSuspended`, preemption by timeout and by interrupt, `sleep` 1...65535 ticks, `sleep(0)`, `wake_up`/`force_wake_up`, `is_sleeping`/`is_suspended`, timer lock, timer hook, critical sections and nested `TISRW`, deferred switch in an ISR, switch count, `stack_slack`/`name`/`waiting_for`, `terminate()` |
| `ring_buffer` | 29 | both ends, full, empty, size 1, wrap-around, `write`/`read`, `operator[]`, `flush`, struct items, `uint16_t` index; comparison with `std::deque` over random operation sequences (~230k operations) |
| `event_flag` | 17 | latching, waking all waiters, timeouts, `clear`, `signal_isr`, wait aborted by `wake_up` |
| `mutex` | 14 | hand-over to the highest waiter, unlock by non-owner, `try_lock`, `try_lock(timeout)`, `unlock_isr`, `TMutexLocker`, mutual exclusion under preemption |
| `channel` | 26 | producer and consumer blocking, timeouts, `read`/`write`, `*_isr`, `flush`, `try_*`, competing consumers, stress |
| `message` | 8 | latching, broadcast, timeout, `reset`, `send_isr`, struct payload |
| `recursive_mutex` | 13 | nesting, `force_unlock`, `try_lock`, ISR variants |
| `semaphore` | 10 | counting, maximum, wake-up order, stolen token with the deadline kept, resource pool |
| `event_flags` | 15 | any/all/no-clear, absolute deadline, competing for bits, `try_wait`, `set_isr` |
| `safe_run` | 2 + 8 `static_assert` | complete, incomplete, duplicated, IDLE sets; start through `safe_run()` |
| `round_robin` | 3 | rotation by time slices, finished processes skipped, externally woken process put off |
| `profiler` | 8 | `TProfiler`, `process_profiler<float/uint32_t>`, driven by the context switch hook |
| `watchdog` | 10 | timely `alive`, reboot exactly at the timeout, `asleep`, double `asleep`, zero timeout |
| `launch` | 6 | priorities by position, captures, `pssSuspended`, alignment, names and stack sizes |

Known defects
-------------

A test written as `UT_TEST_DEFECT(..., "id")` checks the correct behaviour
where a known defect breaks it. Its failure is shown as `[ KNWN ]` and does not
count as an error; if it passes it is shown as `[ FIXD ]` (defect not
reproduced). The ids are those of the v5.2.0 (1-15) and develop (N1-N6) analysis
reports.

| Defect | Tests | v5.2.0 | v5.2.0 + patches 1-6 | develop |
| --- | --- | --- | --- | --- |
| 2 - channel timeout lost | 1 | reproduced | fixed | fixed |
| 4 - `Size` above index range | 1 | reproduced | fixed (compile error) | reproduced |
| 5 - `operator[]` overflow | 2 | reproduced | fixed | reproduced |
| 6 - `try_lock` timeout restarts | 2 | reproduced | fixed | reproduced |
| N1 - `try_pop_back` inverted | 2 | - | - | reproduced |
| N3 - watchdog `nullptr` | 2 | - | - | reproduced |
| N4 - profiler division by zero | 2 (v5.2.0: 1) | reproduced (`TProfiler`) | reproduced | reproduced |

N4 affects `TProfiler` too, which exists since v5.2.0, not only `process_profiler`.

Coverage
--------

`coverage.sh` builds every configuration with `--coverage -O0 -fno-exceptions
-DINLINE=inline`: `INLINE=inline` turns off forced inlining, otherwise gcov counts
the branches of every inlined copy separately; the kernel does not use
exceptions. The data of all configurations is merged: a line is covered if any
configuration ran it.

| Files | Lines | Branches |
| --- | --- | --- |
| kernel (`core/`) | 511/511 - 100% | 129/130 - 99.2% |
| extensions (`ext/`) | 360/368 - 97.8% | 164/174 - 94.3% |
| total, with the port | 976/993 - 98.3% | 322/346 - 93.1% |

Not covered in the kernel and the extensions:

- the repeat of the wait loop in `TKernel::sched()` (scheme 1): the port
  switches at the first interrupt enable;
- the endless loops with which `launch` and the watchdog stop the system on an error;
- defensive conditions unreachable through the public API (`safe_run`: priority
  out of range; recursive mutex: owner with zero count; watchdog: corrupted timeout);
- the watchdog branches for an unregistered process: the test does not get
  there because of defect N3.

Writing a test
--------------

A `UT_TEST` body is the set-up. It runs before `OS::run()` and assigns bodies to
the processes P0...P4 (P0 is the highest), interrupt handlers and an end check.
The test ends when every assigned body has returned. The timer ticks only when
all processes are blocked (from the idle process), or explicitly with `ut::tick()`.

```cpp
UT_TEST(mutex, contended_lock_blocks_until_unlock)
{
    static OS::TMutex M;
    ut::proc(0, []{ OS::sleep(1); ut::trace("P0-lock"); M.lock(); ut::trace("P0-got@%u", ut::ticks()); M.unlock(); });
    ut::proc(1, []{ M.lock(); OS::sleep(5); ut::trace("P1-unlock"); M.unlock(); ut::trace("P1"); });
    ut::at_end([]{ CHECK_TRACE("P0-lock P1-unlock P0-got@5 P1"); });
}
```

- The lambdas capture nothing: shared state is `static` inside the test.
- `ut::proc(i, body, ut::DAEMON)` - a process the test does not wait for;
  `ut::SUSPENDED` (P4 only) - the process starts suspended.
- `ut::irq(n, handler)` and `ut::raise(n)` - user interrupts 0...7; a handler
  starts with `OS::TISRW isr;`.
- `ut::on_systimer()`, `ut::on_context_switch()` - kernel hooks; `ut::max_ticks()` -
  time limit (default 2000 ticks).
- A test without processes is a plain unit test; the kernel is not started.
- Every test runs in its own Linux process (`fork`): a hang (5 s timeout) or a
  crash of one test does not affect the others.
- Write `OS::sleep()`, not `sleep()`, inside tests: the latter clashes with libc `sleep()`.
