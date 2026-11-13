//******************************************************************************
//*
//*     scmRTOS unit tests: TSemaphore (ext/semaphore)
//*
//******************************************************************************
#include "utest.h"
#include <semaphore.h>

using namespace OS;
#define T ut::trace

UT_TEST(semaphore, initial_and_max_count)
{
    static TSemaphore S(5, 2);
    CHECK_EQ(S.get_count(), 2);
    CHECK_EQ(S.get_max_count(), 5);
    CHECK(!S.has_waiters());
}

UT_TEST(semaphore, acquire_takes_available_tokens_at_once)
{
    static TSemaphore S(3, 3);
    ut::proc(0, []{
        unsigned s = ut::switches();
        CHECK(S.acquire()); CHECK(S.acquire(5)); CHECK(S.try_acquire());
        CHECK_EQ(S.get_count(), 0);
        CHECK_EQ(ut::switches(), s);
        CHECK(!S.try_acquire());
    });
}

UT_TEST(semaphore, release_up_to_max)
{
    static TSemaphore S(2, 0);
    ut::proc(0, []{
        CHECK(S.release()); CHECK(S.release());
        CHECK(!S.release());                // already at max: nothing released
        CHECK_EQ(S.get_count(), 2);
    });
}

UT_TEST(semaphore, acquire_blocks_until_release)
{
    static TSemaphore S(1, 0);
    ut::proc(0, []{ bool r = S.acquire(); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{
        CHECK(S.has_waiters());
        OS::sleep(4);
        T("rel");
        S.release();                        // the waiter takes the token at once
        T("P1");
        CHECK_EQ(S.get_count(), 0);
        CHECK(!S.has_waiters());
    });
    ut::at_end([]{ CHECK_TRACE("rel P0:1@4 P1"); });
}

UT_TEST(semaphore, acquire_timeout_keeps_count)
{
    static TSemaphore S(4, 0);
    ut::proc(0, []{
        unsigned t0 = ut::ticks();
        CHECK(!S.acquire(6));
        CHECK_EQ(ut::ticks() - t0, 6);
        CHECK_EQ(S.get_count(), 0);
        CHECK(!S.has_waiters());
    });
}

UT_TEST(semaphore, release_wakes_one_waiter_per_token_by_priority)
{
    static TSemaphore S(5, 0);
    ut::proc(1, []{ S.acquire(); T("P1"); });
    ut::proc(2, []{ S.acquire(); T("P2"); });
    ut::proc(3, []{ S.acquire(); T("P3"); });
    ut::proc(0, []{
        OS::sleep(1);                       // let all of them wait
        S.release(); T("r1");               // highest waiter, P1; lower than P0: no switch
        S.release(); T("r2");
        OS::sleep(1);
        S.release(); T("r3");
    });
    ut::at_end([]{ CHECK_TRACE("r1 r2 P1 P2 r3 P3"); CHECK_EQ(S.get_count(), 0); });
}

UT_TEST(semaphore, higher_process_steals_token_waiter_keeps_deadline)
{
    // P1 waits with timeout 5 from tick 0. At tick 2 the token is released and
    // immediately taken by P0. P1 must keep waiting and give up at its deadline.
    static TSemaphore S(1, 0);
    ut::proc(1, []{ bool r = S.acquire(5); T("P1:%d@%u", r, ut::ticks()); });
    ut::proc(0, []{ OS::sleep(2); S.release(); CHECK(S.try_acquire()); OS::sleep(10); });
    ut::at_end([]{ CHECK_TRACE("P1:0@5"); });
}

UT_TEST(semaphore, release_isr)
{
    static TSemaphore S(2, 0);
    ut::irq(0, []{ TISRW isr; CHECK(S.release_isr()); T("isr"); });
    ut::irq(1, []{ TISRW isr; S.release_isr(); S.release_isr(); CHECK(!S.release_isr()); });
    ut::proc(0, []{ S.acquire(); T("P0"); });
    ut::proc(1, []{ ut::raise(0); T("P1"); ut::raise(1); CHECK_EQ(S.get_count(), 2); });
    ut::at_end([]{ CHECK_TRACE("isr P0 P1"); });
}

UT_TEST(semaphore, wake_up_aborts_acquire)
{
    static TSemaphore S(1, 0);
    ut::proc(0, []{ bool r = S.acquire(100); T("P0:%d@%u", r, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(3); ut::P(0).wake_up(); CHECK(!S.has_waiters()); S.release(); CHECK_EQ(S.get_count(), 1); });
    ut::at_end([]{ CHECK_TRACE("P0:0@3"); });
}

UT_TEST(semaphore, counting_resource_pool)
{
    // 4 workers share 2 tokens; at most 2 may hold one at a time
    static TSemaphore S(2, 2);
    static int Holders, MaxHolders, Done;
    struct W { static void body() {
        for(int i = 0; i < 20; ++i)
        {
            S.acquire();
            if(++Holders > MaxHolders) MaxHolders = Holders;
            OS::sleep(1);
            --Holders;
            S.release();
        }
        ++Done;
    } };
    for(int i = 0; i < 4; ++i) ut::proc(i, W::body);
    ut::at_end([]{ CHECK_EQ(MaxHolders, 2); CHECK_EQ(Done, 4); CHECK_EQ(S.get_count(), 2); });
}

UT_TEST(semaphore, successful_timed_acquire_leaves_no_timeout)
{
    static TSemaphore S(1, 0);  static TMutex Block;
    ut::proc(0, []{ CHECK(S.acquire(100)); Block.lock(); Block.unlock(); });
    ut::proc(1, []{
        Block.lock();
        OS::sleep(2);
        CHECK(ut::P(0).is_sleeping());
        S.release();
        CHECK(ut::P(0).is_suspended());
        CHECK(!ut::P(0).is_sleeping());
        Block.unlock();
    });
}

UT_TEST(semaphore, release_wakes_nobody_but_waiters)
{
    static TSemaphore S(3, 0);
    ut::proc(0, []{ OS::sleep(50); T("P0@%u", ut::ticks()); });
    ut::proc(4, []{ OS::sleep(50); T("P4@%u", ut::ticks()); });
    ut::proc(1, []{ OS::sleep(2); S.acquire(); T("P1@%u", ut::ticks()); });
    ut::proc(2, []{ OS::sleep(5); S.release(); S.release(); CHECK_EQ(S.get_count(), 1); });
    ut::at_end([]{ CHECK_TRACE("P1@5 P0@50 P4@50"); });
}
