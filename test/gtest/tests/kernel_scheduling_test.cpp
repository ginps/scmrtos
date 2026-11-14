//******************************************************************************
//*
//*     scmRTOS kernel tests: scheduling, sleep(), wake_up(), force_wake_up()
//*
//******************************************************************************
#include "rtos_test.h"

using namespace OS;
using namespace scmtest;
using ::testing::InSequence;

namespace
{
    class Scheduling : public RtosTest { };

    std::string at(const char * what) { return std::string(what) + "@" + std::to_string(RtosTest::Ticks()); }
}

TEST_F(Scheduling, WokenHigherProcessPreemptsAtOnce)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0 waits"));
        EXPECT_CALL(probe, Mark("P1 wakes P0"));
        EXPECT_CALL(probe, Mark("P0 woken"));
        EXPECT_CALL(probe, Mark("P1 goes on"));
    }
    Process(0, [] { Mark("P0 waits"); OS::sleep(); Mark("P0 woken"); });
    Process(1, [] { Mark("P1 wakes P0"); P(0).force_wake_up(); Mark("P1 goes on"); });
    RunOS();
}

TEST_F(Scheduling, WokenLowerProcessWaitsForTheCpu)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P3 waits"));
        EXPECT_CALL(probe, Mark("P1 wakes P3"));
        EXPECT_CALL(probe, Mark("P1 goes on"));
        EXPECT_CALL(probe, Mark("P3 woken"));
    }
    Process(3, [] { Mark("P3 waits"); OS::sleep(); Mark("P3 woken"); });
    Process(1, [] {
        OS::sleep(1);                               // let P3 block first
        Mark("P1 wakes P3");
        P(3).force_wake_up();
        Mark("P1 goes on");
    });
    RunOS();
}

TEST_F(Scheduling, SleepWithTimeoutResumesOnTheTick)
{
    Process(0, [] {
        OS::sleep(7);
        EXPECT_EQ(Ticks(), 7u);
        OS::sleep(1);
        EXPECT_EQ(Ticks(), 8u);
    });
    RunOS();
}

TEST_F(Scheduling, TimeoutsOfSeveralProcessesExpireInTimeOrder)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P1@3"));
        EXPECT_CALL(probe, Mark("P2@5"));
        EXPECT_CALL(probe, Mark("P0@7"));
    }
    Process(0, [] { OS::sleep(7); Mark(at("P0")); });
    Process(1, [] { OS::sleep(3); Mark(at("P1")); });
    Process(2, [] { OS::sleep(5); Mark(at("P2")); });
    RunOS();
}

TEST_F(Scheduling, SameTickTimeoutsRunInPriorityOrder)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0@4"));
        EXPECT_CALL(probe, Mark("P2@4"));
        EXPECT_CALL(probe, Mark("P4@4"));
    }
    Process(4, [] { OS::sleep(4); Mark(at("P4")); });
    Process(2, [] { OS::sleep(4); Mark(at("P2")); });
    Process(0, [] { OS::sleep(4); Mark(at("P0")); });
    RunOS();
}

TEST_F(Scheduling, LowerProcessRunsOnlyWhileTheHigherBlocks)
{
    // P1 counts ticks itself; P0 holds the CPU for 3 ticks without blocking
    static unsigned p1_runs = 0;
    Process(0, [] {
        Tick(3);                                    // P1 cannot run in between
        EXPECT_EQ(p1_runs, 0u);
        OS::sleep(2);
        EXPECT_EQ(p1_runs, 1u);
    });
    Process(1, [] { ++p1_runs; });
    RunOS();
}

TEST_F(Scheduling, WakeUpEndsATimedSleep)
{
    Process(0, [] {
        OS::sleep(100);
        EXPECT_EQ(Ticks(), 3u);
        EXPECT_FALSE(P(0).is_sleeping());
    });
    Process(1, [] {
        OS::sleep(3);
        EXPECT_TRUE(P(0).is_sleeping());
        EXPECT_TRUE(P(0).is_suspended());
        P(0).wake_up();
    });
    RunOS();
}

TEST_F(Scheduling, WakeUpIgnoresAProcessSleepingWithoutTimeout)
{
    Process(0, [] { OS::sleep(); EXPECT_EQ(Ticks(), 5u); });
    Process(1, [] {
        OS::sleep(2);
        EXPECT_FALSE(P(0).is_sleeping());           // sleep(0): no timeout
        EXPECT_TRUE(P(0).is_suspended());
        P(0).wake_up();                             // only a timed sleep can be ended by wake_up()
        EXPECT_TRUE(P(0).is_suspended());
        OS::sleep(3);
        P(0).force_wake_up();
    });
    RunOS();
}

