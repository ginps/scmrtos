//******************************************************************************
//*
//*     scmRTOS kernel tests: TEventFlag, TMutex, message<T>
//*
//******************************************************************************
#include "rtos_test.h"

using namespace OS;
using namespace scmtest;
using ::testing::InSequence;
using ::testing::Invoke;

namespace
{
    class EventFlag : public RtosTest { };
    class Mutex     : public RtosTest { };
    class Message   : public RtosTest { };

    std::string at(const std::string & what) { return what + "@" + std::to_string(RtosTest::Ticks()); }
}

//==============================================================================
//
//    TEventFlag
//
TEST(EventFlagStatic, InitialValue)
{
    TEventFlag off;
    TEventFlag on(TEventFlag::efOn);
    EXPECT_FALSE(off.is_signaled());
    EXPECT_TRUE(on.is_signaled());
    on.clear();
    EXPECT_FALSE(on.is_signaled());
}

TEST_F(EventFlag, SignalWithoutWaiterLatches)
{
    static TEventFlag flag;
    Process(0, [] {
        flag.signal();
        EXPECT_TRUE(flag.is_signaled());
        EXPECT_TRUE(flag.wait());                   // at once
        EXPECT_FALSE(flag.is_signaled()) << "wait() consumes the signal";
        EXPECT_EQ(Ticks(), 0u);
    });
    RunOS();
}

TEST_F(EventFlag, WaitBlocksUntilSignal)
{
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("signal@3"));
        EXPECT_CALL(probe, Mark("P0 woken@3"));
        EXPECT_CALL(probe, Mark("P1 goes on"));
    }
    Process(0, [] { EXPECT_TRUE(flag.wait()); Mark(at("P0 woken")); });
    Process(1, [] { OS::sleep(3); Mark(at("signal")); flag.signal(); Mark("P1 goes on"); });
    RunOS();
}

TEST_F(EventFlag, SignalWakesEveryWaiterInPriorityOrder)
{
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P1"));
        EXPECT_CALL(probe, Mark("P2"));
        EXPECT_CALL(probe, Mark("P3"));
    }
    Process(3, [] { flag.wait(); Mark("P3"); });
    Process(1, [] { flag.wait(); Mark("P1"); });
    Process(2, [] { flag.wait(); Mark("P2"); });
    Process(4, [] {
        flag.signal();
        EXPECT_FALSE(flag.is_signaled()) << "a signal that woke waiters does not latch";
    });
    RunOS();
}

TEST_F(EventFlag, SignalIsrWakesEveryWaiter)
{
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(irq, Handler(1));
        EXPECT_CALL(probe, Mark("P0"));
        EXPECT_CALL(probe, Mark("P1"));
        EXPECT_CALL(probe, Mark("P2"));
    }
    Irq(1, [] { flag.signal_isr(); });
    Process(0, [] { flag.wait(); Mark("P0"); });
    Process(1, [] { flag.wait(); Mark("P1"); });
    Process(2, [] { flag.wait(); Mark("P2"); });
    Process(3, [] { Raise(1); EXPECT_FALSE(flag.is_signaled()); });
    RunOS();
}

TEST_F(EventFlag, WaitTimesOut)
{
    static TEventFlag flag;
    Process(1, [] {
        EXPECT_FALSE(flag.wait(5));
        EXPECT_EQ(Ticks(), 5u);
    });
    RunOS();
}

TEST_F(EventFlag, SignalBeforeTheDeadlineWins)
{
    static TEventFlag flag;
    Process(0, [] {
        EXPECT_TRUE(flag.wait(10));
        EXPECT_EQ(Ticks(), 4u);
        EXPECT_FALSE(P(0).is_sleeping());
        OS::sleep(20);                              // the old timeout is gone
        EXPECT_EQ(Ticks(), 24u);
    });
    Process(1, [] { OS::sleep(4); flag.signal(); });
    RunOS();
}

