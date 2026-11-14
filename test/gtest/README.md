scmRTOS kernel tests on GoogleTest / GoogleMock
===============================================

([Русская версия](README_ru.md))

The suite tests the scmRTOS kernel (`core/`): the scheduler, `sleep()` and
wake-ups, process start-up, the system timer, interrupts and `TISRW`,
critical sections, process restart, the services `TEventFlag`, `TMutex`,
`channel`, `message`, and `usr::ring_buffer`. The real kernel runs on the
Linux test port (`port/linux/gcc`) in manual tick mode, so every scenario is
deterministic to the tick.

Build and run
-------------

```sh
cmake -S test/gtest -B build/gtest
cmake --build build/gtest -j
ctest --test-dir build/gtest -j8                 # all configurations, ~10 s
ctest --test-dir build/gtest -L s1_o0_r1         # one configuration
ctest --test-dir build/gtest -L known-defect     # known defects only

build/gtest/scmrtos_gtest_s0_o0_r0 --gtest_filter='Mutex.*'
build/gtest/scmrtos_gtest_s0_o0_r0 --gtest_output=xml:report.xml
```

GoogleTest comes from `-DSCMTEST_GTEST_DIR=<source tree>` (offline builds),
an installed package (`find_package(GTest)`), or GitHub via `FetchContent`
(`-DSCMTEST_GTEST_TAG`, default `v1.15.2`). Other options:
`-DSCMRTOS_DIR=<tree>` tests another scmRTOS tree, `-DSCMTEST_SANITIZE=ON`
adds ASan/UBSan, `-DSCMTEST_COVERAGE=ON` adds `--coverage`. At run time
`SCMTEST_TIMEOUT_MS` sets the per-scenario time limit (default 10000) and
`SCMTEST_VERBOSE=1` prints the scenario output of passed tests too.

Requirements: Linux, GCC or Clang with C++17, CMake 3.22+.

Configurations
--------------

Eight programs `scmrtos_gtest_s<S>_o<O>_r<R>`: context switch scheme
(`scmRTOS_CONTEXT_SWITCH_SCHEME` 0/1), priority order
(`scmRTOS_PRIORITY_ORDER` 0/1), process restart
(`scmRTOS_PROCESS_RESTART_ENABLE` 0/1). Every test of every program is a
CTest test named with the configuration prefix (`s1_o0_r1.Mutex.TryLock`)
and labelled with the configuration.

Writing a test
--------------

```cpp
class Mutex : public scmtest::RtosTest { };

TEST_F(Mutex, WaitersGetTheMutexInPriorityOrder)
{
    static TMutex m;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0"));
        EXPECT_CALL(probe, Mark("P1"));
        EXPECT_CALL(probe, Mark("P2"));
    }
    Process(0, [] { OS::sleep(3); TMutexLocker l(m); Mark("P0"); });
    Process(1, [] { OS::sleep(2); TMutexLocker l(m); Mark("P1"); });
    Process(2, [] { OS::sleep(1); TMutexLocker l(m); Mark("P2"); });
    Process(3, [] { m.lock(); OS::sleep(5); m.unlock(); });
    RunOS();
}
```

- The test body is the set-up: `Process(i, body)` gives a body to process
  `P<i>` (5 processes, `P0` has the highest priority, `P4` is declared
  `pssSuspended`); `RunOS()` starts the kernel.
- `RunOS()` forks. The kernel runs in the child; every GoogleTest/GoogleMock
  failure there (in a process, an interrupt handler, a hook, an unsatisfied
  expectation) is sent to the parent at once and re-reported with its own
  file and line. A hang, a crash, or a scenario that runs out of ticks is a
  failure too. Every scenario gets a fresh kernel.
- The scenario ends when every body has returned or a body calls `Finish()`.
  The system timer ticks only when every process is blocked, or on `Tick()`.
- `ASSERT_*` in the child stops the scenario.
- **Code after `RunOS()` runs in the parent and sees nothing of the
  scenario.** Put final checks into `AtEnd()` or into the process bodies.
- A test without `RunOS()` is an ordinary GoogleTest test.

Mocks of the fixture: `probe` (`StrictMock<MockProbe>`, `Mark()` from the
tests), `hooks` (`NiceMock<MockKernelHooks>`: `SystemTimer()` and
`ContextSwitch()` called by the kernel user hooks), `irq`
(`StrictMock<MockIrq>`: `Handler(n)` called by every interrupt installed with
`Irq(n)`). Actions in `hooks`/`irq` expectations run in interrupt context and
may wake processes or signal services, but must not block. Mocks declared in
a test go to `VerifyAtEnd(&mock)`.

Known defects
-------------

`KnownDefect*` tests check the correct behaviour where a known defect of the
tree breaks it (N1, N7, N9, 4, 5, 6 of the analysis reports). CTest runs them
with `WILL_FAIL` and the label `known-defect`: "passed" means the defect is
still there, "failed" means it is gone and the test should move to its
regular suite.

See [README_ru.md](README_ru.md) for the full list of suites.
