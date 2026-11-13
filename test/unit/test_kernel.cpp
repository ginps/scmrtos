//******************************************************************************
//*
//*     scmRTOS unit tests: kernel
//*
//*     Start-up, priority scheduling, sleep/wake-up, system timer,
//*     interrupts, process state queries, debug support, restart.
//*
//*     P0 has the highest priority. Expected traces are worked out by hand
//*     from the scheduling rules, tick by tick.
//*
//******************************************************************************
#include "utest.h"
#include <string.h>
#include <new>

using namespace OS;
#define T ut::trace

//==============================================================================
//
//    Start-up
//
UT_TEST(kernel, not_running_before_run)
{
    CHECK(!os_running());
}

// os_running() asked from a global constructor. This file is linked before the
// kernel, so the probe below is constructed before OS::Kernel is: the kernel's
// "not running yet" mark is not set and os_running() says "running".
namespace
{
    struct TEarlyProbe { bool running; TEarlyProbe() : running(os_running()) { } } EarlyProbe;
}

UT_TEST_DEFECT(kernel, not_running_in_global_constructors, "N9")
{
    CHECK(!EarlyProbe.running);
}

UT_TEST(kernel, running_inside_process)
{
    ut::proc(0, []{ CHECK(os_running()); });
}

UT_TEST(kernel, processes_start_in_priority_order)
{
    // each process runs until it returns (blocks forever in the framework)
    ut::proc(3, []{ T("3"); });
    ut::proc(0, []{ T("0"); });
    ut::proc(4, []{ T("4"); });
    ut::proc(2, []{ T("2"); });
    ut::proc(1, []{ T("1"); });
    ut::at_end([]{ CHECK_TRACE("0 1 2 3 4"); });
}

UT_TEST(kernel, tick_count_starts_at_zero)
{
    ut::proc(0, []{ CHECK_EQ(get_tick_count(), 0); });
}

UT_TEST(kernel, get_proc_and_priority)
{
    ut::proc(0, []{
        for(int i = 0; i < ut::PROC_COUNT; ++i)
        {
            CHECK(get_proc(ut::prio(i)) == &ut::P(i));
            CHECK_EQ(ut::P(i).priority(), ut::prio(i));
        }
        CHECK(get_proc(prIDLE) == &IdleProc);
        CHECK_EQ(IdleProc.priority(), prIDLE);
    });
}

#ifndef UT_V52
UT_TEST(kernel, process_constructed_with_function_runs_it)
{
    // P3 is created as process<pr3, ...>("P3", p3_entry): p3_entry runs instead of exec()
    ut::proc(3, []{ CHECK(ut::P3StartedViaFunction); });
}
#endif

UT_TEST_SUSPENDED(kernel, suspended_process_waits_for_start)
{
    // P4 is declared pssSuspended; it must not run before start()
    ut::proc(4, []{ T("P4@%u", ut::ticks()); }, ut::SUSPENDED);
    ut::proc(0, []{
        CHECK(ut::P(4).is_suspended());
        OS::sleep(5);
        T("start");
        ut::P(4).start();
    });
    ut::at_end([]{ CHECK_TRACE("start P4@5"); });
}

UT_TEST(kernel, start_of_lower_process_does_not_preempt)
{
    // P3 starts the suspended P4, which is lower: P3 keeps running
    ut::proc(4, []{ T("P4"); }, ut::SUSPENDED);
    ut::proc(3, []{ T("P3"); ut::P(4).start(); T("P3-end"); });
    ut::at_end([]{ CHECK_TRACE("P3 P3-end P4"); });   // P4 has lower priority than P3
}

//==============================================================================
//
//    Preemption and scheduling order
//
UT_TEST(kernel, higher_priority_preempts_on_timeout)
{
    ut::proc(0, []{ T("A0"); OS::sleep(2); T("A1@%u", ut::ticks()); });
    ut::proc(1, []{ T("B0"); OS::sleep(1); T("B1@%u", ut::ticks()); OS::sleep(5); T("B2@%u", ut::ticks()); });
    ut::at_end([]{ CHECK_TRACE("A0 B0 B1@1 A1@2 B2@6"); });
}

