//******************************************************************************
//*
//*     scmRTOS unit tests: core services
//*
//*     TEventFlag, TMutex / TMutexLocker, channel<T, Size, S>, message<T>
//*
//*     P0 has the highest priority. Expected traces are worked out by hand.
//*
//******************************************************************************
#include "utest.h"

using namespace OS;
#define T ut::trace

//==============================================================================
//
//    TEventFlag
//
UT_TEST(event_flag, initial_state)
{
    static TEventFlag Off, On(TEventFlag::efOn);
    CHECK(!Off.is_signaled());
    CHECK(On.is_signaled());
}

UT_TEST(event_flag, signal_without_waiter_latches)
{
    static TEventFlag F;
    ut::proc(0, []{
        F.signal();
        CHECK(F.is_signaled());
        unsigned s = ut::switches();
        CHECK(F.wait());                    // returns at once and clears the flag
        CHECK_EQ(ut::switches(), s);
        CHECK(!F.is_signaled());
    });
}

UT_TEST(event_flag, initially_on_flag_satisfies_first_wait)
{
    static TEventFlag F(TEventFlag::efOn);
    ut::proc(0, []{ CHECK(F.wait(1)); CHECK(!F.wait(3)); CHECK_EQ(ut::ticks(), 3); });
}

UT_TEST(event_flag, repeated_signals_collapse)
{
    static TEventFlag F;
    ut::proc(0, []{
        F.signal(); F.signal(); F.signal();
        CHECK(F.wait(1));
        CHECK(!F.wait(2));                  // only one event was latched
    });
}

UT_TEST(event_flag, wait_blocks_until_signal)
{
    static TEventFlag F;
    ut::proc(0, []{ bool r = F.wait(); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(3); T("signal"); F.signal(); T("P1"); });
    ut::at_end([]{
        CHECK_TRACE("signal P0:1@3 P1");
        CHECK(!F.is_signaled());            // consumed by the waiter, not latched
    });
}

UT_TEST(event_flag, signal_wakes_all_waiters)
{
    static TEventFlag F;
    ut::proc(0, []{ F.wait(); T("P0"); });
    ut::proc(1, []{ F.wait(); T("P1"); });
    ut::proc(2, []{ F.wait(); T("P2"); });
    ut::proc(3, []{ T("sig"); F.signal(); T("P3"); });
    ut::at_end([]{ CHECK_TRACE("sig P0 P1 P2 P3"); CHECK(!F.is_signaled()); });
}

UT_TEST(event_flag, signal_from_higher_process_does_not_switch)
{
    static TEventFlag F;
    ut::proc(2, []{ F.wait(); T("P2"); });
    ut::proc(0, []{ OS::sleep(1); F.signal(); T("P0"); });
    ut::at_end([]{ CHECK_TRACE("P0 P2"); });
}

UT_TEST(event_flag, wait_timeout)
{
    static TEventFlag F;
    ut::proc(0, []{
        static const timeout_t T_[] = { 1, 2, 7, 100 };
        for(unsigned i = 0; i < 4; ++i)
        {
            unsigned t0 = ut::ticks();
            CHECK(!F.wait(T_[i]));
            CHECK_EQ(ut::ticks() - t0, T_[i]);
        }
    });
}

UT_TEST(event_flag, signal_one_tick_before_timeout)
{
    static TEventFlag F;
    ut::proc(0, []{ bool r = F.wait(5); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(4); F.signal(); });
    ut::at_end([]{ CHECK_TRACE("P0:1@4"); });
}

UT_TEST(event_flag, signal_after_timeout_is_latched)
{
    static TEventFlag F;
    ut::proc(0, []{ CHECK(!F.wait(3)); OS::sleep(5); });
    ut::proc(1, []{ OS::sleep(4); F.signal(); CHECK(F.is_signaled()); });
}

UT_TEST(event_flag, wait_forever_does_not_time_out)
{
    static TEventFlag F;
    ut::max_ticks(3000);
    ut::proc(0, []{ CHECK(F.wait()); CHECK_EQ(ut::ticks(), 2500); });
    ut::proc(1, []{ OS::sleep(2500); F.signal(); });
}

UT_TEST(event_flag, clear_removes_latched_event)
{
    static TEventFlag F;
    ut::proc(0, []{ F.signal(); F.clear(); CHECK(!F.is_signaled()); CHECK(!F.wait(3)); });
}

UT_TEST(event_flag, signal_isr_wakes_waiter)
{
    static TEventFlag F;
    ut::irq(0, []{ TISRW isr; F.signal_isr(); T("isr"); });
    ut::proc(0, []{ F.wait(); T("P0"); });
    ut::proc(1, []{ ut::raise(0); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("isr P0 P1"); });
}

UT_TEST(event_flag, signal_isr_without_waiter_latches)
{
    static TEventFlag F;
    ut::irq(0, []{ TISRW isr; F.signal_isr(); });
    ut::proc(0, []{ ut::raise(0); CHECK(F.is_signaled()); CHECK(F.wait(1)); });
}

UT_TEST(event_flag, wake_up_aborts_timed_wait)
{
    static TEventFlag F;
    ut::proc(0, []{ bool r = F.wait(100); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(5); ut::P(0).wake_up(); });
    ut::at_end([]{ CHECK_TRACE("P0:0@5"); });
}

UT_TEST(event_flag, force_wake_up_aborts_untimed_wait)
{
    static TEventFlag F;
    ut::proc(0, []{ bool r = F.wait(); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(5); ut::P(0).force_wake_up(); OS::sleep(1); F.signal(); });
    ut::at_end([]{
        CHECK_TRACE("P0:0@5");
        CHECK(F.is_signaled());             // nobody waits any more: the later signal latches
    });
}

UT_TEST(event_flag, timed_out_waiter_is_not_woken_later)
{
    static TEventFlag F;
    static int Wakes;
    ut::proc(0, []{ CHECK(!F.wait(2)); ++Wakes; OS::sleep(10); });
    ut::proc(1, []{ OS::sleep(5); F.signal(); CHECK_EQ(Wakes, 1); });
    ut::at_end([]{ CHECK(F.is_signaled()); });
}

