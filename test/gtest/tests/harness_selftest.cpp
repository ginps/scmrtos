//******************************************************************************
//*
//*     Self-test of the harness: a failure anywhere in a scenario (a process,
//*     an interrupt handler, a hook, a mock expectation), a hang, a crash and
//*     a scenario that runs out of ticks must reach the test process.
//*
//******************************************************************************
#include "rtos_test.h"

#include <gtest/gtest-spi.h>

using namespace OS;
using namespace scmtest;
using ::testing::InSequence;

namespace
{
    class Harness : public RtosTest
    {
    protected:
        void SetUp() override { Self = this; }
        static Harness * Self;          // EXPECT_*_FAILURE statements cannot use 'this'

        // runs the scenario and collects what it reports, instead of failing
        void RunCaptured(::testing::TestPartResultArray & results)
        {
            ::testing::ScopedFakeTestPartResultReporter capture(
                ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &results);
            RunOS();
        }

        static bool Contains(const ::testing::TestPartResultArray & results, const char * text)
        {
            for(int i = 0; i < results.size(); ++i)
                if(strstr(results.GetTestPartResult(i).message(), text))
                    return true;
            return false;
        }
    };
    Harness * Harness::Self = nullptr;
}

TEST_F(Harness, FailureInAProcessIsReportedAtItsLine)
{
    static const int line = __LINE__ + 1;
    Process(1, [] { EXPECT_EQ(Ticks(), 12345u) << "deliberate"; });
    ::testing::TestPartResultArray results;
    RunCaptured(results);
    ASSERT_EQ(results.size(), 1);
    const ::testing::TestPartResult & r = results.GetTestPartResult(0);
    EXPECT_TRUE(r.nonfatally_failed());
    EXPECT_THAT(r.file_name(), ::testing::EndsWith("harness_selftest.cpp"));
    EXPECT_EQ(r.line_number(), line);
    EXPECT_THAT(r.message(), ::testing::HasSubstr("deliberate"));
    EXPECT_THAT(r.message(), ::testing::HasSubstr("(tick 0)"));
}

TEST_F(Harness, FailureInAnInterruptHandlerIsReported)
{
    EXPECT_CALL(irq, Handler(3));
    Irq(3, [] { ADD_FAILURE() << "from the handler"; });
    Process(0, [] { Raise(3); });
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "from the handler");
}

TEST_F(Harness, UnsatisfiedExpectationIsReported)
{
    EXPECT_CALL(probe, Mark("never"));
    Process(0, [] { });
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "Actual: never called");
}

TEST_F(Harness, UnexpectedCallIsReported)
{
    Process(0, [] { Mark("surprise"); });
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "Uninteresting mock function call");
}

TEST_F(Harness, WrongOrderIsReported)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("A"));
        EXPECT_CALL(probe, Mark("B"));
    }
    Process(0, [] { OS::sleep(1); Mark("A"); });
    Process(1, [] { Mark("B"); });
    ::testing::TestPartResultArray results;
    RunCaptured(results);
    EXPECT_GE(results.size(), 1);
    EXPECT_TRUE(Contains(results, "Unexpected mock function call"));
}

TEST_F(Harness, UnexpectedInterruptIsReported)
{
    Process(0, [] { Raise(5); });
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "Handler(5)");
}

TEST_F(Harness, AssertStopsTheScenario)
{
    Process(0, [] {
        ASSERT_TRUE(false) << "stop here";
        ADD_FAILURE() << "must not run";
    });
    EXPECT_FATAL_FAILURE(Self->RunOS(), "stop here");
}

TEST_F(Harness, ProcessThatNeverFinishesIsReported)
{
    static TEventFlag never;
    MaxTicks(100);
    Process(2, [] { never.wait(); });
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "P2 did not finish within 100 ticks");
}

TEST_F(Harness, DaemonIsNotWaitedFor)
{
    static TEventFlag never;
    Process(2, [] { never.wait(); }, Mode::Daemon);
    Process(3, [] { OS::sleep(3); });
    RunOS();
}

TEST_F(Harness, CrashIsReported)
{
    Process(0, [] { raise(SIGSEGV); });
    ::testing::TestPartResultArray results;
    RunCaptured(results);
    ASSERT_EQ(results.size(), 1);
    // a signal, or exit code 1 when a sanitizer catches the signal first
    EXPECT_TRUE(Contains(results, "the scenario crashed") || Contains(results, "the scenario exited with code"));
}

TEST_F(Harness, HangIsReported)
{
    setenv("SCMTEST_TIMEOUT_MS", "300", 1);
    Process(0, [] { TCritSect cs; for(;;) DUMMY_INSTR(); });   // no ticks, no idle
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "the scenario hung");
    unsetenv("SCMTEST_TIMEOUT_MS");
}

TEST_F(Harness, AtEndRunsInTheScenario)
{
    static int value = 0;
    Process(0, [] { value = 42; });
    AtEnd([] { EXPECT_EQ(value, 41) << "at the end"; });
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "at the end");
    EXPECT_EQ(value, 0) << "the scenario runs in a child process";
}

TEST_F(Harness, FinishEndsTheScenarioEarly)
{
    EXPECT_CALL(probe, Mark("before"));
    Process(0, [] { Mark("before"); Finish(); Mark("after"); });
    RunOS();
}

TEST_F(Harness, MockDeclaredInTheTestIsVerified)
{
    static MockProbe extra;
    EXPECT_CALL(extra, Mark("x")).Times(2);
    VerifyAtEnd(&extra);
    Process(0, [] { TCritSect cs; extra.Mark("x"); });
    EXPECT_NONFATAL_FAILURE(Self->RunOS(), "called once - unsatisfied");
}

TEST_F(Harness, ContextSwitchHookSeesEverySwitch)
{
    // P0 sleeps (switch to P1), P1 sleeps (switch to idle), tick wakes P0
    // (switch to P0), P0 returns and sleeps forever (switch to P1)...
    EXPECT_CALL(hooks, ContextSwitch()).Times(::testing::AtLeast(3));
    Process(0, [] { OS::sleep(1); });
    Process(1, [] { OS::sleep(2); });
    RunOS();
}