TEST_F(EventFlag, TimedOutWaiterIsNoLongerAWaiter)
{
    static TEventFlag flag;
    Process(0, [] {
        EXPECT_FALSE(flag.wait(2));
        OS::sleep(10);
    });
    Process(1, [] {
        OS::sleep(5);                               // P0 timed out at tick 2
        flag.signal();
        EXPECT_TRUE(flag.is_signaled()) << "nobody waits: the signal latches";
        EXPECT_TRUE(P(0).is_sleeping()) << "the signal must not wake P0 from sleep()";
    });
    RunOS();
}

TEST_F(EventFlag, ForceWakeUpEndsTheWaitWithFalse)
{
    static TEventFlag flag;
    Process(0, [] {
        EXPECT_FALSE(flag.wait());
        EXPECT_EQ(Ticks(), 2u);
        EXPECT_EQ(P(0).waiting_for(), nullptr);
    });
    Process(1, [] { OS::sleep(2); P(0).force_wake_up(); });
    RunOS();
}

TEST_F(EventFlag, WakeUpEndsATimedWaitWithFalse)
{
    static TEventFlag flag;
    Process(0, [] { EXPECT_FALSE(flag.wait(100)); EXPECT_EQ(Ticks(), 3u); });
    Process(1, [] { OS::sleep(3); P(0).wake_up(); });
    RunOS();
}

TEST_F(EventFlag, WaitingForShowsTheService)
{
    static TEventFlag flag;
    Process(0, [] { flag.wait(); });
    Process(1, [] {
        EXPECT_EQ(static_cast<const void *>(P(0).waiting_for()), static_cast<const void *>(&flag));
        EXPECT_EQ(P(1).waiting_for(), nullptr);
        flag.signal();
    });
    RunOS();
}

TEST_F(EventFlag, ClearDropsALatchedSignal)
{
    static TEventFlag flag;
    Process(0, [] {
        flag.signal();
        flag.clear();
        EXPECT_FALSE(flag.wait(3));
    });
    RunOS();
}

TEST_F(EventFlag, SignalIsrLatchesWhenNobodyWaits)
{
    static TEventFlag flag;
    EXPECT_CALL(irq, Handler(0));
    Irq(0, [] { flag.signal_isr(); });
    Process(0, [] { Raise(0); EXPECT_TRUE(flag.is_signaled()); EXPECT_TRUE(flag.wait()); });
    RunOS();
}

TEST_F(EventFlag, SignalFromTheTimerHook)
{
    static TEventFlag flag;
    EXPECT_CALL(hooks, SystemTimer()).WillRepeatedly(Invoke([] {
        if(Ticks() == 6)
            flag.signal_isr();
    }));
    Process(2, [] { EXPECT_TRUE(flag.wait()); EXPECT_EQ(Ticks(), 7u); });
    RunOS();
}

//==============================================================================
//
//    TMutex
//
TEST_F(Mutex, LockAndUnlockWithoutContention)
{
    static TMutex m;
    Process(0, [] {
        EXPECT_FALSE(m.is_locked());
        m.lock();
        EXPECT_TRUE(m.is_locked());
        m.unlock();
        EXPECT_FALSE(m.is_locked());
    });
    RunOS();
}

TEST_F(Mutex, HigherProcessBlocksUntilTheOwnerUnlocks)
{
    static TMutex m;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P2 locked"));
        EXPECT_CALL(probe, Mark("P2 unlocks@3"));
        EXPECT_CALL(probe, Mark("P0 locked@3"));
        EXPECT_CALL(probe, Mark("P2 after unlock"));
    }
    Process(0, [] { OS::sleep(1); m.lock(); Mark(at("P0 locked")); m.unlock(); });
    Process(2, [] {
        m.lock();
        Mark("P2 locked");
        Tick(3);                                    // P0 blocks on the mutex at tick 1
        Mark(at("P2 unlocks"));
        m.unlock();                                 // P0 gets it and runs at once
        Mark("P2 after unlock");
    });
    RunOS();
}