UT_TEST(kernel, same_tick_wakeups_run_by_priority)
{
    // P3 goes to sleep first, P0 last; all wake at tick 4
    ut::proc(0, []{ OS::sleep(1); OS::sleep(3); T("P0@%u", ut::ticks()); });
    ut::proc(1, []{ OS::sleep(4); T("P1@%u", ut::ticks()); });
    ut::proc(3, []{ OS::sleep(4); T("P3@%u", ut::ticks()); });
    ut::at_end([]{ CHECK_TRACE("P0@4 P1@4 P3@4"); });
}

UT_TEST(kernel, lower_priority_does_not_preempt)
{
    ut::proc(0, []{
        T("H0");
        ut::P(2).force_wake_up();           // P2 is not even blocked: nothing happens
        ut::tick(3);                        // P2's timeout expires, P0 keeps running
        T("H1");
        OS::sleep(1);
        T("H2");
    });
    ut::proc(2, []{ T("L0"); OS::sleep(2); T("L1@%u", ut::ticks()); });
    // P2 starts only when P0 sleeps at tick 3, sleeps until 5; P0 wakes at 4
    ut::at_end([]{ CHECK_TRACE("H0 H1 L0 H2 L1@5"); });
}

UT_TEST(kernel, ready_lower_process_runs_when_higher_blocks)
{
    ut::proc(0, []{ for(int i = 0; i < 3; ++i) { T("H%d", i); OS::sleep(1); } });
    ut::proc(4, []{ for(int i = 0; i < 3; ++i) { T("L%d@%u", i, ut::ticks()); ut::tick(); } });
    // L ticks the timer itself: each tick wakes H, which preempts L at once
    ut::at_end([]{ CHECK_TRACE("H0 L0@0 H1 L1@1 H2 L2@2"); });
}

UT_TEST(kernel, many_switches_round_trip)
{
    static TEventFlag Ping, Pong;
    static int Count;
    ut::proc(0, []{ for(int i = 0; i < 1000; ++i) { Ping.wait(); ++Count; Pong.signal(); } });
    ut::proc(1, []{ for(int i = 0; i < 1000; ++i) { Ping.signal(); Pong.wait(); } });
    ut::at_end([]{ CHECK_EQ(Count, 1000); CHECK_EQ(ut::ticks(), 0); });
}

//==============================================================================
//
//    OS::sleep() / wake_up() / force_wake_up()
//
UT_TEST(kernel, sleep_lasts_exact_ticks)
{
    ut::max_ticks(70000);
    ut::proc(0, []{
        static const timeout_t T_[] = { 1, 2, 3, 5, 10, 100, 1000, 65535 };
        for(unsigned i = 0; i < sizeof(T_)/sizeof(T_[0]); ++i)
        {
            unsigned t0 = ut::ticks();
            OS::sleep(T_[i]);
            CHECK_EQ(ut::ticks() - t0, T_[i]);
        }
    });
}

UT_TEST(kernel, sleep_zero_means_forever)
{
    ut::max_ticks(500);
    ut::proc(0, []{ OS::sleep(0); T("woke@%u", ut::ticks()); });
    ut::proc(1, []{
        OS::sleep(300);
        CHECK(!ut::done(0));
        CHECK(!ut::P(0).is_sleeping());     // waiting without timeout
        CHECK(ut::P(0).is_suspended());
        ut::P(0).force_wake_up();
    });
    ut::at_end([]{ CHECK_TRACE("woke@300"); });
}

UT_TEST(kernel, wake_up_ends_timed_sleep)
{
    ut::proc(0, []{ OS::sleep(100); T("P0@%u", ut::ticks()); });
    ut::proc(1, []{
        OS::sleep(3);
        T("wake");
        ut::P(0).wake_up();                 // P0 preempts right here
        T("after");
    });
    ut::at_end([]{ CHECK_TRACE("wake P0@3 after"); });
}

UT_TEST(kernel, wake_up_ignores_untimed_sleep)
{
    ut::max_ticks(100);
    ut::proc(0, []{ OS::sleep(); T("P0@%u", ut::ticks()); });
    ut::proc(1, []{
        ut::P(0).wake_up();                 // Timeout == 0: no effect
        OS::sleep(10);
        CHECK(!ut::done(0));
        ut::P(0).force_wake_up();
    });
    ut::at_end([]{ CHECK_TRACE("P0@10"); });
}

