scmRTOS unit tests
==================

([Русская версия](README_ru.md))

Tests of the scmRTOS kernel, services and extensions: 271 scenario tests and
several further levels (see "Test levels"). They run on Linux through the test
port `port/linux/gcc`. The scenario tests use manual tick mode, so their result
is deterministic down to the tick and the order of context switches.

Running
-------

```sh
test/unit/run.sh                 # all levels, 41 runs (~2 minutes)
FUZZ_SEEDS=0 test/unit/run.sh    # without the seed sweeps
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
/tmp/ut --seeds=1-300 fuzz.injected   # run a test once per seed of the range
/tmp/ut --seed=34 -v fuzz.single      # replay one seed, with output
/tmp/ut -x                       # stop at the first unexpected failure
```

Environment: `SRC` - the scmRTOS tree to test (default: the one the tests are
in), `PORT` - the Linux port directory, `CXX` - compiler, `OPT` - optimization
(default `-O1`), `FUZZ_SEEDS` - seeds per fuzz test (default 300).

The suite adapts to the tree: without the develop extensions in `SRC`
(semaphore, event-flags, launch, safe-run, watchdog) their tests and the
develop-only API (`try_push`, `terminate(func)`, `process_profiler`) are left
out, so the same suite also checks v5.2.0: `SRC=/path/to/v5.2.0 test/unit/run.sh`.

Test levels
-----------

`run.sh` builds and runs all of the following for every pair "context switch
scheme 0/1 x priority order 0/1":

| Level | Program | What it gives | Runs |
| --- | --- | --- | --- |
| Scenario tests | main, process restart off/on | exact switch order and tick numbers; 264 / 271 tests | 8 |
| Preemption point sweep | main, `fuzz.injected_*`, `fuzz.no_injection` | random workload of five processes; interrupts injected at critical section exits; 300 seeds per test | 4 |
| One interrupt at every point | main, `fuzz.single_interrupt_at_every_point` | fixed scenario, 653 points x 3 kinds of interrupt = 1959 runs | 4 |
| Asynchronous stress | main, `fuzz.async_stress`, suite `atomic` | interrupts from a real timer at any instruction | in the same runs |
| `OS::launch()` | `LAUNCH=1`, C++17, 3 processes | 6 tests | 4 |
| Process count | `WIDE=N` for N = 1, 7, 8, 15, 16, 31 | boundaries of the `TProcessMap` type and completely filled maps; 8 tests each | 24 |
| Mask table | `-DscmRTOS_LINUX_PRIO_MASK_TABLE=1`, scheme 0 and order 0 only | `get_prio_tag()` as in the AVR/MSP430/STM8 ports (defect 3) | 1 |

Run separately:

| Level | Command | What it gives |
| --- | --- | --- |
| Sanitizers | `build.sh /tmp/ut -fsanitize=undefined` or `-fsanitize=address` (ASan: `ASAN_OPTIONS=detect_stack_use_after_return=0`) | undefined behaviour and out-of-bounds access; on develop they report only the known N3 and N4 |
| 16-bit targets | `test/target16/run.sh` (needs clang with the msp430 and avr targets) | the process map code is compiled for a real 16-bit target, the compiler folds it to constants, which are compared with the expected ones (defect 1) |
| Mutation testing | `mutate.py`, see below | measures what share of errors planted in the code the tests notice |
| Coverage | `coverage.sh` | lines and branches |

### Fuzzing with injected interrupts

The port calls a hook every time an outermost critical section ends, i.e. at
every point where a real MCU would take a pending interrupt. The hook raises a
system tick or a user interrupt at random. The sequence depends only on the
seed, so any failure is replayed with `--seed=N`. Five processes work on two
mutexes, a channel, a flag, a message and a semaphore and check invariants all
the way: mutual exclusion, order and conservation of channel items, the
semaphore limit, minimal duration of `sleep()` and of timeouts, and that every
process finishes.

`fuzz.async_stress` and the suite `atomic` use a real timer: an interrupt
arrives at an arbitrary instruction. They cannot be replayed by seed, but they
notice a missing critical section. On correct code they always pass; they catch
an error with some probability, not for certain.

A seed sweep stops after five failures: a hung seed costs 5 seconds.

### Interrupt state checks

- The framework checks in the context switch hook that the scheduler was
  entered with the interrupts disabled.
- The tests `*_copied_only_inside_critical_sections` use an item type that
  notes every copy made outside a critical section.
- The port aborts when a process is restarted outside a critical section.

Suites
------