TEST_F(Mutex, WaitersGetTheMutexInPriorityOrder)
{
    static TMutex m;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0"));
        EXPECT_CALL(probe, Mark("P1"));
        EXPECT_CALL(probe, Mark("P2"));
    }
    // P2, P1, P0 block on the mutex (in that order) while P3 holds it
    Process(0, [] { OS::sleep(3); TMutexLocker l(m); Mark("P0"); });
    Process(1, [] { OS::sleep(2); TMutexLocker l(m); Mark("P1"); });
    Process(2, [] { OS::sleep(1); TMutexLocker l(m); Mark("P2"); });
    Process(3, [] { m.lock(); OS::sleep(5); m.unlock(); });
    RunOS();
}

TEST_F(Mutex, UnlockWakesOnlyTheNextOwner)
{
    // the other waiters stay blocked: no needless wake-ups and context switches
    static TMutex m;
    Process(1, [] {
        OS::sleep(1);
        m.lock();                                   // gets it from P3 at tick 3
        EXPECT_TRUE(P(2).is_suspended()) << "P2 must still wait for the mutex";
        m.unlock();
    });
    Process(2, [] { OS::sleep(1); m.lock(); EXPECT_EQ(Ticks(), 3u); m.unlock(); });
    Process(3, [] { m.lock(); OS::sleep(3); m.unlock(); });
    RunOS();
}

TEST_F(Mutex, LowerWaiterGetsTheMutexWhenTheOwnerBlocks)
{
    static TMutex m;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0 unlocked"));
        EXPECT_CALL(probe, Mark("P3 locked@2"));
    }
    Process(0, [] { m.lock(); OS::sleep(2); m.unlock(); Mark("P0 unlocked"); OS::sleep(1); });
    Process(3, [] { m.lock(); Mark(at("P3 locked")); m.unlock(); });
    RunOS();
}

TEST_F(Mutex, UnlockByAnotherProcessIsIgnored)
{
    static TMutex m;
    Process(0, [] { m.lock(); OS::sleep(2); EXPECT_TRUE(m.is_locked()); m.unlock(); });
    Process(1, [] { m.unlock(); EXPECT_TRUE(m.is_locked()); });
    RunOS();
}

TEST_F(Mutex, TryLock)
{
    static TMutex m;
    Process(0, [] { OS::sleep(1); EXPECT_FALSE(m.try_lock()); OS::sleep(5); EXPECT_TRUE(m.try_lock()); m.unlock(); });
    Process(1, [] { EXPECT_TRUE(m.try_lock()); OS::sleep(3); m.unlock(); });
    RunOS();
}

TEST_F(Mutex, TryLockWithTimeoutTimesOut)
{
    static TMutex m;
    Process(0, [] {
        OS::sleep(1);
        EXPECT_FALSE(m.try_lock(4));
        EXPECT_EQ(Ticks(), 5u);
        OS::sleep(10);
        EXPECT_FALSE(m.is_locked()) << "a timed-out waiter must not get the mutex later";
    });
    Process(1, [] { m.lock(); OS::sleep(8); m.unlock(); });
    RunOS();
}

TEST_F(Mutex, TryLockWithTimeoutGetsAReleasedMutex)
{
    static TMutex m;
    Process(0, [] {
        OS::sleep(1);
        EXPECT_TRUE(m.try_lock(10));
        EXPECT_EQ(Ticks(), 4u);
        EXPECT_FALSE(P(0).is_sleeping()) << "the timeout of a successful try_lock() must be cleared";
        OS::sleep(20);
        EXPECT_EQ(Ticks(), 24u);
        m.unlock();
    });
    Process(1, [] { m.lock(); OS::sleep(4); m.unlock(); });
    RunOS();
}

TEST_F(Mutex, UnlockIsrReleasesAndWakesTheWaiter)
{
    static TMutex m;
    {
        InSequence seq;
        EXPECT_CALL(irq, Handler(0));
        EXPECT_CALL(probe, Mark("P0 got it"));
        EXPECT_CALL(probe, Mark("P1"));
    }
    Irq(0, [] { m.unlock_isr(); });
    Process(0, [] { OS::sleep(1); m.lock(); Mark("P0 got it"); });
    Process(1, [] { m.lock(); OS::sleep(2); Raise(0); Mark("P1"); });
    AtEnd([] { EXPECT_TRUE(m.is_locked()); });
    RunOS();
}