UT_TEST(kernel, wake_up_of_running_process_is_harmless)
{
    ut::proc(0, []{
        ut::P(0).wake_up();
        ut::P(0).force_wake_up();
        T("self");
        OS::sleep(2);
        T("P0@%u", ut::ticks());
    });
    ut::at_end([]{ CHECK_TRACE("self P0@2"); });
}

UT_TEST(kernel, force_wake_up_ends_timed_sleep)
{
    ut::proc(0, []{ OS::sleep(50); T("P0@%u", ut::ticks()); });
    ut::proc(2, []{ OS::sleep(7); ut::P(0).force_wake_up(); T("P2"); });
    ut::at_end([]{ CHECK_TRACE("P0@7 P2"); });
}

UT_TEST(kernel, woken_process_is_not_sleeping)
{
    // wake_up() and force_wake_up() cancel the rest of the timeout
    static TMutex Block;
    ut::proc(0, []{ OS::sleep(100); Block.lock(); Block.unlock(); OS::sleep(100); Block.lock(); Block.unlock(); });
    ut::proc(1, []{
        Block.lock();
        OS::sleep(3);
        CHECK(ut::P(0).is_sleeping());
        ut::P(0).wake_up();                 // P0 runs and blocks on the mutex
        CHECK(!ut::P(0).is_sleeping());
        Block.unlock();                     // P0 sleeps again
        Block.lock();
        CHECK(ut::P(0).is_sleeping());
        ut::P(0).force_wake_up();
        CHECK(!ut::P(0).is_sleeping());
        Block.unlock();
    });
}

UT_TEST(kernel, critical_section_survives_blocking_call)
{
    // a process that blocks inside its critical section gets it back intact
    // when it is switched in again (the interrupt state is part of the context)
    ut::proc(0, []{
        TCritSect cs;
        OS::sleep(2);                       // switched out and in again
        unsigned t = ut::ticks();
        ut::tick();
        CHECK_EQ(ut::ticks(), t);           // still inside the critical section: the tick is pending
    });
    ut::proc(1, []{
        static TEventFlag F;
        TCritSect cs;
        F.wait(1);
        unsigned t = ut::ticks();
        ut::tick();
        CHECK_EQ(ut::ticks(), t);
    });
}

UT_TEST(kernel, is_sleeping_and_is_suspended)
{
    static TEventFlag Flag;
    ut::proc(0, []{ OS::sleep(10); });
    ut::proc(1, []{ Flag.wait(); });
    ut::proc(2, []{
        CHECK(ut::P(0).is_sleeping());      CHECK(ut::P(0).is_suspended());
        CHECK(!ut::P(1).is_sleeping());     CHECK(ut::P(1).is_suspended());
        CHECK(!ut::P(2).is_sleeping());     CHECK(!ut::P(2).is_suspended());
        CHECK(!ut::P(3).is_sleeping());     CHECK(!ut::P(3).is_suspended());  // ready, not started yet
        Flag.signal();
        OS::sleep(20);
        CHECK(!ut::P(0).is_sleeping());
    });
}

//==============================================================================
//
//    System timer
//
UT_TEST(kernel, tick_from_process_advances_time)
{
    ut::proc(0, []{
        ut::tick(5);
        CHECK_EQ(ut::ticks(), 5);
    });
}

UT_TEST(kernel, lock_system_timer_drops_ticks)
{
    ut::proc(0, []{
        lock_system_timer();
        ut::tick(3);
        CHECK_EQ(ut::ticks(), 0);
        CHECK(!ut::done(1));                // P1 has not run yet anyway
        unlock_system_timer();
        ut::tick(2);
        CHECK_EQ(ut::ticks(), 2);
    });
    ut::proc(1, []{ T("P1@%u", ut::ticks()); });
    ut::at_end([]{ CHECK_TRACE("P1@2"); });
}

UT_TEST(kernel, locked_timer_does_not_expire_timeouts)
{
    ut::proc(0, []{ OS::sleep(2); T("P0@%u", ut::ticks()); });
    ut::proc(1, []{
        lock_system_timer();
        ut::tick(10);
        T("unlock");
        unlock_system_timer();
        ut::tick(2);
        T("P1@%u", ut::ticks());
    });
    ut::at_end([]{ CHECK_TRACE("unlock P0@2 P1@2"); });
}