//==============================================================================
//
//    TMutex
//
UT_TEST(mutex, lock_unlock)
{
    static TMutex M;
    ut::proc(0, []{
        CHECK(!M.is_locked());
        M.lock();
        CHECK(M.is_locked());
        M.unlock();
        CHECK(!M.is_locked());
    });
}

UT_TEST(mutex, contended_lock_blocks_until_unlock)
{
    static TMutex M;
    ut::proc(0, []{ OS::sleep(1); T("P0-lock"); M.lock(); T("P0-got@%u", ut::ticks()); M.unlock(); });
    ut::proc(1, []{ M.lock(); OS::sleep(5); T("P1-unlock"); M.unlock(); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("P0-lock P1-unlock P0-got@5 P1"); });
}

UT_TEST(mutex, unlock_hands_over_to_highest_waiter)
{
    static TMutex M;
    ut::proc(4, []{ M.lock(); OS::sleep(3); T("free"); M.unlock(); });
    ut::proc(3, []{ OS::sleep(1); M.lock(); T("P3"); M.unlock(); });
    ut::proc(1, []{ OS::sleep(1); M.lock(); T("P1"); M.unlock(); });
    ut::proc(2, []{ OS::sleep(2); M.lock(); T("P2"); M.unlock(); });
    // P4 starts last: P0..P3 sleep first. P4 locks, the others queue up by tick 2
    ut::at_end([]{ CHECK_TRACE("free P1 P2 P3"); });
}

UT_TEST(mutex, unlock_by_non_owner_is_ignored)
{
    static TMutex M;
    ut::proc(0, []{
        OS::sleep(1);
        CHECK(M.is_locked());
        M.unlock();                         // P0 does not own it
        CHECK(M.is_locked());
    });
    ut::proc(1, []{ M.lock(); OS::sleep(2); CHECK(M.is_locked()); M.unlock(); CHECK(!M.is_locked()); });
}

UT_TEST(mutex, unlock_of_free_mutex_is_harmless)
{
    static TMutex M;
    ut::proc(0, []{ M.unlock(); CHECK(!M.is_locked()); M.lock(); CHECK(M.is_locked()); M.unlock(); });
}

UT_TEST(mutex, try_lock_free_and_taken)
{
    static TMutex M;
    ut::proc(0, []{
        OS::sleep(1);
        unsigned s = ut::switches();
        CHECK(!M.try_lock());               // taken by P1: fails without blocking
        CHECK_EQ(ut::switches(), s);
        OS::sleep(2);
        CHECK(M.try_lock());                // free now
        CHECK(M.is_locked());
        M.unlock();
    });
    ut::proc(1, []{ CHECK(M.try_lock()); OS::sleep(2); M.unlock(); });
}

UT_TEST(mutex, try_lock_timeout_fails_after_timeout)
{
    static TMutex M;
    ut::proc(0, []{ OS::sleep(1); unsigned t0 = ut::ticks(); CHECK(!M.try_lock(4)); CHECK_EQ(ut::ticks() - t0, 4); });
    ut::proc(1, []{ M.lock(); OS::sleep(10); M.unlock(); });
}

UT_TEST(mutex, try_lock_timeout_succeeds_when_released)
{
    static TMutex M;
    ut::proc(0, []{ OS::sleep(1); bool r = M.try_lock(10); T("P0:%d@%u", r, ut::ticks()); if(r) M.unlock(); });
    ut::proc(1, []{ M.lock(); OS::sleep(3); M.unlock(); });
    ut::at_end([]{ CHECK_TRACE("P0:1@3"); CHECK(!M.is_locked()); });
}

UT_TEST(mutex, try_lock_timeout_on_free_mutex)
{
    static TMutex M;
    ut::proc(0, []{ CHECK(M.try_lock(5)); CHECK_EQ(ut::ticks(), 0); M.unlock(); });
}

UT_TEST_DEFECT(mutex, try_lock_timeout_is_total_across_handovers, "6")
{
    // P1 waits with try_lock(5) from tick 0; at tick 3 the owner P0 unlocks
    // and immediately relocks before P1 runs. P1 must give up at tick 5.
    static TMutex M;
    ut::proc(0, []{ M.lock(); OS::sleep(3); M.unlock(); M.lock(); OS::sleep(20); M.unlock(); });
    ut::proc(1, []{ bool r = M.try_lock(5); T("P1:%d@%u", r, ut::ticks()); });
    ut::at_end([]{ CHECK_TRACE("P1:0@5"); });
}

UT_TEST_DEFECT(mutex, forced_wakeup_leaves_no_stale_waiter, "N7")
{
    // P1 waits in lock(). The owner P0 wakes it by force and then unlocks:
    // P1 is already ready, so unlock() does not take it out of the mutex's
    // waiting map, and lock() does not either. P1 gets the mutex, releases it
    // and goes to sleep for 50 ticks - and the next unlock() of that mutex by
    // anybody "resumes" it out of that sleep.
    static TMutex M;
    ut::proc(0, []{
        M.lock();
        OS::sleep(1);                       // P1 blocks on M
        ut::P(1).force_wake_up();
        M.unlock();
        OS::sleep(5);
        M.lock(); M.unlock();               // tick 6: must not affect P1
        OS::sleep(100);
    });
    ut::proc(1, []{
        M.lock(); M.unlock();
        unsigned t0 = ut::ticks();
        OS::sleep(50);
        T("P1-slept%u", ut::ticks() - t0);
    });
    ut::at_end([]{ CHECK_TRACE("P1-slept50"); });
}

UT_TEST_DEFECT(channel, forced_wakeup_leaves_no_stale_producer, "N7")
{
    // the same with a producer waiting in push() on a full channel
    static channel<int, 1> C;
    ut::proc(0, []{
        C.push(1);
        OS::sleep(1);                       // P1 blocks in push()
        ut::P(1).force_wake_up();
        int x; C.pop(x);                    // P1 is ready already: stays in the producers map
        OS::sleep(5);
        C.pop(x);                           // tick 6: wakes "producers" - must not affect P1
        OS::sleep(100);
    });
    ut::proc(1, []{
        C.push(2);
        unsigned t0 = ut::ticks();
        OS::sleep(50);
        T("P1-slept%u", ut::ticks() - t0);
    });
    ut::at_end([]{ CHECK_TRACE("P1-slept50"); });
}

