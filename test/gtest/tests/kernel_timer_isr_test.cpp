//******************************************************************************
//*
//*     scmRTOS kernel tests: system timer, user hooks, interrupts (TISRW),
//*     critical sections
//*
//******************************************************************************
#include "rtos_test.h"

using namespace OS;
using namespace scmtest;
using ::testing::AnyNumber;
using ::testing::InSequence;
using ::testing::Invoke;

namespace
{
    class SystemTimer : public RtosTest { };
    class Interrupts  : public RtosTest { };
    class CriticalSection : public RtosTest { };
}

//==============================================================================
//
//    System timer and its hook
//
TEST_F(SystemTimer, HookIsCalledOncePerTick)
{
    EXPECT_CALL(hooks, SystemTimer()).Times(10);
    Process(0, [] { OS::sleep(10); });
    RunOS();
}

TEST_F(SystemTimer, HookRunsInsideTheInterrupt)
{
    EXPECT_CALL(hooks, SystemTimer()).Times(3).WillRepeatedly(Invoke([] {
        EXPECT_TRUE(get_interrupt_state());
    }));
    Process(1, [] { Tick(3); });
    RunOS();
}

TEST_F(SystemTimer, HookRunsBeforeTheTickIsCounted)
{
    static std::vector<unsigned> seen;
    EXPECT_CALL(hooks, SystemTimer()).Times(3).WillRepeatedly(Invoke([] { seen.push_back(Ticks()); }));
    Process(1, [] { Tick(3); });
    AtEnd([] { EXPECT_THAT(seen, ::testing::ElementsAre(0u, 1u, 2u)); });
    RunOS();
}

TEST_F(SystemTimer, HookCanWakeAProcess)
{
    // the hook wakes P1 on the 4th tick; P1 runs right after that interrupt
    EXPECT_CALL(hooks, SystemTimer()).WillRepeatedly(Invoke([] {
        if(Ticks() == 3)
            P(1).force_wake_up();
    }));
    Process(1, [] { OS::sleep(); EXPECT_EQ(Ticks(), 4u); });
    RunOS();
}

TEST_F(SystemTimer, TickInsideACriticalSectionIsTakenAtItsEnd)
{
    Process(0, [] {
        {
            TCritSect cs;
            Tick();
            EXPECT_EQ(Ticks(), 0u);                 // pending
        }
        EXPECT_EQ(Ticks(), 1u);
    });
    RunOS();
}

TEST_F(SystemTimer, LockedTimerDropsTicks)
{
    EXPECT_CALL(hooks, SystemTimer()).Times(1);
    Process(0, [] {
        lock_system_timer();
        Tick(5);
        EXPECT_EQ(Ticks(), 0u);
        unlock_system_timer();
        Tick(1);
        EXPECT_EQ(Ticks(), 1u);
    });
    RunOS();
}

TEST_F(SystemTimer, LockedTimerFreezesTimeouts)
{
    Process(0, [] {
        OS::sleep(3);
        EXPECT_EQ(Ticks(), 3u);
    });
    Process(1, [] {
        lock_system_timer();
        Tick(10);                                   // dropped: P0's timeout does not run
        EXPECT_TRUE(P(0).is_sleeping());
        unlock_system_timer();
    });
    RunOS();
}

TEST_F(SystemTimer, TimeoutsRunOnlyForBlockedProcesses)
{
    // every process that waits with a timeout loses one tick per interrupt
    Process(0, [] { OS::sleep(2); EXPECT_EQ(Ticks(), 2u); });
    Process(1, [] { OS::sleep(4); EXPECT_EQ(Ticks(), 4u); });
    Process(2, [] { Tick(6); EXPECT_EQ(Ticks(), 6u); });  // P2 never blocks while it ticks
    RunOS();
}

//==============================================================================
//
//    Interrupts
//
TEST_F(Interrupts, HandlerRunsAtOnceWhenEnabled)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("before"));
        EXPECT_CALL(irq, Handler(0));
        EXPECT_CALL(probe, Mark("after"));
    }
    Process(1, [] { Mark("before"); Raise(0); Mark("after"); });
    RunOS();
}