UT_TEST(kernel, systimer_hook_called_every_tick)
{
    static unsigned Hooks;
    ut::on_systimer([]{ ++Hooks; });
    ut::proc(0, []{ OS::sleep(25); ut::tick(5); });
    ut::at_end([]{ CHECK_EQ(Hooks, 30); CHECK_EQ(ut::ticks(), 30); });
}

UT_TEST(kernel, tick_inside_critical_section_is_deferred)
{
    ut::proc(0, []{
        {
            TCritSect cs;
            ut::tick();
            CHECK_EQ(ut::ticks(), 0);       // the timer interrupt is pending
        }
        CHECK_EQ(ut::ticks(), 1);           // taken at the end of the critical section
    });
}

UT_TEST(kernel, nested_critical_sections)
{
    ut::proc(0, []{
        {
            TCritSect a;
            {
                TCritSect b;
                ut::tick();
            }
            CHECK_EQ(ut::ticks(), 0);       // still inside the outer section
        }
        CHECK_EQ(ut::ticks(), 1);
    });
}

//==============================================================================
//
//    Interrupts
//
UT_TEST(kernel, isr_wakeup_preempts_at_isr_exit)
{
    static TEventFlag Flag;
    ut::irq(0, []{ TISRW isr; T("isr"); Flag.signal_isr(); T("isr-end"); });
    ut::proc(1, []{ Flag.wait(); T("P1"); });
    ut::proc(2, []{ T("P2"); ut::raise(0); T("P2-after"); });
    ut::at_end([]{ CHECK_TRACE("P2 isr isr-end P1 P2-after"); });
}

UT_TEST(kernel, isr_wakeup_of_lower_process_does_not_switch)
{
    static TEventFlag Flag;
    ut::irq(0, []{ TISRW isr; Flag.signal_isr(); });
    ut::proc(3, []{ Flag.wait(); T("P3"); });
    ut::proc(1, []{ OS::sleep(1); T("P1"); ut::raise(0); T("P1-after"); });
    ut::at_end([]{ CHECK_TRACE("P1 P1-after P3"); });
}

UT_TEST(kernel, nested_isr_wrappers_switch_at_outer_exit)
{
    static TEventFlag Flag;
    ut::irq(0, []{
        TISRW outer;
        {
            TISRW inner;
            Flag.signal_isr();
        }
        T("after-inner");                   // no switch at the inner exit
    });
    ut::proc(0, []{ Flag.wait(); T("P0"); });
    ut::proc(1, []{ ut::raise(0); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("after-inner P0 P1"); });
}

UT_TEST(kernel, non_isr_service_inside_isr_defers_switch)
{
    // force_wake_up() and signal() are not _isr functions; inside TISRW the
    // scheduler must not switch until the interrupt exits
    static TEventFlag Flag;
    ut::irq(0, []{ TISRW isr; ut::P(0).force_wake_up(); T("h1"); Flag.signal(); T("h2"); });
    ut::proc(0, []{ OS::sleep(); T("P0"); });
    ut::proc(1, []{ Flag.wait(); T("P1"); });
    ut::proc(2, []{ ut::raise(0); T("P2"); });
    ut::at_end([]{ CHECK_TRACE("h1 h2 P0 P1 P2"); });
}

UT_TEST(kernel, isr_inside_critical_section_is_deferred)
{
    static TEventFlag Flag;
    ut::irq(0, []{ TISRW isr; T("isr"); Flag.signal_isr(); });
    ut::proc(0, []{ Flag.wait(); T("P0"); });
    ut::proc(1, []{
        {
            TCritSect cs;
            ut::raise(0);
            T("in-cs");
        }
        T("P1");
    });
    ut::at_end([]{ CHECK_TRACE("in-cs isr P0 P1"); });
}

UT_TEST(kernel, several_interrupts)
{
    static TEventFlag F0, F1;
    ut::irq(0, []{ TISRW isr; F0.signal_isr(); });
    ut::irq(7, []{ TISRW isr; F1.signal_isr(); });
    ut::proc(0, []{ F1.wait(); T("P0"); });
    ut::proc(1, []{ F0.wait(); T("P1"); });
    ut::proc(2, []{ ut::raise(0); T("x"); ut::raise(7); T("y"); });
    ut::at_end([]{ CHECK_TRACE("P1 x P0 y"); });
}

