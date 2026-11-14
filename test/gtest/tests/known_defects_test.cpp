//******************************************************************************
//*
//*     scmRTOS kernel tests: known defects
//*
//*     Every test here checks the CORRECT behaviour at a place where a known
//*     defect of the tree breaks it, so on a tree with the defect it fails.
//*     CTest registers the suite with WILL_FAIL: "passed" means "the defect is
//*     still there", "failed" means "the defect is gone - move the test to its
//*     regular suite". Run directly, the program shows the actual failures:
//*
//*         scmrtos_gtest_s0_o0_r0 --gtest_filter='KnownDefect.*'
//*
//*     The ids refer to the analysis reports: 1-15 for v5.2.0, N1-N9 for develop.
//*
//******************************************************************************
#include "rtos_test.h"

using namespace OS;
using namespace scmtest;

namespace
{
    class KnownDefect : public RtosTest { };

    // N9: a global object constructed before OS::Kernel. The test sources are
    // linked before core/os_kernel.cpp, so this constructor runs first.
    struct TEarlyProbe
    {
        bool running;
        TEarlyProbe() : running(os_running()) { }
    } EarlyProbe;
}

//------------------------------------------------------------------------------
//  N9: os_running() is true in a global constructor that runs before the
//      constructor of OS::Kernel (CurProcPriority is still zero-initialized)
//
TEST_F(KnownDefect, N9_OsRunningInAnEarlyGlobalConstructor)
{
    EXPECT_FALSE(EarlyProbe.running);
}

//------------------------------------------------------------------------------
//  N1: channel::try_pop_back() has its condition inverted
//
TEST_F(KnownDefect, N1_TryPopBackOnAnEmptyChannel)
{
    static channel<int, 4> c;
    Process(0, [] {
        int x = -1;
        EXPECT_FALSE(c.try_pop_back(x));
        EXPECT_EQ(c.get_count(), 0);
    });
    RunOS();
}

TEST_F(KnownDefect, N1_TryPopBackOnAFilledChannel)
{
    static channel<int, 4> c;
    Process(0, [] {
        c.push(1);
        c.push(2);
        int x = -1;
        EXPECT_TRUE(c.try_pop_back(x));
        EXPECT_EQ(x, 2);
        EXPECT_EQ(c.get_count(), 1);
    });
    RunOS();
}

//------------------------------------------------------------------------------
//  6: TMutex::try_lock(timeout) restarts the timeout every time the mutex is
//     released and taken again before the waiter runs
//
TEST_F(KnownDefect, D6_TryLockTimeoutIsTotalAcrossHandovers)
{
    static TMutex m;
    static bool   result = true;
    static unsigned tick = 0;
    Process(0, [] { m.lock(); OS::sleep(3); m.unlock(); m.lock(); OS::sleep(20); m.unlock(); });
    Process(1, [] { result = m.try_lock(5); tick = Ticks(); });
    AtEnd([] {
        EXPECT_FALSE(result);
        EXPECT_EQ(tick, 5u) << "try_lock(5) from tick 0 must give up at tick 5";
    });
    RunOS();
}

//------------------------------------------------------------------------------
//  N7: force_wake_up() of a process that waits in TMutex::lock() or in
//      channel::push() leaves its tag in the waiting map of the service; the
//      next unlock()/pop() "resumes" it out of an unrelated sleep()
//
TEST_F(KnownDefect, N7_ForcedWakeUpLeavesNoStaleMutexWaiter)
{
    static TMutex m;
    static unsigned slept = 0;
    Process(0, [] {
        m.lock();
        OS::sleep(1);                               // P1 blocks on the mutex
        P(1).force_wake_up();
        m.unlock();
        OS::sleep(5);
        m.lock(); m.unlock();                       // must not affect P1
        OS::sleep(100);
    }, Mode::Daemon);
    Process(1, [] {
        m.lock(); m.unlock();
        const unsigned t0 = Ticks();
        OS::sleep(50);
        slept = Ticks() - t0;
    });
    AtEnd([] { EXPECT_EQ(slept, 50u); });
    RunOS();
}

TEST_F(KnownDefect, N7_ForcedWakeUpLeavesNoStaleProducer)
{
    static channel<int, 1> c;
    static unsigned slept = 0;
    Process(0, [] {
        c.push(1);
        OS::sleep(1);                               // P1 blocks in push()
        P(1).force_wake_up();
        int x; c.pop(x);
        OS::sleep(5);
        c.pop(x);                                   // wakes "producers": must not affect P1
        OS::sleep(100);
    }, Mode::Daemon);
    Process(1, [] {
        c.push(2);
        const unsigned t0 = Ticks();
        OS::sleep(50);
        slept = Ticks() - t0;
    });
    AtEnd([] { EXPECT_EQ(slept, 50u); });
    RunOS();
}

//------------------------------------------------------------------------------
//  4: ring_buffer with a Size its index type cannot hold (uint8_t, Size 300)
//
TEST(KnownDefectStatic, D4_SizeAboveTheIndexRange)
{
    usr::ring_buffer<int, 300> b;
    EXPECT_EQ(b.get_free_size(), 300);
    int n = 0;
    while(n < 1000 && b.push_back(n))
        ++n;
    EXPECT_EQ(n, 300);
}

//------------------------------------------------------------------------------
//  5: ring_buffer::operator[] computes First + index in the index type
//
TEST(KnownDefectStatic, D5_IndexAcrossTheWrapWithSize200)
{
    usr::ring_buffer<int, 200> b;
    for(int i = 0; i < 150; ++i) b.push_back(i);
    for(int i = 0; i < 150; ++i) b.pop_front();     // First = 150
    for(int i = 0; i < 170; ++i) b.push_back(1000 + i);
    EXPECT_EQ(b[120], 1120);
}