TEST_F(Interrupts, HandlerRunsWithInterruptsDisabled)
{
    EXPECT_CALL(irq, Handler(1)).WillOnce(Invoke([](int) { EXPECT_TRUE(get_interrupt_state()); }));
    Process(1, [] { Raise(1); EXPECT_FALSE(get_interrupt_state()); });
    RunOS();
}

TEST_F(Interrupts, HandlerWakingAHigherProcessSwitchesAtItsExit)
{
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0 waits"));
        EXPECT_CALL(probe, Mark("isr: signal"));
        EXPECT_CALL(irq, Handler(2));
        EXPECT_CALL(probe, Mark("P0 woken"));
        EXPECT_CALL(probe, Mark("P1 after the interrupt"));
    }
    Irq(2, [] { Mark("isr: signal"); flag.signal_isr(); });
    Process(0, [] { Mark("P0 waits"); EXPECT_TRUE(flag.wait()); Mark("P0 woken"); });
    Process(1, [] { Raise(2); Mark("P1 after the interrupt"); });
    RunOS();
}

TEST_F(Interrupts, HandlerWakingALowerProcessReturnsToTheInterruptedOne)
{
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(irq, Handler(2));
        EXPECT_CALL(probe, Mark("P1 after the interrupt"));
        EXPECT_CALL(probe, Mark("P3 woken"));
    }
    Irq(2, [] { flag.signal_isr(); });
    Process(3, [] { EXPECT_TRUE(flag.wait()); Mark("P3 woken"); });
    Process(1, [] { OS::sleep(1); Raise(2); Mark("P1 after the interrupt"); });
    RunOS();
}

TEST_F(Interrupts, SchedulingInsideAHandlerIsDeferredToItsExit)
{
    // a process service called inside TISRW does not switch: the handler ends first
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("isr: woke P0"));
        EXPECT_CALL(probe, Mark("isr: end"));
        EXPECT_CALL(irq, Handler(4));
        EXPECT_CALL(probe, Mark("P0"));
    }
    Irq(4, [] { P(0).force_wake_up(); Mark("isr: woke P0"); Mark("isr: end"); });
    Process(0, [] { OS::sleep(); Mark("P0"); });
    Process(2, [] { Raise(4); });
    RunOS();
}

TEST_F(Interrupts, NestedWrappersScheduleOnlyAtTheOutermostExit)
{
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("inner done"));
        EXPECT_CALL(probe, Mark("outer done"));
        EXPECT_CALL(probe, Mark("P0"));
    }
    RawIrq(5, [] {
        TISRW outer;
        {
            TISRW inner;
            flag.signal_isr();
        }
        Mark("inner done");                         // P0 must not have run yet
        Mark("outer done");
    });
    Process(0, [] { flag.wait(); Mark("P0"); });
    Process(1, [] { Raise(5); });
    RunOS();
}

TEST_F(Interrupts, RawHandlerWithoutWrapperDoesNotSchedule)
{
    // without TISRW the kernel is not told about the interrupt: a ready
    // higher process runs only at the next scheduling point
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P2 after the interrupt"));
        EXPECT_CALL(probe, Mark("P0"));
    }
    RawIrq(6, [] { flag.signal_isr(); });
    Process(0, [] { flag.wait(); Mark("P0"); });
    Process(2, [] { Raise(6); Mark("P2 after the interrupt"); OS::sleep(1); });
    RunOS();
}

TEST_F(Interrupts, InterruptRaisedInACriticalSectionIsTakenAtItsEnd)
{
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("inside"));
        EXPECT_CALL(irq, Handler(1));
        EXPECT_CALL(probe, Mark("after"));
    }
    Process(0, [] {
        {
            TCritSect cs;
            Raise(1);
            Mark("inside");
        }
        Mark("after");
    });
    RunOS();
}

TEST_F(Interrupts, SignalArrivingWhileTheWaiterBlocks)
{
    // Regression for the lost wake-up of scheme 1 (upstream aa4e99d): the
    // interrupt is pending while P1 enters wait(); it signals the flag during
    // the context switch away from P1. P1 must be ready again at once.
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(irq, Handler(0));
        EXPECT_CALL(probe, Mark("P1 got the flag"));
        EXPECT_CALL(probe, Mark("P2"));
    }
    Irq(0, [] { flag.signal_isr(); });
    Process(1, [] {
        bool r;
        {
            TCritSect cs;
            Raise(0);
            r = flag.wait();
        }
        EXPECT_TRUE(r);
        Mark("P1 got the flag");
    });
    Process(2, [] { Mark("P2"); });
    RunOS();
}