//------------------------------------------------------------------------------
//  An interrupt taken while a context switch is already requested.
//
//  A process blocks: the scheduler picks the next process and requests the
//  switch. With the software interrupt scheme the switch itself happens a moment
//  later, when the interrupts are enabled, and an interrupt that is pending at
//  that moment runs first. It may make the blocking process ready again, or wake
//  a higher one; the requested switch must follow.
//  (The first case is the lost wake-up fixed upstream in aa4e99d.)
//
UT_TEST(kernel, isr_signal_arriving_while_the_waiter_blocks)
{
    static TEventFlag Flag;
    ut::irq(0, []{ TISRW isr; Flag.signal_isr(); });
    ut::proc(1, []{
        bool r;
        {
            TCritSect cs;
            ut::raise(0);                   // pending until the interrupts are enabled
            r = Flag.wait();
        }
        T("P1:%d", r);
    });
    ut::proc(2, []{ T("P2"); });
    // P1 is ready again before anybody else runs: it must go on first
    ut::at_end([]{ CHECK_TRACE("P1:1 P2"); });
}

UT_TEST(kernel, timeout_expiring_while_the_process_blocks)
{
    ut::proc(1, []{
        {
            TCritSect cs;
            ut::tick();                     // the timer interrupt is pending
            OS::sleep(1);                   // expires in that very interrupt
        }
        T("P1@%u", ut::ticks());
    });
    ut::proc(2, []{ T("P2"); });
    ut::at_end([]{ CHECK_TRACE("P1@1 P2"); });
}

UT_TEST(kernel, isr_wakes_higher_process_while_another_blocks)
{
    static TEventFlag Flag;
    ut::irq(0, []{ TISRW isr; Flag.signal_isr(); });
    ut::proc(0, []{ Flag.wait(); T("P0"); });
    ut::proc(1, []{
        {
            TCritSect cs;
            ut::raise(0);
            OS::sleep(3);                   // the switch to P2 is requested, then the interrupt wakes P0
        }
        T("P1@%u", ut::ticks());
    });
    ut::proc(2, []{ T("P2"); });
    ut::at_end([]{ CHECK_TRACE("P0 P2 P1@3"); });
}

UT_TEST(kernel, two_interrupts_pending_at_once_both_run_before_the_switch)
{
    static TEventFlag F0, F1;
    ut::irq(0, []{ TISRW isr; T("i0"); F0.signal_isr(); });
    ut::irq(1, []{ TISRW isr; T("i1"); F1.signal_isr(); });
    ut::proc(0, []{ F1.wait(); T("P0"); });
    ut::proc(1, []{ F0.wait(); T("P1"); });
    ut::proc(2, []{
        {
            TCritSect cs;
            ut::raise(1);
            ut::raise(0);
        }
        T("P2");
    });
    // both handlers run back to back, then the highest woken process
    ut::at_end([]{ CHECK_TRACE("i0 i1 P0 P1 P2"); });
}

//==============================================================================
//
//    Context switch hook
//
UT_TEST(kernel, context_switch_counted_once_per_switch)
{
    static TEventFlag Flag;
    ut::proc(0, []{ for(;;) { Flag.wait(); T("P0"); } }, ut::DAEMON);
    ut::proc(3, []{ for(;;) { Flag.wait(); T("P3"); } }, ut::DAEMON);
    ut::proc(1, []{
        OS::sleep(1);                           // let every process start and block first
        unsigned s = ut::switches();
        Flag.signal();                      // P0 runs and blocks again: 2 switches; P3 is lower
        CHECK_EQ(ut::switches() - s, 2);
        s = ut::switches();
        T("x");
        OS::sleep(1);                           // to P3, which blocks, to idle, tick, back to P1
        CHECK_EQ(ut::switches() - s, 3);
    });
    ut::at_end([]{ CHECK_TRACE("P0 x P3"); });
}

UT_TEST(kernel, no_switch_when_nothing_changes)
{
    ut::proc(0, []{
        unsigned s = ut::switches();
        ut::P(3).force_wake_up();           // lower and already ready
        ut::tick(3);                        // nobody times out
        CHECK_EQ(ut::switches(), s);
    });
}

//==============================================================================
//
//    Debug support (scmRTOS_DEBUG_ENABLE == 1)
//
#if scmRTOS_DEBUG_ENABLE == 1

