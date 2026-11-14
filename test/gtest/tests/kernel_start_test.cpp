//******************************************************************************
//*
//*     scmRTOS kernel tests: start-up, process table, priorities
//*
//******************************************************************************
#include "rtos_test.h"

using namespace OS;
using namespace scmtest;
using ::testing::InSequence;

namespace
{
    class KernelStart : public RtosTest { };
}

TEST(KernelStatic, NotRunningBeforeRun)
{
    // the test program runs in main(): the kernel is constructed, but not started
    EXPECT_FALSE(os_running());
}

TEST(KernelStatic, ProcessTableHoldsEveryProcess)
{
    const TPriority prios[] = { pr0, pr1, pr2, pr3, pr4 };
    for(TPriority pr : prios)
    {
        ASSERT_NE(get_proc(pr), nullptr);
        EXPECT_EQ(get_proc(pr)->priority(), pr);
    }
    ASSERT_NE(get_proc(prIDLE), nullptr);
    EXPECT_EQ(get_proc(prIDLE), &IdleProc);
    EXPECT_STREQ(IdleProc.name(), "Idle");
}

TEST(KernelStatic, ProcessNamesAndStackSizes)
{
    const char * names[] = { "P0", "P1", "P2", "P3", "P4" };
    const TPriority prios[] = { pr0, pr1, pr2, pr3, pr4 };
    for(int i = 0; i < kProcCount; ++i)
    {
        EXPECT_STREQ(get_proc(prios[i])->name(), names[i]);
        EXPECT_EQ(get_proc(prios[i])->stack_size(), 128u * 1024 / sizeof(stack_item_t));
    }
}

TEST(KernelStatic, PriorityValuesFollowTheConfiguredOrder)
{
#if scmRTOS_PRIORITY_ORDER == 0
    EXPECT_EQ(pr0, 0);
    EXPECT_EQ(prIDLE, scmRTOS_PROCESS_COUNT);
#else
    EXPECT_EQ(pr0, scmRTOS_PROCESS_COUNT);
    EXPECT_EQ(prIDLE, 0);
#endif
}

TEST_F(KernelStart, RunningInsideAProcess)
{
    Process(0, [] { EXPECT_TRUE(os_running()); });
    RunOS();
}

TEST_F(KernelStart, ProcessesStartInPriorityOrder)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0"));
        EXPECT_CALL(probe, Mark("P1"));
        EXPECT_CALL(probe, Mark("P2"));
        EXPECT_CALL(probe, Mark("P3"));
        EXPECT_CALL(probe, Mark("P4"));
    }
    // set up in a mixed order: the start order depends on the priorities only
    Process(3, [] { Mark("P3"); });
    Process(0, [] { Mark("P0"); });
    Process(4, [] { Mark("P4"); });
    Process(2, [] { Mark("P2"); });
    Process(1, [] { Mark("P1"); });
    RunOS();
}

TEST_F(KernelStart, TickCountStartsAtZero)
{
    Process(0, [] { EXPECT_EQ(get_tick_count(), 0u); });
    RunOS();
}

TEST_F(KernelStart, FirstProcessRunsWithInterruptsEnabled)
{
    Process(0, [] { EXPECT_FALSE(get_interrupt_state()); });
    RunOS();
}

TEST_F(KernelStart, CurrentProcessIsTheHighestReady)
{
    // P1 runs while P0 sleeps; the idle process runs while everybody sleeps
    Process(0, [] {
        EXPECT_FALSE(P(0).is_suspended());
        OS::sleep(5);
        EXPECT_EQ(Ticks(), 5u);
    });
    Process(1, [] {
        EXPECT_TRUE(P(0).is_suspended());
        EXPECT_TRUE(P(0).is_sleeping());
        EXPECT_FALSE(P(1).is_suspended());
    });
    RunOS();
}

TEST_F(KernelStart, SuspendedProcessWaitsForStart)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0 starts P4"));
        EXPECT_CALL(probe, Mark("P4"));
        EXPECT_CALL(probe, Mark("P0 again"));
    }
    Process(4, [] { Mark("P4"); }, Mode::Suspended);
    Process(0, [] {
        OS::sleep(10);
        EXPECT_TRUE(P(4).is_suspended());           // never ran
        EXPECT_FALSE(P(4).is_sleeping());
        Mark("P0 starts P4");
        P(4).start();                               // lower priority: runs when P0 blocks
        OS::sleep(1);
        Mark("P0 again");
    });
    RunOS();
}

TEST_F(KernelStart, SuspendedProcessStartedByAHigherOneDoesNotPreemptIt)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("start"));
        EXPECT_CALL(probe, Mark("after start"));
        EXPECT_CALL(probe, Mark("P4"));
    }
    Process(4, [] { Mark("P4"); }, Mode::Suspended);
    Process(3, [] { Mark("start"); P(4).start(); Mark("after start"); });
    RunOS();
}

TEST_F(KernelStart, StackOfAProcessIsFilledWithThePattern)
{
    Process(1, [] {
        const size_t size  = P(1).stack_size();
        const size_t slack = P(1).stack_slack();
        EXPECT_GT(slack, 0u);
        EXPECT_LT(slack, size);                     // the process uses some of it
        EXPECT_GT(P(4).stack_slack(), 0u);
    });
    RunOS();
}

TEST_F(KernelStart, NoBodiesEndsAtOnce)
{
    // a scenario with no process bodies ends before the kernel starts
    bool ran = false;
    AtEnd([&] { ran = true; EXPECT_FALSE(os_running()); });
    RunOS();
    EXPECT_FALSE(ran) << "AtEnd() runs in the child process, never in the test process";
}