TEST_F(Scheduling, ForceWakeUpEndsATimedSleepAndClearsTheTimeout)
{
    Process(0, [] {
        OS::sleep(50);
        EXPECT_EQ(Ticks(), 2u);
        OS::sleep(10);                              // the old timeout must not cut this one short
        EXPECT_EQ(Ticks(), 12u);
    });
    Process(1, [] { OS::sleep(2); P(0).force_wake_up(); });
    RunOS();
}

TEST_F(Scheduling, WakingTheRunningProcessChangesNothing)
{
    Process(1, [] {
        const unsigned before = Switches();
        P(1).force_wake_up();
        P(1).wake_up();
        EXPECT_EQ(Switches(), before);
        EXPECT_FALSE(P(1).is_suspended());
        EXPECT_FALSE(P(1).is_sleeping());
    });
    RunOS();
}

TEST_F(Scheduling, WakingAReadyProcessKeepsItsTurn)
{
    // P2 is ready but not running (P1 holds the CPU): waking it again is harmless
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P1"));
        EXPECT_CALL(probe, Mark("P2"));
    }
    Process(1, [] { P(2).force_wake_up(); P(2).wake_up(); Mark("P1"); });
    Process(2, [] { Mark("P2"); });
    RunOS();
}

TEST_F(Scheduling, PingPongBetweenTwoProcesses)
{
    {
        InSequence seq;
        for(int i = 0; i < 3; ++i)
        {
            EXPECT_CALL(probe, Mark("ping " + std::to_string(i)));
            EXPECT_CALL(probe, Mark("pong " + std::to_string(i)));
        }
    }
    Process(1, [] {
        for(int i = 0; i < 3; ++i)
        {
            OS::sleep();                            // woken by P2
            Mark("ping " + std::to_string(i));
        }
    });
    Process(2, [] {
        for(int i = 0; i < 3; ++i)
        {
            P(1).force_wake_up();                   // P1 runs at once
            Mark("pong " + std::to_string(i));
        }
    });
    RunOS();
}

TEST_F(Scheduling, ExactNumberOfContextSwitches)
{
    // P0 sleeps: P1, P2, P3, P4 run in turn and block (their bodies are empty),
    // then the idle process. The tick wakes P0. 6 switches in all.
    EXPECT_CALL(hooks, ContextSwitch()).Times(6);
    Process(0, [] { OS::sleep(1); });
    RunOS();
}

TEST_F(Scheduling, NoContextSwitchWithoutAReason)
{
    // ticks, a critical section and a wake-up of the running process do not switch
    EXPECT_CALL(hooks, ContextSwitch()).Times(0);
    Process(0, [] {
        Tick(5);
        { TCritSect cs; }
        P(0).force_wake_up();
        Finish();                                   // before P0 blocks
    });
    RunOS();
}

TEST_F(Scheduling, SleepCountsFromTheCurrentTick)
{
    Process(0, [] {
        Tick(10);                                   // P0 does not block: 10 ticks pass
        OS::sleep(5);
        EXPECT_EQ(Ticks(), 15u);
    });
    RunOS();
}

TEST_F(Scheduling, ForceWokenProcessRunsWhenTheCpuIsFree)
{
    // P1 is woken by force while P0 runs; the ticks P0 makes do not touch P1
    Process(0, [] {
        OS::sleep(1);
        P(1).force_wake_up();                       // P1 is ready, P0 keeps the CPU
        Tick(10);
        EXPECT_FALSE(P(1).is_sleeping());
        EXPECT_FALSE(P(1).is_suspended());
        OS::sleep(1);
    });
    Process(1, [] { OS::sleep(5); EXPECT_EQ(Ticks(), 11u); });
    RunOS();
}

//------------------------------------------------------------------------------
//  sleep(n) for a range of n, including the type limits of timeout_t (uint16_t)
//
class SleepFor : public RtosTest, public ::testing::WithParamInterface<timeout_t>
{
protected:
    static timeout_t Duration;
};
timeout_t SleepFor::Duration = 0;

TEST_P(SleepFor, ResumesExactlyAfterTheTimeout)
{
    Duration = GetParam();
    MaxTicks(70000);
    EXPECT_CALL(hooks, SystemTimer()).Times(Duration);
    Process(2, [] {
        OS::sleep(Duration);
        EXPECT_EQ(Ticks(), Duration);
    });
    RunOS();
}

INSTANTIATE_TEST_SUITE_P(Kernel, SleepFor,
                         ::testing::Values(1, 2, 3, 10, 127, 128, 255, 256, 1000, 32767, 32768, 65535),
                         [](const ::testing::TestParamInfo<timeout_t> & i) { return std::to_string(i.param) + "ticks"; });