static NOINLINE size_t use_stack(int depth)
{
    volatile char buf[1024];
    for(size_t i = 0; i < sizeof(buf); ++i) buf[i] = char(i);
    return depth ? use_stack(depth - 1) + buf[depth] : buf[0];
}

UT_TEST(kernel, debug_names_and_stack_size)
{
    ut::proc(0, []{
        CHECK(!strcmp(ut::P(0).name(), "P0"));
        CHECK(!strcmp(ut::P(4).name(), "P4"));
        CHECK(!strcmp(IdleProc.name(), "Idle"));
        CHECK_EQ(ut::P(0).stack_size(), 64*1024 / sizeof(stack_item_t));
    });
}

UT_TEST(kernel, debug_stack_slack_shrinks_with_use)
{
    ut::proc(0, []{
        size_t before = ut::P(0).stack_slack();
        CHECK(before > 0 && before < ut::P(0).stack_size());
        use_stack(20);                      // ~20 KiB deeper
        size_t after = ut::P(0).stack_slack();
        CHECK(after + 20*1024/sizeof(stack_item_t) <= before);
        CHECK(ut::P(3).stack_slack() > after);   // P3 has not run: almost untouched
    });
}

namespace
{
    // read access to the protected stack pool pointer, for the test only
    struct TStackAccess : public TBaseProcess
    {
        static const stack_item_t * pool(const TBaseProcess & p) { return p.*(&TStackAccess::StackPool); }
    };
}

UT_TEST(kernel, debug_stack_slack_is_exact)
{
    // slack = number of untouched pattern words at the bottom of the stack
    ut::proc(0, []{
        use_stack(8);
        for(int i = 0; i < 4; ++i)
        {
            const stack_item_t * pool = TStackAccess::pool(ut::P(i));
            size_t slack = ut::P(i).stack_slack();
            CHECK(slack > 0 && slack < ut::P(i).stack_size());
            CHECK(pool[slack - 1] == scmRTOS_STACK_PATTERN);
            CHECK(pool[slack] != scmRTOS_STACK_PATTERN);
        }
    });
}

UT_TEST(kernel, debug_waiting_for)
{
    static TMutex M;
    ut::proc(0, []{ OS::sleep(1); M.lock(); M.unlock(); });
    ut::proc(1, []{
        M.lock();
        OS::sleep(1);                           // P0 blocks on M
        CHECK((void*)ut::P(0).waiting_for() == (void*)&M);
        CHECK(ut::P(1).waiting_for() == 0);
        M.unlock();                         // P0 runs, finishes
        CHECK(ut::P(0).waiting_for() == 0);
    });
}

#endif // scmRTOS_DEBUG_ENABLE

//==============================================================================
//
//    Process restart (scmRTOS_PROCESS_RESTART_ENABLE == 1)
//
#if scmRTOS_PROCESS_RESTART_ENABLE == 1

UT_TEST(kernel, restart_sleeping_process)
{
    static int Runs;
    ut::proc(1, []{ ++Runs; T("P1-run%d", Runs); OS::sleep(100); T("P1-woke"); }, ut::DAEMON);
    ut::proc(0, []{
        OS::sleep(5);
        ut::terminate(1);                   // P1 restarts from the beginning, stays stopped
        CHECK(ut::P(1).is_suspended());
        CHECK(!ut::P(1).is_sleeping());
        OS::sleep(200);                         // its old timeout must not wake it
        CHECK_EQ(Runs, 1);
        ut::P(1).start();
        T("started");
        OS::sleep(1);                           // let P1 run before the test ends
    });
    ut::max_ticks(400);
    ut::at_end([]{ CHECK_TRACE("P1-run1 started P1-run2"); CHECK_EQ(Runs, 2); });
}

UT_TEST(kernel, restart_ready_process_stops_it)
{
    // P3 is ready but has not run yet: terminate() must take it out of the ready map
    static int Runs;
    ut::proc(3, []{ ++Runs; }, ut::DAEMON);
    ut::proc(0, []{
        ut::terminate(3);
        CHECK(ut::P(3).is_suspended());
        OS::sleep(5);
        CHECK_EQ(Runs, 0);
        ut::P(3).start();
        OS::sleep(1);
        CHECK_EQ(Runs, 1);
    });
}

