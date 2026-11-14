//******************************************************************************
//*
//*     scmRTOS kernel tests: process restart (scmRTOS_PROCESS_RESTART_ENABLE)
//*     and the process map primitives of the port
//*
//******************************************************************************
#include "rtos_test.h"

using namespace OS;
using namespace scmtest;
using ::testing::InSequence;

//==============================================================================
//
//    Process maps (plain unit tests)
//
TEST(ProcessMap, PrioTagHasOneBitPerPriority)
{
    TProcessMap all = 0;
    for(uint_fast8_t pr = 0; pr < PROCESS_COUNT; ++pr)
    {
        const TProcessMap tag = get_prio_tag(pr);
        EXPECT_EQ(tag & (tag - 1), 0u) << "one bit, priority " << int(pr);
        EXPECT_EQ(all & tag, 0u) << "distinct, priority " << int(pr);
        all |= tag;
    }
    EXPECT_EQ(all, (TProcessMap(1) << PROCESS_COUNT) - 1);
}

TEST(ProcessMap, HighestPriorityOfAMap)
{
    // pr0 is the most urgent whatever the numeric order of the priorities
    const TPriority prios[] = { pr0, pr1, pr2, pr3, pr4, prIDLE };
    for(int hi = 0; hi < 6; ++hi)
    {
        TProcessMap m = 0;
        for(int k = hi; k < 6; ++k)
            m |= get_prio_tag(prios[k]);
        EXPECT_EQ(highest_priority(m), prios[hi]) << "map 0x" << std::hex << m;
    }
}

TEST(ProcessMap, SetAndClearTags)
{
    TProcessMap m = 0;
    set_prio_tag(m, get_prio_tag(pr1));
    set_prio_tag(m, get_prio_tag(pr3));
    EXPECT_EQ(m, get_prio_tag(pr1) | get_prio_tag(pr3));
    clr_prio_tag(m, get_prio_tag(pr1));
    EXPECT_EQ(m, get_prio_tag(pr3));
    clr_prio_tag(m, get_prio_tag(pr1));             // clearing again changes nothing
    EXPECT_EQ(m, get_prio_tag(pr3));
}

//==============================================================================
//
//    terminate()
//
namespace
{
    class Restart : public RtosTest { };
}

#if scmRTOS_PROCESS_RESTART_ENABLE == 1

TEST_F(Restart, TerminatedProcessStaysStoppedUntilStarted)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P2 first life"));
        EXPECT_CALL(probe, Mark("P0 restarts P2"));
        EXPECT_CALL(probe, Mark("P0 starts P2@5"));
        EXPECT_CALL(probe, Mark("P2 second life@5"));
    }
    Process(2, [] { Mark("P2 first life"); OS::sleep(); ADD_FAILURE() << "the old body must not go on"; });
    Process(0, [] {
        OS::sleep(1);
        Mark("P0 restarts P2");
        Terminate(2, [] { Mark("P2 second life@" + std::to_string(Ticks())); });
        EXPECT_TRUE(P(2).is_suspended());
        OS::sleep(4);
        EXPECT_TRUE(P(2).is_suspended()) << "terminate() does not start the process";
        Mark("P0 starts P2@" + std::to_string(Ticks()));
        P(2).start();
        OS::sleep(1);
    });
    RunOS();
}

TEST_F(Restart, TerminateWithoutAFunctionRunsTheProcessBodyAgain)
{
    static int lives = 0;
    Process(3, [] { ++lives; OS::sleep(); });
    Process(0, [] {
        OS::sleep(1);
        EXPECT_EQ(lives, 1);
        Terminate(3);
        P(3).start();
        OS::sleep(1);
        EXPECT_EQ(lives, 2);
        Finish();
    });
    RunOS();
}

TEST_F(Restart, TerminateClearsATimeout)
{
    Process(1, [] { OS::sleep(10); ADD_FAILURE() << "old body"; });
    Process(0, [] {
        OS::sleep(1);
        EXPECT_TRUE(P(1).is_sleeping());
        Terminate(1, [] { });
        EXPECT_FALSE(P(1).is_sleeping());
        OS::sleep(20);                              // the old timeout must not start P1
        EXPECT_TRUE(P(1).is_suspended());
        P(1).start();
        OS::sleep(1);
    });
    RunOS();
}

TEST_F(Restart, TerminateRemovesTheProcessFromTheServiceItWaitedFor)
{
    // P1 waited for the flag; after its restart a signal must latch, not wake it
    static TEventFlag flag;
    Process(1, [] { flag.wait(); ADD_FAILURE() << "old body"; });
    Process(0, [] {
        OS::sleep(1);
        EXPECT_NE(P(1).waiting_for(), nullptr);
        Terminate(1, [] { OS::sleep(); });
        EXPECT_EQ(P(1).waiting_for(), nullptr);
        flag.signal();
        EXPECT_TRUE(flag.is_signaled()) << "nobody waits for the flag any more";
        EXPECT_TRUE(P(1).is_suspended());
        P(1).force_wake_up();
        OS::sleep(1);
        Finish();
    });
    RunOS();
}

TEST_F(Restart, TerminatedMutexWaiterDoesNotGetTheMutex)
{
    static TMutex m;
    Process(0, [] {
        m.lock();
        OS::sleep(1);                               // P2 blocks on the mutex
        Terminate(2, [] { OS::sleep(); });
        m.unlock();
        EXPECT_FALSE(m.is_locked());
        OS::sleep(1);
        EXPECT_FALSE(m.is_locked());
        Finish();
    });
    Process(2, [] { m.lock(); ADD_FAILURE() << "old body"; });
    RunOS();
}

#else

TEST_F(Restart, Disabled)
{
    GTEST_SKIP() << "built with scmRTOS_PROCESS_RESTART_ENABLE == 0: see the *_r1 programs";
}

#endif // scmRTOS_PROCESS_RESTART_ENABLE