TEST_F(Mutex, ScopedLockerUnlocksAtScopeExit)
{
    static TMutex m;
    Process(0, [] {
        {
            TMutexLocker l(m);
            EXPECT_TRUE(m.is_locked());
        }
        EXPECT_FALSE(m.is_locked());
    });
    RunOS();
}

TEST_F(Mutex, MutualExclusionUnderPreemption)
{
    // three processes increment a shared counter in a non-atomic way, with
    // ticks inside the protected region; the mutex must keep it consistent
    static TMutex m;
    static volatile int counter = 0;
    static volatile int inside  = 0;
    auto worker = [] {
        for(int i = 0; i < 20; ++i)
        {
            TMutexLocker l(m);
            EXPECT_EQ(inside, 0) << "two owners at once";
            inside = inside + 1;
            int c = counter;
            OS::sleep(1);                           // the others run and try to lock
            counter = c + 1;
            inside = inside - 1;
        }
    };
    Process(0, worker);
    Process(1, worker);
    Process(2, worker);
    AtEnd([] { EXPECT_EQ(counter, 60); });
    RunOS();
}

//==============================================================================
//
//    message<T>
//
namespace
{
    struct TPoint { int x, y; };
}

TEST_F(Message, SendWithoutWaiterLatches)
{
    static message<int> msg;
    Process(0, [] {
        EXPECT_FALSE(msg.is_non_empty());
        msg = 5;
        msg.send();
        EXPECT_TRUE(msg.is_non_empty());
        EXPECT_TRUE(msg.wait());
        EXPECT_FALSE(msg.is_non_empty());
        EXPECT_EQ(int(msg), 5);
    });
    RunOS();
}

TEST_F(Message, WaitBlocksUntilSend)
{
    static message<int> msg;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("send"));
        EXPECT_CALL(probe, Mark("P0 got 77@2"));
        EXPECT_CALL(probe, Mark("P1"));
    }
    Process(0, [] { EXPECT_TRUE(msg.wait()); int v; msg.out(v); Mark(at("P0 got " + std::to_string(v))); });
    Process(1, [] { OS::sleep(2); msg = 77; Mark("send"); msg.send(); Mark("P1"); });
    RunOS();
}

TEST_F(Message, SendWakesEveryWaiter)
{
    static message<int> msg;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P1 9"));
        EXPECT_CALL(probe, Mark("P2 9"));
    }
    Process(1, [] { msg.wait(); Mark("P1 " + std::to_string(int(msg))); });
    Process(2, [] { msg.wait(); Mark("P2 " + std::to_string(int(msg))); });
    Process(3, [] { msg = 9; msg.send(); EXPECT_FALSE(msg.is_non_empty()); });
    RunOS();
}

TEST_F(Message, WaitTimesOut)
{
    static message<int> msg;
    Process(0, [] { EXPECT_FALSE(msg.wait(6)); EXPECT_EQ(Ticks(), 6u); });
    RunOS();
}

TEST_F(Message, ResetDropsALatchedSend)
{
    static message<int> msg;
    Process(0, [] { msg.send(); msg.reset(); EXPECT_FALSE(msg.is_non_empty()); EXPECT_FALSE(msg.wait(2)); });
    RunOS();
}

TEST_F(Message, SendIsrFromAHandler)
{
    static message<TPoint> msg;
    EXPECT_CALL(irq, Handler(3));
    Irq(3, [] { msg = TPoint{ 3, -4 }; msg.send_isr(); });
    Process(0, [] {
        EXPECT_TRUE(msg.wait());
        TPoint p = msg;
        EXPECT_EQ(p.x, 3);
        EXPECT_EQ(p.y, -4);
    });
    Process(1, [] { OS::sleep(1); Raise(3); });
    RunOS();
}

TEST_F(Message, ForceWakeUpEndsTheWaitWithFalse)
{
    static message<int> msg;
    Process(0, [] { EXPECT_FALSE(msg.wait()); });
    Process(1, [] { P(0).force_wake_up(); });
    RunOS();
}