TEST_F(Interrupts, TimeoutExpiringWhileTheProcessBlocks)
{
    // the timer interrupt is pending while P1 goes to sleep(1): it expires
    // the timeout during the switch, P1 runs on before P2
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P1@1"));
        EXPECT_CALL(probe, Mark("P2"));
    }
    Process(1, [] {
        {
            TCritSect cs;
            Tick();
            OS::sleep(1);
        }
        Mark("P1@" + std::to_string(Ticks()));
    });
    Process(2, [] { Mark("P2"); });
    RunOS();
}

TEST_F(Interrupts, HandlerWakesAHigherProcessWhileAnotherBlocks)
{
    static TEventFlag flag;
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0"));
        EXPECT_CALL(probe, Mark("P2"));
        EXPECT_CALL(probe, Mark("P1@3"));
    }
    EXPECT_CALL(irq, Handler(0));
    Irq(0, [] { flag.signal_isr(); });
    Process(0, [] { flag.wait(); Mark("P0"); });
    Process(1, [] {
        {
            TCritSect cs;
            Raise(0);
            OS::sleep(3);                           // the switch to P2 is requested, then the interrupt wakes P0
        }
        Mark("P1@" + std::to_string(Ticks()));
    });
    Process(2, [] { Mark("P2"); });
    RunOS();
}

TEST_F(Interrupts, ContextSwitchHookRunsWithInterruptsDisabled)
{
    EXPECT_CALL(hooks, ContextSwitch()).Times(::testing::AtLeast(1)).WillRepeatedly(Invoke([] {
        EXPECT_TRUE(get_interrupt_state());
    }));
    static TEventFlag flag;
    EXPECT_CALL(irq, Handler(0));
    Irq(0, [] { flag.signal_isr(); });
    Process(0, [] { flag.wait(); });
    Process(1, [] { OS::sleep(2); Raise(0); });
    RunOS();
}

TEST_F(Interrupts, EveryInterruptIsCountedOnce)
{
    EXPECT_CALL(irq, Handler(7)).Times(100);
    Process(3, [] { for(int i = 0; i < 100; ++i) Raise(7); });
    RunOS();
}

//==============================================================================
//
//    Critical sections
//
TEST_F(CriticalSection, DisablesAndRestoresInterrupts)
{
    Process(0, [] {
        EXPECT_FALSE(get_interrupt_state());
        {
            TCritSect outer;
            EXPECT_TRUE(get_interrupt_state());
            {
                TCritSect inner;
                EXPECT_TRUE(get_interrupt_state());
            }
            EXPECT_TRUE(get_interrupt_state()) << "the inner section must restore, not enable";
        }
        EXPECT_FALSE(get_interrupt_state());
    });
    RunOS();
}

TEST_F(CriticalSection, SleepInsideACriticalSectionKeepsItAfterwards)
{
    // the state of the interrupts belongs to the process: P0 returns from
    // sleep() still inside its critical section, while P1 ran with interrupts on
    Process(0, [] {
        TCritSect cs;
        OS::sleep(2);
        EXPECT_TRUE(get_interrupt_state());
    });
    Process(1, [] { EXPECT_FALSE(get_interrupt_state()); });
    RunOS();
}

TEST_F(CriticalSection, WakeUpInsideACriticalSectionSwitchesAtOnce)
{
    // like sleep(), a wake-up of a higher process switches in the critical
    // section; the woken process runs with its own interrupt state
    {
        InSequence seq;
        EXPECT_CALL(probe, Mark("P0"));
        EXPECT_CALL(probe, Mark("P1"));
    }
    Process(0, [] { OS::sleep(); EXPECT_FALSE(get_interrupt_state()); Mark("P0"); });
    Process(1, [] {
        TCritSect cs;
        P(0).force_wake_up();
        EXPECT_TRUE(get_interrupt_state());
        Mark("P1");
    });
    RunOS();
}