UT_TEST(mutex, timed_out_waiter_does_not_get_mutex_later)
{
    static TMutex M;
    ut::proc(0, []{ OS::sleep(1); CHECK(!M.try_lock(2)); OS::sleep(5); CHECK(!M.is_locked()); });
    ut::proc(1, []{ M.lock(); OS::sleep(5); M.unlock(); CHECK(!M.is_locked()); });
}

UT_TEST(mutex, unlock_isr_wakes_waiter)
{
    static TMutex M;
    ut::irq(0, []{ TISRW isr; M.unlock_isr(); T("isr"); });
    ut::proc(0, []{ OS::sleep(1); M.lock(); T("P0-got"); });
    ut::proc(1, []{ M.lock(); OS::sleep(2); ut::raise(0); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("isr P0-got P1"); CHECK(M.is_locked()); });
}

UT_TEST(mutex, scoped_locker)
{
    static TMutex M;
    ut::proc(0, []{
        {
            TMutexLocker lock(M);
            CHECK(M.is_locked());
        }
        CHECK(!M.is_locked());
    });
}

UT_TEST(mutex, mutual_exclusion_under_preemption)
{
    // two processes increment a shared value non-atomically under the mutex;
    // ticks from the lower process preempt it inside the critical region
    static TMutex M;
    static volatile int Shared;
    ut::proc(0, []{
        for(int i = 0; i < 200; ++i)
        {
            M.lock(); int v = Shared; OS::sleep(1); Shared = v + 1; M.unlock();
        }
    });
    ut::proc(1, []{
        for(int i = 0; i < 200; ++i)
        {
            M.lock(); int v = Shared; ut::tick(); Shared = v + 1; M.unlock();
        }
    });
    ut::max_ticks(5000);
    ut::at_end([]{ CHECK_EQ(Shared, 400); });
}

//==============================================================================
//
//    channel<T, Size, S>
//
UT_TEST(channel, push_pop_fifo)
{
    static channel<int, 4> C;
    ut::proc(0, []{
        C.push(1); C.push(2); C.push(3);
        CHECK_EQ(C.get_count(), 3);
        CHECK_EQ(C.get_free_size(), 1);
        int x;
        CHECK(C.pop(x)); CHECK_EQ(x, 1);
        CHECK(C.pop(x)); CHECK_EQ(x, 2);
        CHECK(C.pop(x)); CHECK_EQ(x, 3);
        CHECK_EQ(C.get_count(), 0);
    });
}

UT_TEST(channel, push_front_and_pop_back)
{
    static channel<int, 4> C;
    ut::proc(0, []{
        C.push(2); C.push_front(1); C.push(3);      // 1 2 3
        int x;
        CHECK(C.pop_back(x)); CHECK_EQ(x, 3);
        CHECK(C.pop(x));      CHECK_EQ(x, 1);
        CHECK(C.pop_back(x)); CHECK_EQ(x, 2);
    });
}