| Suite | Tests | Covers |
| --- | --- | --- |
| `kernel` | 45 (+7 restart) | start-up, `os_running`, start order, `pssSuspended`, preemption by timeout and by interrupt, `sleep` 1...65535 ticks, `sleep(0)`, `wake_up`/`force_wake_up`, `is_sleeping`/`is_suspended`, timer lock, timer hook, critical sections and nested `TISRW`, deferred switch in an ISR, switch count, an interrupt taken while a switch is already requested, `stack_slack`/`name`/`waiting_for`, `terminate()`, including no trace of an earlier wait |
| `ring_buffer` | 29 | both ends, full, empty, size 1, wrap-around, `write`/`read`, `operator[]`, `flush`, struct items, `uint16_t` index; comparison with `std::deque` over random operation sequences (~230k operations) |
| `event_flag` | 18 | latching, waking all waiters, timeouts, `clear`, `signal_isr`, wait aborted by `wake_up` |
| `mutex` | 18 | hand-over to the highest waiter, unlock by non-owner, `try_lock`, `try_lock(timeout)`, `unlock_isr`, `TMutexLocker`, mutual exclusion under preemption |
| `channel` | 36 | producer and consumer blocking, timeouts, `read`/`write`, `*_isr`, `flush`, `try_*`, competing consumers, stress |
| `message` | 10 | latching, broadcast, timeout, `reset`, `send_isr`, struct payload |
| `recursive_mutex` | 18 | nesting, `force_unlock`, `try_lock`, ISR variants |
| `semaphore` | 12 | counting, maximum, wake-up order, stolen token with the deadline kept, resource pool |
| `event_flags` | 17 | any/all/no-clear, absolute deadline, competing for bits, `try_wait`, `set_isr` |
| `safe_run` | 2 + 8 `static_assert` | complete, incomplete, duplicated, IDLE sets; start through `safe_run()` |
| `round_robin` | 11 | rotation by time slices, finished processes skipped, externally woken process put off |
| `profiler` | 9 | `TProfiler`, `process_profiler<float/uint32_t>`, driven by the context switch hook |
| `watchdog` | 13 | timely `alive`, reboot exactly at the timeout, `asleep`, double `asleep`, zero timeout |
| `service_kit`, `services` | 8 | the basic `TService` operations (`resume_all`, `resume_next_ready` and the ISR versions), the timeout after a successful wait |
| `fuzz` | 5 | see "Fuzzing with injected interrupts" |
| `atomic` | 3 | `TEventFlags::clear()`/`try_wait()` and `TSemaphore::try_acquire()` under asynchronous interrupts |
| `launch` | 6 | priorities by position, captures, `pssSuspended`, alignment, names and stack sizes |
| `wide` | 8 | start order, timeouts, flag, mutex and channel with N processes |

Known defects
-------------

A test written as `UT_TEST_DEFECT(..., "id")` checks the correct behaviour
where a known defect breaks it. Its failure is shown as `[ KNWN ]` and does not
count as an error; if it passes it is shown as `[ FIXD ]` (defect not
reproduced). The ids are those of the v5.2.0 (1-15) and develop (N1-N9) analysis
reports.

| Defect | Tests | v5.2.0 | v5.2.0 + patches 1-6 | develop |
| --- | --- | --- | --- | --- |
| 2 - channel timeout lost | 1 + fuzzing | reproduced | fixed | fixed |
| 3 - `pssSuspended` and the mask table | 1, configuration `masktable` only | reproduced | fixed | reproduced |
| 4 - `Size` above index range | 1 | reproduced | fixed (compile error) | reproduced |
| 5 - `operator[]` overflow | 2 | reproduced | fixed | reproduced |
| 6 - `try_lock` timeout restarts | 2 | reproduced | fixed | reproduced |
| N1 - `try_pop_back` inverted | 2 | - | - | reproduced |
| N3 - watchdog `nullptr` | 2 | - | - | reproduced |
| N4 - profiler division by zero | 2 (v5.2.0: 1) | reproduced (`TProfiler`) | reproduced | reproduced |

| N7 - `force_wake_up()` leaves the tag in the waiting map | 2 | reproduced | reproduced | reproduced |
| N8 - round-robin wakes the first process | 1 | reproduced | reproduced | reproduced |
| N9 - `os_running()` before the kernel constructor | 1 | reproduced | reproduced | reproduced |

N4 affects `TProfiler` too, which exists since v5.2.0, not only `process_profiler`.
Defect 1 (16-bit masks) does not show on a 64-bit host; `test/target16/run.sh`
checks it. On a tree with defect 2 the fuzz tests are marked as a known defect:
they find it within a few seeds.