#if scmRTOS_DEBUG_ENABLE == 1
UT_TEST(kernel, restart_clears_waiting_for)
{
    static TMutex M;
    ut::proc(1, []{ M.lock(); M.unlock(); }, ut::DAEMON);
    ut::proc(0, []{
        M.lock();
        OS::sleep(1);
        CHECK((void*)ut::P(1).waiting_for() == (void*)&M);
        ut::terminate(1);
        CHECK(ut::P(1).waiting_for() == 0);
        M.unlock();
    });
}
#endif

//------------------------------------------------------------------------------
//  The kernel remembers which waiting map a blocked process is in, so that
//  terminate() can take the process out of it. That note must not outlive the
//  wait. The mutex below lives in a buffer; after it is destroyed the buffer
//  is filled with ones, and a restart of the process must leave it alone.
//
namespace
{
    alignas(TMutex) unsigned char MutexBuf[sizeof(TMutex)];
    TMutex * BufMutex;

    void make_buf_mutex()   { BufMutex = new(MutexBuf) TMutex; }
    void scrap_buf_mutex()  { BufMutex->~TMutex(); memset(MutexBuf, 0xFF, sizeof(MutexBuf)); }
    bool buf_untouched()
    {
        for(size_t i = 0; i < sizeof(MutexBuf); ++i)
            if(MutexBuf[i] != 0xFF) return false;
        return true;
    }
}

UT_TEST(kernel, restart_leaves_alone_a_service_the_process_finished_waiting_for)
{
    make_buf_mutex();
    ut::proc(1, []{ BufMutex->lock(); BufMutex->unlock(); T("P1-got-it"); OS::sleep(); }, ut::DAEMON);
    ut::proc(0, []{
        BufMutex->lock();
        OS::sleep(1);                       // P1 blocks on the mutex
        BufMutex->unlock();
        OS::sleep(1);                       // P1 gets it, releases it, sleeps
        CHECK_TRACE("P1-got-it");
        scrap_buf_mutex();
        ut::terminate(1);                   // P1 waits for nothing now
        CHECK(buf_untouched());
    });
}

UT_TEST(kernel, second_restart_leaves_alone_the_service_of_the_first)
{
    make_buf_mutex();
    ut::proc(1, []{ BufMutex->lock(); T("unreachable"); }, ut::DAEMON);
    ut::proc(0, []{
        BufMutex->lock();
        OS::sleep(1);                       // P1 blocks on the mutex
        ut::terminate(1);                   // takes P1 out of the mutex's waiting map
        BufMutex->unlock();
        CHECK(!BufMutex->is_locked());
        scrap_buf_mutex();
        ut::terminate(1);                   // P1 is stopped and waits for nothing
        CHECK(buf_untouched());
        CHECK_TRACE("");
    });
}

UT_TEST(kernel, restart_process_waiting_for_mutex)
{
    static TMutex M;
    static int Runs;
    ut::proc(1, []{ ++Runs; T("P1-run%d", Runs); M.lock(); T("P1-locked"); M.unlock(); }, ut::DAEMON);
    ut::proc(0, []{
        M.lock();
        OS::sleep(1);                           // P1 blocks on M
        ut::terminate(1);                   // P1 leaves the mutex's waiting map
        M.unlock();                         // nobody is woken
        CHECK(!M.is_locked());
        CHECK_EQ(Runs, 1);
        OS::sleep(1);
        CHECK_EQ(Runs, 1);
        ut::P(1).start();                   // runs again, locks the free mutex
        OS::sleep(1);
        CHECK_EQ(Runs, 2);
    });
    ut::at_end([]{ CHECK_TRACE("P1-run1 P1-run2 P1-locked"); });
}

#ifndef UT_V52     // terminate(func) is new in develop
UT_TEST(kernel, restart_with_another_function)
{
    ut::proc(2, []{ T("old"); OS::sleep(); }, ut::DAEMON);
    ut::proc(0, []{
        OS::sleep(1);
        ut::terminate(2, []{ T("new"); for(;;) OS::sleep(); });
        ut::P(2).start();
        OS::sleep(1);
    });
    ut::at_end([]{ CHECK_TRACE("old new"); });
}
#endif

#endif // scmRTOS_PROCESS_RESTART_ENABLE