UT_TEST(channel, pop_blocks_until_push)
{
    static channel<int, 4> C;
    ut::proc(0, []{ int x = 0; bool r = C.pop(x); T("P0:%d:%d@%u", r, x, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(3); T("push"); C.push(42); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("push P0:1:42@3 P1"); });
}

UT_TEST(channel, pop_back_blocks_until_push)
{
    static channel<int, 4> C;
    ut::proc(0, []{ int x = 0; bool r = C.pop_back(x, 10); T("P0:%d:%d", r, x); });
    ut::proc(1, []{ OS::sleep(2); C.push(5); });
    ut::at_end([]{ CHECK_TRACE("P0:1:5"); });
}

UT_TEST(channel, push_blocks_when_full)
{
    static channel<int, 2> C;
    ut::proc(0, []{ C.push(1); C.push(2); T("full"); C.push(3); T("P0-pushed@%u", ut::ticks()); });
    ut::proc(1, []{ OS::sleep(4); int x; C.pop(x); T("popped%d", x); });
    ut::at_end([]{
        CHECK_TRACE("full P0-pushed@4 popped1");      // P0 preempts P1 inside pop()
        CHECK_EQ(C.get_count(), 2);
    });
}

UT_TEST(channel, push_front_blocks_when_full)
{
    static channel<int, 1> C;
    ut::proc(0, []{ C.push(1); C.push_front(0); T("P0@%u", ut::ticks()); });
    ut::proc(1, []{ OS::sleep(2); int x; C.pop(x); T("got%d", x); });
    ut::at_end([]{ CHECK_TRACE("P0@2 got1"); int x = -1; CHECK(C.pop(x, 1) && x == 0); });
}

UT_TEST(channel, pop_timeout)
{
    static channel<int, 4> C;
    ut::proc(0, []{
        int x = -7;
        CHECK(!C.pop(x, 5));
        CHECK_EQ(ut::ticks(), 5);
        CHECK_EQ(x, -7);                    // untouched on timeout
        CHECK(!C.pop_back(x, 3));
        CHECK_EQ(ut::ticks(), 8);
    });
}

UT_TEST(channel, item_arriving_before_timeout)
{
    static channel<int, 4> C;
    ut::proc(0, []{ int x; bool r = C.pop(x, 5); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(4); C.push(1); });
    ut::at_end([]{ CHECK_TRACE("P0:1@4"); });
}

UT_TEST(channel, one_push_wakes_highest_consumer_only)
{
    static channel<int, 4> C;
    ut::proc(0, []{ int x; C.pop(x); T("P0:%d", x); });
    ut::proc(1, []{ int x; C.pop(x); T("P1:%d", x); });
    ut::proc(2, []{ C.push(10); T("a"); C.push(20); T("b"); });
    // push wakes both; P0 takes 10, P1 finds the channel empty and waits again
    ut::at_end([]{ CHECK_TRACE("P0:10 a P1:20 b"); });
}

UT_TEST(channel, pop_back_waiter_loses_item_to_higher)
{
    static channel<int, 4> C;
    ut::proc(0, []{ int x; C.pop_back(x); T("P0:%d", x); });
    ut::proc(1, []{ int x; bool r = C.pop_back(x, 10); T("P1:%d:%d@%u", r, x, ut::ticks()); });
    ut::proc(2, []{ C.push(1); T("a"); OS::sleep(3); C.push(2); });
    // the push wakes both; P0 takes the item, P1 waits again and gets the next one
    ut::at_end([]{ CHECK_TRACE("P0:1 a P1:1:2@3"); });
}

UT_TEST_DEFECT(channel, timeout_kept_when_item_is_stolen, "2")
{
    // P1 waits with pop(x, 5). At tick 4 a push wakes it, but P0 (higher)
    // takes the item first, and P1's timeout runs out before it gets the CPU.
    // The wait must still end with false at the deadline: at tick 5, or at
    // tick 6 when the system timer does not count down a ready process
    // (develop). v5.2.0 waits forever here.
    static channel<int, 4> C;
    static int Result = -1;
    static unsigned Tick;
    ut::proc(1, []{ int x; Result = C.pop(x, 5); Tick = ut::ticks(); });
    ut::proc(0, []{
        OS::sleep(4);
        C.push(1);                          // P1 becomes ready, P0 keeps the CPU
        ut::tick();                         // tick 5
        int x; C.pop(x);
        OS::sleep(20);
    });
    ut::at_end([]{ CHECK_EQ(Result, 0); CHECK(Tick == 5 || Tick == 6); });
}

UT_TEST(channel, read_waits_for_enough_items)
{
    static channel<int, 8> C;
    ut::proc(0, []{
        int buf[3] = {};
        bool r = C.read(buf, 3);
        T("read:%d@%u:%d,%d,%d", r, ut::ticks(), buf[0], buf[1], buf[2]);
    });
    ut::proc(1, []{ for(int i = 1; i <= 3; ++i) { OS::sleep(2); C.push(i); T("push%d", i); } });
    ut::at_end([]{ CHECK_TRACE("push1 push2 read:1@6:1,2,3 push3"); });
}

UT_TEST(channel, read_timeout_leaves_data)
{
    static channel<int, 8> C;
    ut::proc(0, []{
        int buf[3];
        CHECK(!C.read(buf, 3, 4));
        CHECK_EQ(ut::ticks(), 4);
        CHECK_EQ(C.get_count(), 2);
    });
    ut::proc(1, []{ C.push(1); C.push(2); });
}

UT_TEST(channel, write_and_read_block)
{
    static channel<int, 4> C;
    ut::proc(0, []{
        const int a[3] = { 1, 2, 3 }, b[3] = { 4, 5, 6 };
        C.write(a, 3);
        T("w1");
        C.write(b, 3);                      // only 1 free: waits for 2 more
        T("w2@%u", ut::ticks());
    });
    ut::proc(1, []{
        OS::sleep(1);
        int out[2];
        C.read(out, 2);
        T("r:%d,%d", out[0], out[1]);
    });
    ut::at_end([]{
        CHECK_TRACE("w1 w2@1 r:1,2");                 // P0 preempts P1 inside read()
        CHECK_EQ(C.get_count(), 4);
        int out[4]; C.read(out, 4);
        CHECK(out[0] == 3 && out[1] == 4 && out[2] == 5 && out[3] == 6);
    });
}

UT_TEST(channel, write_isr_partial)
{
    static channel<int, 4> C;
    static int Written;
    ut::irq(0, []{ TISRW isr; const int d[6] = { 1, 2, 3, 4, 5, 6 }; Written = C.write_isr(d, 6); });
    ut::proc(0, []{
        C.push(0);
        ut::raise(0);
        CHECK_EQ(Written, 3);               // only 3 free
        CHECK_EQ(C.get_free_size(), 0);
        int out[4]; C.read(out, 4);
        CHECK(out[0] == 0 && out[1] == 1 && out[2] == 2 && out[3] == 3);
    });
}

UT_TEST(channel, read_isr_partial)
{
    static channel<int, 4> C;
    static int Got, Buf[8];
    ut::irq(0, []{ TISRW isr; Got = C.read_isr(Buf, 8); });
    ut::proc(0, []{
        C.push(7); C.push(8);
        ut::raise(0);
        CHECK_EQ(Got, 2);
        CHECK(Buf[0] == 7 && Buf[1] == 8);
        CHECK_EQ(C.get_count(), 0);
        ut::raise(0);
        CHECK_EQ(Got, 0);                   // empty channel: nothing read
    });
}

UT_TEST(channel, write_isr_wakes_consumer)
{
    static channel<int, 4> C;
    ut::irq(0, []{ TISRW isr; const int d = 9; C.write_isr(&d, 1); T("isr"); });
    ut::proc(0, []{ int x; C.pop(x); T("P0:%d", x); });
    ut::proc(1, []{ ut::raise(0); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("isr P0:9 P1"); });
}

UT_TEST(channel, read_isr_wakes_producer)
{
    static channel<int, 1> C;
    ut::irq(0, []{ TISRW isr; int x = -1; C.read_isr(&x, 1); T("isr:%d", x); });
    ut::proc(0, []{ C.push(1); C.push(2); T("P0"); });
    ut::proc(1, []{ ut::raise(0); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("isr:1 P0 P1"); });
}

UT_TEST(channel, flush_wakes_producers)
{
    static channel<int, 2> C;
    ut::proc(0, []{ C.push(1); C.push(2); C.push(3); T("P0"); });
    ut::proc(1, []{ T("flush"); C.flush(); T("P1"); });
    ut::at_end([]{
        CHECK_TRACE("flush P0 P1");
        CHECK_EQ(C.get_count(), 1);
        int x = -1; CHECK(C.pop(x, 1) && x == 3);
    });
}

//------------------------------------------------------------------------------
//  Item type that notes whether it is ever copied with the interrupts enabled.
//  The channel and the message copy items into and out of their storage; an
//  interrupt in the middle of that would see (or leave) a half-written object.
namespace
{
    volatile unsigned UnprotectedCopies, Copies;

    struct TGuarded
    {
        int v;
        TGuarded(int x = 0) : v(x) { }
        TGuarded(const TGuarded & o) : v(o.v) { note(); }
        TGuarded & operator=(const TGuarded & o) { v = o.v; note(); return *this; }
        static void note()
        {
            Copies = Copies + 1;
            if(!get_interrupt_state()) UnprotectedCopies = UnprotectedCopies + 1;
        }
    };

    // copies made by the caller itself (arguments, results) are not the service's business
    struct TQuiet { unsigned c, u; TQuiet() : c(Copies), u(UnprotectedCopies) { } ~TQuiet() { Copies = c; UnprotectedCopies = u; } };
}

UT_TEST(channel, items_are_copied_only_inside_critical_sections)
{
    static channel<TGuarded, 4> C;
    ut::proc(0, []{
        TGuarded a(1), b(2), out;
        TGuarded arr[2], dst[2];
        { TQuiet q; arr[0] = TGuarded(3); arr[1] = TGuarded(4); }

        C.push(a);           CHECK_EQ(UnprotectedCopies, 0);
        C.push_front(b);     CHECK_EQ(UnprotectedCopies, 0);
        C.pop(out);          CHECK_EQ(UnprotectedCopies, 0);   CHECK_EQ(out.v, 2);
        C.pop_back(out);     CHECK_EQ(UnprotectedCopies, 0);   CHECK_EQ(out.v, 1);
        C.write(arr, 2);     CHECK_EQ(UnprotectedCopies, 0);
        C.read(dst, 2);      CHECK_EQ(UnprotectedCopies, 0);   CHECK_EQ(dst[1].v, 4);
    #ifndef UT_V52
        CHECK(C.try_push(a));        CHECK_EQ(UnprotectedCopies, 0);
        CHECK(C.try_push_front(b));  CHECK_EQ(UnprotectedCopies, 0);
        CHECK(C.try_pop(out));       CHECK_EQ(UnprotectedCopies, 0);
        C.flush();
    #endif
        CHECK(Copies >= 8);                                    // the probe itself works
    });
}

UT_TEST(channel, isr_transfers_copy_items_with_interrupts_disabled)
{
    static channel<TGuarded, 4> C;
    static TGuarded Src[2], Dst[2];
    ut::irq(0, []{ TISRW isr; C.write_isr(Src, 2); });
    ut::irq(1, []{ TISRW isr; C.read_isr(Dst, 2); });
    ut::proc(0, []{
        { TQuiet q; Src[0] = TGuarded(5); Src[1] = TGuarded(6); }
        ut::raise(0); ut::raise(1);
        CHECK_EQ(UnprotectedCopies, 0);
        CHECK(Copies >= 4);
        CHECK_EQ(Dst[1].v, 6);
    });
}

// After a blocking wait the service goes on inside its critical section: the
// scheduler must give it back with the interrupts disabled. Here the copies
// happen right after the producer and the consumer were blocked.
UT_TEST(channel, items_are_copied_inside_critical_sections_after_blocking)
{
    static channel<TGuarded, 1> C;
    ut::proc(0, []{
        TGuarded out;
        C.pop(out);                         // blocks: the channel is empty
        CHECK_EQ(out.v, 1);
        CHECK(C.pop(out, 10));              // blocks again, until P1's second push
        CHECK_EQ(out.v, 2);
    });
    ut::proc(1, []{
        TGuarded a(1), b(2), c(3);
        C.push(a);                          // P0 takes it at once
        C.push(b);
        C.push(c);                          // blocks: the channel is full
    }, ut::DAEMON);
    ut::proc(2, []{
        OS::sleep(1);
        TGuarded out;
        C.pop(out);                         // frees P1, which copies its item after the wait
        CHECK_EQ(out.v, 3);
        CHECK_EQ(UnprotectedCopies, 0);
        CHECK(Copies >= 6);
    });
}

UT_TEST(message, payload_is_copied_only_inside_critical_sections)
{
    static message<TGuarded> M;
    ut::proc(0, []{
        TGuarded a(7), out;
        M = a;               CHECK_EQ(UnprotectedCopies, 0);
        M.out(out);          CHECK_EQ(UnprotectedCopies, 0);   CHECK_EQ(out.v, 7);
        CHECK(Copies >= 2);
    });
}

UT_TEST(channel, struct_items_wide_index)
{
    struct TMsg { uint32_t id; char text[12]; };
    static channel<TMsg, 300, uint16_t> C;
    ut::proc(0, []{
        for(uint32_t i = 0; i < 300; ++i) { TMsg m = { i, "x" }; C.push(m); }
        CHECK_EQ(C.get_free_size(), 0);
        for(uint32_t i = 0; i < 300; ++i) { TMsg m = {}; C.pop(m); CHECK_EQ(m.id, i); }
    });
}

UT_TEST(channel, producer_consumer_stress)
{
    static channel<unsigned, 3> C;
    static unsigned Sum, Errors;
    ut::max_ticks(20000);
    ut::proc(1, []{ for(unsigned i = 0; i < 2000; ++i) { C.push(i); if(i % 7 == 0) OS::sleep(1); } });
    ut::proc(0, []{
        for(unsigned i = 0; i < 2000; ++i)
        {
            unsigned x;
            if(!C.pop(x, 50)) { ++Errors; break; }
            if(x != i) ++Errors;
            Sum += x;
            if(i % 5 == 0) OS::sleep(1);
        }
    });
    ut::at_end([]{ CHECK_EQ(Errors, 0); CHECK_EQ(Sum, 2000u * 1999u / 2); });
}

#ifndef UT_V52
UT_TEST(channel, try_push_and_try_pop)
{
    static channel<int, 2> C;
    ut::proc(0, []{
        int x;
        CHECK(!C.try_pop(x));
        CHECK(C.try_push(1));
        CHECK(C.try_push_front(0));
        CHECK(!C.try_push(2));              // full
        CHECK(!C.try_push_front(2));
        CHECK(C.try_pop(x)); CHECK_EQ(x, 0);
        CHECK(C.try_pop(x)); CHECK_EQ(x, 1);
        CHECK(!C.try_pop(x));
    });
}

UT_TEST(channel, try_push_wakes_consumer)
{
    static channel<int, 2> C;
    ut::proc(0, []{ int x; C.pop(x); T("P0:%d", x); });
    ut::proc(1, []{ CHECK(C.try_push(5)); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("P0:5 P1"); });
}

UT_TEST(channel, try_pop_wakes_producer)
{
    static channel<int, 1> C;
    ut::proc(0, []{ C.push(1); C.push(2); T("P0"); });
    ut::proc(1, []{ int x = -1; CHECK(C.try_pop(x)); T("P1:%d", x); });
    ut::at_end([]{ CHECK_TRACE("P0 P1:1"); });
}

UT_TEST_DEFECT(channel, try_pop_back_on_empty_channel, "N1")
{
    static channel<int, 4> C;
    ut::proc(0, []{ int x = -1; CHECK(!C.try_pop_back(x)); CHECK_EQ(C.get_count(), 0); });
}

UT_TEST_DEFECT(channel, try_pop_back_on_filled_channel, "N1")
{
    static channel<int, 4> C;
    ut::proc(0, []{
        C.push(1); C.push(2);
        int x = -1;
        CHECK(C.try_pop_back(x));
        CHECK_EQ(x, 2);
        CHECK_EQ(C.get_count(), 1);
    });
}

UT_TEST_DEFECT(channel, try_pop_back_copies_the_item_inside_a_critical_section, "N1")
{
    static channel<TGuarded, 4> C;
    ut::proc(0, []{
        TGuarded a(1), b(2), out;
        C.push(a); C.push(b);
        unsigned before = Copies;
        CHECK(C.try_pop_back(out));
        CHECK_EQ(out.v, 2);
        CHECK(Copies > before);                     // the probe saw the copy
        CHECK_EQ(UnprotectedCopies, 0);
    });
}

UT_TEST_DEFECT(channel, try_pop_back_wakes_a_producer_waiting_for_room, "N1")
{
    static channel<int, 2> C;
    ut::proc(0, []{
        OS::sleep(1);                               // P1 has filled the channel and waits
        int x = -1;
        CHECK(C.try_pop_back(x));
        CHECK_EQ(x, 2);
        T("P0");
        OS::sleep(1);
        CHECK_EQ(C.get_count(), 2);                 // P1's third item went in
    });
    ut::proc(1, []{ C.push(1); C.push(2); C.push(3); T("P1@%u", ut::ticks()); });
    ut::at_end([]{ CHECK_TRACE("P0 P1@1"); });
}
#endif // UT_V52

//==============================================================================
//
//    message<T>
//
UT_TEST(message, send_without_waiter_latches)
{
    static message<int> M;
    ut::proc(0, []{
        CHECK(!M.is_non_empty());
        M = 5;
        M.send();
        CHECK(M.is_non_empty());
        CHECK(M.wait());                    // at once, and clears the state
        CHECK(!M.is_non_empty());
        CHECK_EQ(int(M), 5);
    });
}

UT_TEST(message, wait_blocks_until_send)
{
    static message<int> M;
    ut::proc(0, []{ bool r = M.wait(); int v; M.out(v); T("P0:%d:%d@%u", r, v, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(2); M = 77; T("send"); M.send(); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("send P0:1:77@2 P1"); CHECK(!M.is_non_empty()); });
}

UT_TEST(message, send_wakes_all_waiters)
{
    static message<int> M;
    ut::proc(0, []{ M.wait(); T("P0:%d", int(M)); });
    ut::proc(2, []{ M.wait(); T("P2:%d", int(M)); });
    ut::proc(3, []{ M = 3; M.send(); T("P3"); });
    ut::at_end([]{ CHECK_TRACE("P0:3 P2:3 P3"); });
}

UT_TEST(message, wait_timeout)
{
    static message<int> M;
    ut::proc(0, []{ CHECK(!M.wait(6)); CHECK_EQ(ut::ticks(), 6); });
}

UT_TEST(message, reset_discards_latched_message)
{
    static message<int> M;
    ut::proc(0, []{ M.send(); M.reset(); CHECK(!M.is_non_empty()); CHECK(!M.wait(2)); });
}

UT_TEST(message, send_isr)
{
    static message<int> M;
    ut::irq(0, []{ TISRW isr; M = 11; M.send_isr(); });
    ut::proc(0, []{ M.wait(); T("P0:%d", int(M)); });
    ut::proc(1, []{ ut::raise(0); T("P1"); ut::raise(0); CHECK(M.is_non_empty()); });
    ut::at_end([]{ CHECK_TRACE("P0:11 P1"); });
}

UT_TEST(message, struct_payload)
{
    struct TPoint { int x, y; double w; };
    static message<TPoint> M;
    ut::proc(0, []{
        TPoint p = { 1, -2, 0.5 };
        CHECK_EQ((M = p).y, -2);            // operator= returns the stored value
        p.x = 100;                          // the message keeps its own copy
        TPoint q = M;
        CHECK(q.x == 1 && q.y == -2 && q.w == 0.5);
        TPoint r; M.out(r);
        CHECK(r.x == 1);
    });
}

UT_TEST(message, wake_up_aborts_wait)
{
    static message<int> M;
    ut::proc(0, []{ bool r = M.wait(50); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(3); ut::P(0).wake_up(); });
    ut::at_end([]{ CHECK_TRACE("P0:0@3"); });
}

//==============================================================================
//
//    TService: the kit user services are built from
//
//    A minimal service made of suspend() / is_waked_up() / resume_all() /
//    resume_next_ready() and their ISR versions; checks their return values,
//    which the kernel's own services mostly ignore.
//
namespace
{
    class TGate : public TService
    {
    public:
        TGate() : Map(0) { }
        // true: released by open_*(); false: timeout or forced wake-up
        bool pass(timeout_t timeout = 0)
        {
            TCritSect cs;
            cur_proc_timeout() = timeout;
            suspend(Map);
        #ifdef UT_V52
            if(is_timeouted(Map)) return false;     // the v5.2.0 name
        #else
            if(is_waked_up(Map)) return false;
        #endif
            cur_proc_timeout() = 0;
            return true;
        }
        bool open_all()      { TCritSect cs; return resume_all(Map); }
        bool open_one()      { TCritSect cs; return resume_next_ready(Map); }
        bool open_all_isr()  { TCritSect cs; return resume_all_isr(Map); }
        bool open_one_isr()  { TCritSect cs; return resume_next_ready_isr(Map); }
        bool has_waiters()   { TCritSect cs; return Map != 0; }
    private:
        volatile TProcessMap Map;
    };
    TGate * G;
    int     GateResult[4];
}

UT_TEST(service_kit, resume_returns_false_without_waiters)
{
    static TGate Gate;
    ut::proc(0, []{
        CHECK(!Gate.open_all());
        CHECK(!Gate.open_one());
        CHECK(!Gate.open_all_isr());
        CHECK(!Gate.open_one_isr());
        CHECK(!Gate.has_waiters());
    });
}

UT_TEST(service_kit, resume_all_wakes_everybody_and_returns_true)
{
    static TGate Gate;
    ut::proc(0, []{ CHECK(Gate.pass()); T("P0"); });
    ut::proc(1, []{ CHECK(Gate.pass(50)); T("P1"); });
    ut::proc(2, []{
        CHECK(Gate.has_waiters());
        CHECK(Gate.open_all());             // both run before this returns
        T("P2");
        CHECK(!Gate.has_waiters());
        CHECK(!Gate.open_all());
    });
    ut::at_end([]{ CHECK_TRACE("P0 P1 P2"); });
}

UT_TEST(service_kit, resume_next_ready_wakes_one_and_returns_true)
{
    static TGate Gate;
    ut::proc(0, []{ CHECK(Gate.pass()); T("P0"); });
    ut::proc(1, []{ CHECK(Gate.pass()); T("P1"); });
    ut::proc(2, []{
        CHECK(Gate.open_one()); T("a");     // the highest waiter only
        CHECK(Gate.has_waiters());
        CHECK(Gate.open_one()); T("b");
        CHECK(!Gate.has_waiters());
        CHECK(!Gate.open_one());
    });
    ut::at_end([]{ CHECK_TRACE("P0 a P1 b"); });
}

UT_TEST(service_kit, isr_versions_return_values)
{
    static TGate Gate;
    G = &Gate;
    ut::irq(0, []{ TISRW isr; GateResult[0] = G->open_one_isr(); GateResult[1] = G->has_waiters(); });
    ut::irq(1, []{ TISRW isr; GateResult[2] = G->open_all_isr(); GateResult[3] = G->has_waiters(); });
    ut::proc(0, []{ CHECK(Gate.pass()); T("P0"); CHECK(Gate.pass()); T("P0"); });
    ut::proc(1, []{ CHECK(Gate.pass()); T("P1"); });
    ut::proc(2, []{
        ut::raise(0); T("a");               // P0 only
        CHECK_EQ(GateResult[0], 1); CHECK_EQ(GateResult[1], 1);
        ut::raise(1); T("b");               // P0 (waiting again) and P1
        CHECK_EQ(GateResult[2], 1); CHECK_EQ(GateResult[3], 0);
        ut::raise(0); ut::raise(1);
        CHECK_EQ(GateResult[0], 0); CHECK_EQ(GateResult[2], 0);
    });
    ut::at_end([]{ CHECK_TRACE("P0 a P0 P1 b"); });
}

UT_TEST(service_kit, timeout_and_forced_wakeup_return_false_and_leave_the_map)
{
    static TGate Gate;
    ut::proc(0, []{
        CHECK(!Gate.pass(3));
        CHECK_EQ(ut::ticks(), 3);
        CHECK(!Gate.has_waiters());
        CHECK(!Gate.pass());                // forced wake-up at tick 5
        CHECK_EQ(ut::ticks(), 5);
        CHECK(!Gate.has_waiters());
    });
    ut::proc(1, []{ OS::sleep(5); ut::P(0).force_wake_up(); CHECK(!Gate.open_all()); });
}

UT_TEST(service_kit, timed_out_process_is_not_resumed)
{
    // P1's timeout has expired but it has not run yet: a resume must skip it
    // (it will report the timeout itself) and return false
    static TGate Gate;
    ut::proc(1, []{ bool r = Gate.pass(2); T("P1:%d", r); });
    ut::proc(0, []{
        OS::sleep(1);
        ut::tick();                         // tick 2: P1 is ready by timeout, P0 keeps the CPU
        CHECK(!Gate.open_one());
        CHECK(!Gate.open_all());
        CHECK(Gate.has_waiters());          // still listed: P1 removes itself
    });
    ut::at_end([]{ CHECK_TRACE("P1:0"); CHECK(!Gate.has_waiters()); });
}

//==============================================================================
//
//    Side effects the services must not have
//
UT_TEST(event_flag, signal_isr_to_a_waiter_does_not_latch)
{
    static TEventFlag F;
    ut::irq(0, []{ TISRW isr; F.signal_isr(); });
    ut::proc(0, []{ CHECK(F.wait()); });
    ut::proc(1, []{ ut::raise(0); CHECK(!F.is_signaled()); });
}

UT_TEST(message, send_isr_to_a_waiter_does_not_latch)
{
    static message<int> M;
    ut::irq(0, []{ TISRW isr; M.send_isr(); });
    ut::proc(0, []{ CHECK(M.wait()); });
    ut::proc(1, []{ ut::raise(0); CHECK(!M.is_non_empty()); ut::raise(0); CHECK(M.is_non_empty()); });
}

UT_TEST(mutex, try_lock_timeout_takes_the_mutex)
{
    static TMutex M;
    ut::proc(0, []{
        CHECK(M.try_lock(5));               // free: taken at once
        CHECK(M.is_locked());
        OS::sleep(3);
        M.unlock();
    });
    ut::proc(1, []{
        CHECK(!M.try_lock());               // P0 really owns it
        CHECK(M.try_lock(10));              // released at tick 3
        CHECK(M.is_locked());
        M.unlock();
        CHECK(!M.is_locked());
    });
}

UT_TEST(mutex, unlock_wakes_nobody_but_waiters)
{
    // P4 sleeps and has nothing to do with the mutex; P1 and P2 wait for it
    static TMutex M;
    ut::proc(4, []{ unsigned t0 = ut::ticks(); OS::sleep(50); T("P4-slept%u", ut::ticks() - t0); });
    ut::proc(1, []{ OS::sleep(1); M.lock(); T("P1"); OS::sleep(3); M.unlock(); });
    ut::proc(2, []{ OS::sleep(1); M.lock(); T("P2@%u", ut::ticks()); M.unlock(); });
    ut::proc(3, []{ M.lock(); OS::sleep(2); unsigned s = ut::switches(); M.unlock(); CHECK_EQ(ut::switches() - s, 2); });
    // unlock at tick 2 wakes P1 only: to P1, which sleeps, and back to P3 - two switches
    ut::at_end([]{ CHECK_TRACE("P1 P2@5 P4-slept50"); });
}

UT_TEST(mutex, unlock_isr_takes_the_waiter_out_of_the_map)
{
    static TMutex M;
    ut::irq(0, []{ TISRW isr; M.unlock_isr(); });
    ut::proc(0, []{
        OS::sleep(1);
        M.lock();                           // released by the interrupt at tick 2
        M.unlock();
        unsigned t0 = ut::ticks();
        OS::sleep(50);
        T("P0-slept%u", ut::ticks() - t0);
    });
    ut::proc(1, []{
        M.lock();
        OS::sleep(2);
        ut::raise(0);
        OS::sleep(5);
        M.lock(); M.unlock();               // must not "resume" P0 out of its sleep
        OS::sleep(100);
    });
    ut::at_end([]{ CHECK_TRACE("P0-slept50"); });
}

UT_TEST(services, fresh_services_do_not_wake_sleeping_processes)
{
    // every service starts with an empty waiting map: using it must not make
    // sleeping processes ready (P0 and P4 are the ends of the priority range)
    ut::proc(0, []{ OS::sleep(50); T("P0@%u", ut::ticks()); });
    ut::proc(4, []{ OS::sleep(50); T("P4@%u", ut::ticks()); });
    ut::proc(2, []{
        static TEventFlag F;  static TMutex M;  static channel<int, 2> C;  static message<int> Msg;
        OS::sleep(5);
        int x = 0;
        F.signal(); F.clear();
        M.lock(); M.unlock();
        C.push(1); C.pop(x); C.push_front(2); C.pop_back(x);
        const int d[2] = { 1, 2 }; int o[2];
        C.write(d, 2); C.read(o, 2); C.flush();
        Msg.send(); Msg.reset();
    });
    ut::at_end([]{ CHECK_TRACE("P0@50 P4@50"); });
}

UT_TEST(services, successful_timed_wait_leaves_no_timeout)
{
    // After a wait with a timeout succeeds the timeout must be off. P0 goes
    // straight from each successful timed wait into TMutex::lock(), which does
    // not touch the timeout: there it must not be "sleeping".
    static TEventFlag F;  static TMutex M, Block;  static channel<int, 4> C;  static message<int> Msg;
    ut::proc(0, []{
        int x; int buf[2];
        CHECK(F.wait(100));         Block.lock(); Block.unlock();
        CHECK(Msg.wait(100));       Block.lock(); Block.unlock();
        CHECK(M.try_lock(100));     Block.lock(); Block.unlock(); M.unlock();
        CHECK(C.pop(x, 100));       Block.lock(); Block.unlock();
        CHECK(C.pop_back(x, 100));  Block.lock(); Block.unlock();
        CHECK(C.read(buf, 2, 100)); Block.lock(); Block.unlock();
    });
    ut::proc(1, []{
        const int d[2] = { 1, 2 };
        M.lock();
        for(int stage = 1; stage <= 6; ++stage)
        {
            Block.lock();
            OS::sleep(2);
            CHECK(ut::P(0).is_sleeping());  // in the timed wait
            switch(stage)                   // P0 preempts, succeeds and blocks on Block
            {
                case 1: F.signal();    break;
                case 2: Msg.send();    break;
                case 3: M.unlock();    break;
                case 4: C.push(1);     break;
                case 5: C.push(2);     break;
                case 6: C.write(d, 2); break;
            }
            CHECK(ut::P(0).is_suspended());
            CHECK(!ut::P(0).is_sleeping());
            Block.unlock();                 // P0 goes on to its next wait
        }
    });
}

//==============================================================================
//
//    channel: who wakes whom
//
UT_TEST(channel, push_front_wakes_consumer)
{
    static channel<int, 2> C;
    ut::proc(0, []{ int x = 0; C.pop(x); T("P0:%d", x); });
    ut::proc(1, []{ C.push_front(7); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("P0:7 P1"); });
}

UT_TEST(channel, write_wakes_consumer)
{
    static channel<int, 4> C;
    ut::proc(0, []{ int x = 0; C.pop(x); T("P0:%d", x); });
    ut::proc(1, []{ const int d[2] = { 5, 6 }; C.write(d, 2); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("P0:5 P1"); });
}

UT_TEST(channel, consumer_that_waited_wakes_producer)
{
    // P1 waits for data. P0 pushes one item (P1 becomes ready), then blocks on
    // the full channel. P1's pop, on its "after waiting" path, must wake P0.
    static channel<int, 1> C;
    ut::proc(1, []{ int x = 0; C.pop(x); T("P1:%d", x); });
    ut::proc(0, []{ OS::sleep(1); C.push(1); C.push(2); T("P0"); });
    ut::at_end([]{ CHECK_TRACE("P0 P1:1"); CHECK_EQ(C.get_count(), 1); });
}

UT_TEST(channel, pop_back_wakes_producer)
{
    static channel<int, 1> C;
    ut::proc(0, []{ C.push(1); C.push(2); T("P0"); });
    ut::proc(1, []{ int x = 0; C.pop_back(x); T("P1:%d", x); });
    ut::at_end([]{ CHECK_TRACE("P0 P1:1"); });
}

UT_TEST(channel, pop_back_consumer_that_waited_wakes_producer)
{
    static channel<int, 1> C;
    ut::proc(1, []{ int x = 0; C.pop_back(x); T("P1:%d", x); });
    ut::proc(0, []{ OS::sleep(1); C.push(1); C.push(2); T("P0"); });
    ut::at_end([]{ CHECK_TRACE("P0 P1:1"); });
}

UT_TEST(channel, read_wakes_producer)
{
    static channel<int, 2> C;
    ut::proc(0, []{ const int d[2] = { 1, 2 }; C.write(d, 2); C.write(d, 2); T("P0"); });
    ut::proc(1, []{ int o[2]; C.read(o, 2); T("P1:%d,%d", o[0], o[1]); });
    ut::at_end([]{ CHECK_TRACE("P0 P1:1,2"); });
}

#ifndef UT_V52
UT_TEST(channel, try_push_front_wakes_consumer)
{
    static channel<int, 2> C;
    ut::proc(0, []{ int x = 0; C.pop(x); T("P0:%d", x); });
    ut::proc(1, []{ CHECK(C.try_push_front(3)); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("P0:3 P1"); });
}
#endif
