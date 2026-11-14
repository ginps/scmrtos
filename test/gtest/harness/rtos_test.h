//******************************************************************************
//*
//*     scmRTOS GoogleTest suite: test harness
//*
//*     Runs the real kernel (core/) on the Linux test port (port/linux/gcc) in
//*     manual tick mode.
//*
//*     Scenario tests derive from scmtest::RtosTest. The test body is the
//*     set-up: it sets GoogleMock expectations and gives bodies to the RTOS
//*     processes P0..P4 (P0 has the highest priority), then calls RunOS().
//*
//*     RunOS() forks. The child starts the kernel and runs the scenario; every
//*     GoogleTest/GoogleMock failure in the child (in a process, an interrupt
//*     handler or a hook) is sent to the parent at once and re-reported there
//*     with its original file and line, so the IDE, the console output and
//*     --gtest_output=xml show it as usual. A hang (timeout), a crash or a
//*     scenario that runs out of ticks is reported as a failure too. The kernel
//*     never returns from OS::run(), so the fork also gives every scenario a
//*     fresh kernel.
//*
//*     The scenario ends when every MustFinish body has returned, or when a
//*     body calls Finish(). The system timer ticks only when every process is
//*     blocked (from the idle process) or when a body calls Tick().
//*
//*     IMPORTANT: everything the scenario does happens in the child. Code after
//*     RunOS() in the test body runs in the parent and sees none of it: put the
//*     final checks into AtEnd() or into the process bodies.
//*
//*     Mock objects of the fixture (probe, hooks, irq) are verified in the child
//*     when the scenario ends. A mock declared in the test body must be passed
//*     to VerifyAtEnd().
//*
//*     A test that does not call RunOS() is an ordinary GoogleTest test and runs
//*     in the test process itself (the kernel is not started).
//*
//******************************************************************************
#ifndef SCMTEST_RTOS_TEST_H
#define SCMTEST_RTOS_TEST_H

#include <scmRTOS.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <functional>
#include <string>
#include <vector>

namespace scmtest
{
    constexpr int kProcCount = scmRTOS_PROCESS_COUNT;          // P0..P4
    constexpr int kIrqCount  = OS::host::IRQ_COUNT;            // user interrupts 0..7

    enum class Mode
    {
        MustFinish,     // the scenario waits for this body to return
        Daemon,         // may block forever; the scenario does not wait for it
        Suspended       // as MustFinish, the process starts suspended (P4 only, pssSuspended)
    };

    //--------------------------------------------------------------------------
    //  Mocks
    //
    // Execution probe: processes and handlers report what they do, the test
    // states the expected order with EXPECT_CALL and InSequence.
    class MockProbe
    {
    public:
        MOCK_METHOD(void, Mark, (const std::string & what));
    };

    // Kernel user hooks (scmRTOS_SYSTIMER_HOOK_ENABLE, scmRTOS_CONTEXT_SWITCH_USER_HOOK_ENABLE).
    // Called from the system timer interrupt and from the scheduler.
    class MockKernelHooks
    {
    public:
        MOCK_METHOD(void, SystemTimer, ());
        MOCK_METHOD(void, ContextSwitch, ());
    };

    // User interrupt handlers installed with Irq(n): the harness calls Handler(n)
    // inside OS::TISRW, after the handler body (if any).
    class MockIrq
    {
    public:
        MOCK_METHOD(void, Handler, (int n));
    };

    //--------------------------------------------------------------------------
    //  Fixture
    //
    class RtosTest : public ::testing::Test
    {
    public:
        RtosTest();
        ~RtosTest() override;

        //--- set-up (test body, before RunOS) ---------------------------------
        void Process(int idx, std::function<void()> body, Mode mode = Mode::MustFinish);
        void Irq(int n, std::function<void()> body = nullptr);      // handler inside OS::TISRW
        void RawIrq(int n, std::function<void()> handler);          // handler without OS::TISRW
        void AtEnd(std::function<void()> check);                    // runs in the child at the end
        void MaxTicks(unsigned n);                                  // tick limit, default 5000
        void VerifyAtEnd(void * mock);                              // a mock declared in the test body

        // Starts the kernel in a child process and waits for the scenario to end.
        void RunOS();

        //--- run time (processes, handlers, hooks) ----------------------------
        static void      Finish();                                  // end the scenario now
        static void      Tick(unsigned n = 1);                      // n system timer ticks here
        static unsigned  Ticks();                                   // OS::get_tick_count()
        static unsigned  Switches();                                // context switches so far
        static void      Raise(int n);                              // request user interrupt n
        static OS::TBaseProcess & P(int idx);                       // process P<idx>
        static OS::TPriority      Prio(int idx);                    // priority of P<idx>
        static bool      Done(int idx);                             // the body of P<idx> returned
        static void      Mark(const std::string & what);            // probe.Mark() in a critical section
    #if scmRTOS_PROCESS_RESTART_ENABLE == 1
        // OS::process::terminate(): P<idx> restarts and runs new_body (or its
        // original body when new_body is empty)
        static void      Terminate(int idx, std::function<void()> new_body = nullptr);
    #endif

        ::testing::StrictMock<MockProbe>     probe;   // every Mark() must be expected
        ::testing::NiceMock<MockKernelHooks> hooks;   // hooks are called all the time: nice
        ::testing::StrictMock<MockIrq>       irq;     // every interrupt must be expected

    private:
        std::vector<void *> extra_mocks_;
        friend struct HarnessAccess;
    };
}

#endif // SCMTEST_RTOS_TEST_H
