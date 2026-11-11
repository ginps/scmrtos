//******************************************************************************
//*
//*     scmRTOS unit tests: TRecursiveMutex (ext/recursive-mutex)
//*
//******************************************************************************
#include "utest.h"
#include <recursive_mutex.h>

using namespace OS;
#define T ut::trace

UT_TEST(recursive_mutex, nested_lock_needs_as_many_unlocks)
{
    static TRecursiveMutex M;
    ut::proc(0, []{
        M.lock(); M.lock(); M.lock();
        OS::sleep(1);                       // P1 tries
        M.unlock(); M.unlock();
        CHECK(M.is_locked());
        OS::sleep(1);                       // P1 tries again
        M.unlock();
        CHECK(!M.is_locked());
    });
    ut::proc(1, []{
        CHECK(!M.try_lock());
        OS::sleep(1);
        CHECK(!M.try_lock());
    });
}

UT_TEST(recursive_mutex, waiter_gets_it_after_last_unlock)
{
    static TRecursiveMutex M;
    ut::proc(0, []{ OS::sleep(1); M.lock(); T("P0@%u", ut::ticks()); M.unlock(); });
    ut::proc(1, []{
        M.lock(); M.lock();
        OS::sleep(3); T("u1"); M.unlock();  // still held once: P0 keeps waiting
        OS::sleep(2); T("u2"); M.unlock();  // P0 takes it here
        T("P1");
    });
    ut::at_end([]{ CHECK_TRACE("u1 u2 P0@5 P1"); });
}

UT_TEST(recursive_mutex, unlock_by_non_owner_is_ignored)
{
    static TRecursiveMutex M;
    ut::proc(0, []{ OS::sleep(1); M.unlock(); M.force_unlock(); CHECK(M.is_locked()); });
    ut::proc(1, []{ M.lock(); OS::sleep(2); M.unlock(); CHECK(!M.is_locked()); });
}

UT_TEST(recursive_mutex, unlock_when_not_locked_is_harmless)
{
    static TRecursiveMutex M;
    ut::proc(0, []{
        M.unlock(); M.force_unlock();
        CHECK(!M.is_locked());
        M.lock(); M.unlock();
        M.unlock();                         // one too many
        CHECK(!M.is_locked());
        M.lock();                           // nesting starts from 1 again
        M.unlock();
        CHECK(!M.is_locked());
    });
}

UT_TEST(recursive_mutex, force_unlock_releases_all_levels)
{
    static TRecursiveMutex M;
    ut::proc(0, []{ OS::sleep(1); M.lock(); T("P0"); M.unlock(); });
    ut::proc(1, []{ M.lock(); M.lock(); M.lock(); OS::sleep(2); T("force"); M.force_unlock(); T("P1"); CHECK(!M.is_locked()); });
    ut::at_end([]{ CHECK_TRACE("force P0 P1"); });
}

UT_TEST(recursive_mutex, try_lock_is_reentrant)
{
    static TRecursiveMutex M;
    ut::proc(0, []{
        CHECK(M.try_lock());
        CHECK(M.try_lock());                // owner: nests
        M.unlock();
        CHECK(M.is_locked());
        M.unlock();
        CHECK(!M.is_locked());
    });
}

UT_TEST(recursive_mutex, try_lock_timeout)
{
    static TRecursiveMutex M;
    ut::proc(0, []{
        OS::sleep(1);
        unsigned t0 = ut::ticks();
        CHECK(!M.try_lock(3));
        CHECK_EQ(ut::ticks() - t0, 3);
        CHECK(M.try_lock(20));              // released at tick 10
        CHECK_EQ(ut::ticks(), 10);
        CHECK(M.try_lock(5));               // owner: at once
        CHECK_EQ(ut::ticks(), 10);
        M.unlock(); M.unlock();
        CHECK(!M.is_locked());
    });
    ut::proc(1, []{ M.lock(); OS::sleep(10); M.unlock(); });
}

UT_TEST_DEFECT(recursive_mutex, try_lock_timeout_is_total_across_handovers, "6")
{
    static TRecursiveMutex M;
    ut::proc(0, []{ M.lock(); OS::sleep(3); M.unlock(); M.lock(); OS::sleep(20); M.unlock(); });
    ut::proc(1, []{ bool r = M.try_lock(5); T("P1:%d@%u", r, ut::ticks()); });
    ut::at_end([]{ CHECK_TRACE("P1:0@5"); });
}

UT_TEST(recursive_mutex, unlock_isr_decrements_and_releases)
{
    static TRecursiveMutex M;
    ut::irq(0, []{ TISRW isr; M.unlock_isr(); T("isr"); });
    ut::proc(0, []{ OS::sleep(1); M.lock(); T("P0"); M.unlock(); });
    ut::proc(1, []{
        M.lock(); M.lock();
        OS::sleep(2);
        ut::raise(0);                       // nesting 2 -> 1
        CHECK(M.is_locked());
        ut::raise(0);                       // 1 -> 0: P0 gets it
        T("P1");
    });
    ut::at_end([]{ CHECK_TRACE("isr isr P0 P1"); });
}

UT_TEST(recursive_mutex, force_unlock_isr)
{
    static TRecursiveMutex M;
    ut::irq(0, []{ TISRW isr; M.force_unlock_isr(); });
    ut::proc(0, []{ OS::sleep(1); M.lock(); T("P0"); M.unlock(); });
    ut::proc(1, []{ M.lock(); M.lock(); M.lock(); OS::sleep(2); ut::raise(0); T("P1"); ut::raise(0); });
    ut::at_end([]{ CHECK_TRACE("P0 P1"); CHECK(!M.is_locked()); });
}

UT_TEST(recursive_mutex, handover_to_highest_waiter)
{
    static TRecursiveMutex M;
    ut::proc(3, []{ M.lock(); M.lock(); OS::sleep(2); M.unlock(); M.unlock(); });
    ut::proc(2, []{ OS::sleep(1); M.lock(); T("P2"); M.unlock(); });
    ut::proc(0, []{ OS::sleep(1); M.lock(); T("P0"); M.lock(); M.unlock(); M.unlock(); });
    ut::at_end([]{ CHECK_TRACE("P0 P2"); });
}

UT_TEST(recursive_mutex, try_lock_timeout_on_free_mutex)
{
    static TRecursiveMutex M;
    ut::proc(0, []{ CHECK(M.try_lock(5)); CHECK_EQ(ut::ticks(), 0); CHECK(M.is_locked()); M.unlock(); CHECK(!M.is_locked()); });
}

UT_TEST(recursive_mutex, unlock_isr_when_not_locked_is_harmless)
{
    static TRecursiveMutex M;
    ut::irq(0, []{ TISRW isr; M.unlock_isr(); });
    ut::proc(0, []{ ut::raise(0); CHECK(!M.is_locked()); M.lock(); CHECK(M.is_locked()); M.unlock(); CHECK(!M.is_locked()); });
}
