//******************************************************************************
//*
//*     scmRTOS unit tests: TEventFlags (ext/event-flags)
//*
//******************************************************************************
#include "utest.h"
#include <event_flags.h>

using namespace OS;
#define T ut::trace

UT_TEST(event_flags, set_clear_get_values)
{
    static TEventFlags F;
    ut::proc(0, []{
        CHECK_EQ(F.get(), 0);
        CHECK_EQ(F.set(0x5), 0x5);          // value after setting
        CHECK_EQ(F.set(0x30), 0x35);
        CHECK_EQ(F.clear(0x11), 0x35);      // value before clearing
        CHECK_EQ(F.get(), 0x24);
        CHECK_EQ(F.clear(0xFFFFFFFF), 0x24);
        CHECK_EQ(F.get(), 0);
    });
}

UT_TEST(event_flags, wait_any_already_set)
{
    static TEventFlags F;
    ut::proc(0, []{
        F.set(0x6);
        uint32_t r = 0;
        CHECK(F.wait(0x3, efWaitAny, 5, &r));
        CHECK_EQ(r, 0x6);                   // the value before the waited bits are cleared
        CHECK_EQ(F.get(), 0x4);             // only the requested bits are cleared
        CHECK_EQ(ut::ticks(), 0);
    });
}

UT_TEST(event_flags, wait_all_needs_every_bit)
{
    static TEventFlags F;
    ut::proc(0, []{ uint32_t r = 0; bool ok = F.wait(0x3, efWaitAll, 0, &r); T("P0:%d:%x@%u", ok, unsigned(r), ut::ticks()); });
    ut::proc(1, []{
        OS::sleep(2); F.set(0x1); T("s1");  // P0 wakes, re-checks, sleeps again
        OS::sleep(2); F.set(0x2); T("s2");
    });
    ut::at_end([]{ CHECK_TRACE("s1 P0:1:3@4 s2"); CHECK_EQ(F.get(), 0); });
}