Totals of the main program: develop - 17 known defects (18 with the mask
table), v5.2.0 - 11-12, v5.2.0 with patches 1-6 - 5; no unexpected failures.

Coverage
--------

`coverage.sh` builds every configuration with `--coverage -O0 -fno-exceptions
-DINLINE=inline`: `INLINE=inline` turns off forced inlining, otherwise gcov counts
the branches of every inlined copy separately; the kernel does not use
exceptions. The data of all configurations is merged: a line is covered if any
configuration ran it.

| Files | Lines | Branches |
| --- | --- | --- |
| kernel (`core/`) | 511/511 - 100% | 131/132 - 99.2% |
| extensions (`ext/`) | 360/368 - 97.8% | 164/174 - 94.3% |
| total, with the port | 996/1016 - 98.0% | 333/358 - 93.0% |

Coverage is measured on the main program and `launch`; the `wide` programs and
the seed sweeps are not part of it.

Not covered in the kernel and the extensions:

- the repeat of the wait loop in `TKernel::sched()` (scheme 1): the port
  switches at the first interrupt enable;
- the endless loops with which `launch` and the watchdog stop the system on an error;
- defensive conditions unreachable through the public API (`safe_run`: priority
  out of range; recursive mutex: owner with zero count; watchdog: corrupted timeout);
- the watchdog branches for an unregistered process: the test does not get
  there because of defect N3.

Mutation testing
----------------

Coverage tells which code ran, not whether its result is checked. `mutate.py`
plants one small error into every executed line (an operator, a constant, a
condition, a deleted statement or critical section), builds the tests and sees
whether any of them notices.

```sh
test/unit/coverage.sh --dump-covered=/tmp/covered.txt
python3 test/unit/mutate.py --covered /tmp/covered.txt --work /tmp/mut \
        --fuzz-seeds 30 --report /tmp/mut.txt
# the survivors once more, under sanitizers
python3 test/unit/mutate.py --covered /tmp/covered.txt --work /tmp/mut \
        --retest /tmp/mut.txt.json --report /tmp/mut-san.txt \
        --cxxflags "-fsanitize=address,undefined -fno-sanitize-recover=undefined"
```

A full run takes about 40 minutes on two cores. Final run (2026-11-13): of 714
compilable mutants the main run killed 662 (kernel 95.1%, extensions 90.0%), and
a re-run of the survivors under ASan and UBSan with three new tests killed 7
more. In total 669 of 714, **93.7%**. Leaving out the 28 mutants that cannot be
killed in this port at all, 669 of 686, **97.5%**. Per-file numbers and every
survivor are in `results/mutation-final_ru.md` (Russian).

| Survivors | Count | Why |
| --- | --- | --- |
| equivalent | 16 | behaviour does not change: `<` for `<=` in a minimum, a repeated timeout reset, `DUMMY_INSTR()` and so on |
| critical sections in `*_isr` | 9 | handlers do not nest in the port; on Cortex-M the sections are needed |
| critical sections around a one-word read | 2 | `is_sleeping()`, `is_suspended()`: atomic on x86-64, not on AVR |
| port specific | 1 | the stack pointer in `context_switch_hook` is constant in the port |
| hidden by known defects | 7 | the body of `try_pop_back` (N1), `put_off` in round-robin (N8) |
| race windows without an interrupt injection point | 5 | `TRecursiveMutex::try_lock()`, profiler, watchdog: need instruction-level stepping |
| defensive stops | 4 | `launch_precheck`, the corrupted-timeout check of the watchdog |
| write into padding | 1 | `i <= proc_count` in round-robin: ASan does not see it |

For sanitizer builds `mutate.py` raises the per-test time limit from 1.5 to 6 s:
the `atomic` tests are slower under ASan, and an equivalent mutant was killed by
timeout.

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
- `ut::on_critsect_exit(hook)` - called whenever an outermost critical section
  ends; `ut::seed()` - the seed of the run; `ut::async_ticks(period_us)` - system
  timer from a real timer (not deterministic, for stress tests only).
- `get_interrupt_state()` returns 1 while the interrupts are disabled.
- `UT_TEST_SUSPENDED` - a test of a `pssSuspended` process; in the mask table
  configuration it counts as a test of defect 3.
- A test without processes is a plain unit test; the kernel is not started.
- Every test runs in its own Linux process (`fork`): a hang (5 s timeout) or a
  crash of one test does not affect the others.
- Write `OS::sleep()`, not `sleep()`, inside tests: the latter clashes with libc `sleep()`.