UT_TEST(event_flags, wait_any_wakes_on_first_bit)
{
    static TEventFlags F;
    ut::proc(0, []{ bool ok = F.wait(0x30, efWaitAny, 0); T("P0:%d@%u", ok, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(1); F.set(0x1); OS::sleep(1); F.set(0x10); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("P0:1@2 P1"); CHECK_EQ(F.get(), 0x1); });
}

UT_TEST(event_flags, no_clear_option)
{
    static TEventFlags F;
    ut::proc(0, []{
        F.set(0x9);
        CHECK(F.wait(0x8, efWaitAny | efNoClear, 1));
        CHECK_EQ(F.get(), 0x9);
        CHECK(F.wait(0x9, efWaitAll | efNoClear, 1));
        CHECK_EQ(F.get(), 0x9);
        CHECK(F.wait(0x9, efWaitAll, 1));
        CHECK_EQ(F.get(), 0);
    });
}

UT_TEST(event_flags, timeout)
{
    static TEventFlags F;
    ut::proc(0, []{
        uint32_t r = 0xDEAD;
        CHECK(!F.wait(0x1, efWaitAny, 7, &r));
        CHECK_EQ(ut::ticks(), 7);
        CHECK_EQ(r, 0xDEAD);                // result untouched on timeout
        CHECK(!F.has_waiters());
    });
}

UT_TEST(event_flags, deadline_is_absolute_across_unrelated_sets)
{
    // unrelated bits wake the waiter several times; the timeout must not restart
    static TEventFlags F;
    ut::proc(0, []{ bool ok = F.wait(0x100, efWaitAny, 10); T("P0:%d@%u", ok, ut::ticks()); });
    ut::proc(1, []{ for(int i = 0; i < 8; ++i) { OS::sleep(2); F.set(1u << i); } });
    ut::at_end([]{ CHECK_TRACE("P0:0@10"); });
}

UT_TEST(event_flags, first_waiter_consumes_bits)
{
    static TEventFlags F;
    ut::proc(0, []{ bool ok = F.wait(0x1, efWaitAny, 10); T("P0:%d", ok); });
    ut::proc(1, []{ bool ok = F.wait(0x1, efWaitAny, 10); T("P1:%d@%u", ok, ut::ticks()); });
    ut::proc(2, []{ OS::sleep(2); F.set(0x1); T("P2"); });
    // P0 runs first and clears the bit; P1 re-checks, waits again, times out
    ut::at_end([]{ CHECK_TRACE("P0:1 P2 P1:0@10"); });
}

UT_TEST(event_flags, no_clear_waiters_all_wake)
{
    static TEventFlags F;
    ut::proc(0, []{ F.wait(0x1, efWaitAny | efNoClear, 0); T("P0"); });
    ut::proc(1, []{ F.wait(0x1, efWaitAny | efNoClear, 0); T("P1"); });
    ut::proc(3, []{ F.wait(0x1, efWaitAny, 0); T("P3"); });
    ut::proc(2, []{ OS::sleep(1); F.set(0x1); T("P2"); });
    ut::at_end([]{ CHECK_TRACE("P0 P1 P2 P3"); CHECK_EQ(F.get(), 0); });
}

UT_TEST(event_flags, try_wait)
{
    static TEventFlags F;
    ut::proc(0, []{
        uint32_t r = 0;
        CHECK(!F.try_wait(0x1, efWaitAny));
        F.set(0x3);
        CHECK(!F.try_wait(0x7, efWaitAll));
        CHECK(F.try_wait(0x2, efWaitAny, &r));
        CHECK_EQ(r, 0x3);
        CHECK_EQ(F.get(), 0x1);
        CHECK(F.try_wait(0x1, efWaitAny | efNoClear));
        CHECK_EQ(F.get(), 0x1);
    });
}

UT_TEST(event_flags, zero_mask_is_rejected)
{
    static TEventFlags F;
    ut::proc(0, []{
        F.set(0xFF);
        CHECK(!F.wait(0, efWaitAny, 5));
        CHECK(!F.wait(0, efWaitAll, 0));   // would otherwise be satisfied at once, or never
        CHECK(!F.try_wait(0, efWaitAll));
        CHECK_EQ(ut::ticks(), 0);
        CHECK_EQ(F.get(), 0xFF);
    });
}

UT_TEST(event_flags, set_isr)
{
    static TEventFlags F;
    ut::irq(0, []{ TISRW isr; F.set_isr(0x40); T("isr"); });
    ut::proc(0, []{ uint32_t r = 0; F.wait(0x40, efWaitAny, 0, &r); T("P0:%x", unsigned(r)); });
    ut::proc(1, []{ ut::raise(0); T("P1"); });
    ut::at_end([]{ CHECK_TRACE("isr P0:40 P1"); });
}

UT_TEST(event_flags, has_waiters)
{
    static TEventFlags F;
    ut::proc(0, []{ F.wait(0x1, efWaitAny, 0); });
    ut::proc(1, []{ CHECK(F.has_waiters()); F.set(0x1); CHECK(!F.has_waiters()); });
}

UT_TEST(event_flags, wake_up_aborts_wait)
{
    static TEventFlags F;
    ut::proc(0, []{ bool ok = F.wait(0x1, efWaitAny, 50); T("P0:%d@%u", ok, ut::ticks()); });
    ut::proc(1, []{ OS::sleep(4); ut::P(0).wake_up(); CHECK(!F.has_waiters()); });
    ut::at_end([]{ CHECK_TRACE("P0:0@4"); });
}

UT_TEST(event_flags, all_32_bits)
{
    static TEventFlags F;
    ut::proc(0, []{
        for(int i = 0; i < 32; ++i)
        {
            F.set(1u << i);
            CHECK(F.try_wait(1u << i, efWaitAll));
        }
        F.set(0xFFFFFFFF);
        CHECK(F.wait(0xFFFFFFFF, efWaitAll, 1));
        CHECK_EQ(F.get(), 0);
    });
}
