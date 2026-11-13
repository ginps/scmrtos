//******************************************************************************
//*
//*     scmRTOS unit tests: TProfiler and process_profiler (ext/profiler)
//*
//*     time_interval() is supplied by the user; here it returns a value the
//*     test controls.
//*
//******************************************************************************
#include "utest.h"
#include <profiler.h>

using namespace OS;

namespace { volatile uint32_t Interval; }

template<> uint32_t TProfiler<0>::time_interval()            { return Interval; }
template<> uint32_t TProfiler<4>::time_interval()            { return Interval; }
#ifndef UT_V52
template<> uint32_t process_profiler<float>::time_interval()    { return Interval; }
template<> uint32_t process_profiler<uint32_t>::time_interval() { return Interval; }
#endif

UT_TEST(profiler, shares_in_hundredths_of_percent)
{
    static TProfiler<0> Prof;
    ut::proc(0, []{ Interval = 300; Prof.advance_counters(); });
    ut::proc(1, []{ Interval = 100; Prof.advance_counters(); Prof.process_data(); });
    ut::at_end([]{
        CHECK_EQ(Prof.get_result(ut::prio(0)), 7500);
        CHECK_EQ(Prof.get_result(ut::prio(1)), 2500);
        CHECK_EQ(Prof.get_result(ut::prio(2)), 0);
        CHECK_EQ(Prof.get_result(prIDLE), 0);
    });
}

UT_TEST(profiler, counters_reset_after_processing)
{
    static TProfiler<0> Prof;
    ut::proc(0, []{
        Interval = 10; Prof.advance_counters();
        Prof.process_data();
        CHECK_EQ(Prof.get_result(ut::prio(0)), 10000);
        OS::sleep(1);
        Interval = 30; Prof.advance_counters();
    });
    ut::proc(1, []{
        OS::sleep(2);
        Interval = 10; Prof.advance_counters();
        Prof.process_data();                // only the new period: 30 vs 10
        CHECK_EQ(Prof.get_result(ut::prio(0)), 7500);
        CHECK_EQ(Prof.get_result(ut::prio(1)), 2500);
    });
}

UT_TEST(profiler, sum_shift_handles_large_counts)
{
    static TProfiler<4> Prof;
    ut::proc(0, []{ Interval = 3000000; Prof.advance_counters(); });
    ut::proc(1, []{ Interval = 1000000; Prof.advance_counters(); Prof.process_data(); });
    ut::at_end([]{
        CHECK_EQ(Prof.get_result(ut::prio(0)), 7500);
        CHECK_EQ(Prof.get_result(ut::prio(1)), 2500);
    });
}

UT_TEST(profiler, driven_by_context_switch_hook)
{
    // one unit per switch, charged to the process that is switched out
    static TProfiler<0> Prof;
    ut::on_context_switch([]{ Interval = 1; Prof.advance_counters(); });
    ut::proc(0, []{ for(int i = 0; i < 10; ++i) OS::sleep(1); });
    ut::proc(1, []{ for(int i = 0; i < 10; ++i) OS::sleep(1); });
    ut::proc(2, []{
        OS::sleep(11);
        Prof.process_data();
        unsigned sum = 0;
        for(unsigned i = 0; i < OS::PROCESS_COUNT; ++i) sum += Prof.get_result(i);
        CHECK(sum <= 10000 && sum >= 10000 - OS::PROCESS_COUNT);    // integer rounding
        CHECK(Prof.get_result(ut::prio(0)) > 0);
        CHECK(Prof.get_result(prIDLE) > 0);
    });
}

UT_TEST_DEFECT(profiler, no_time_elapsed, "N4")
{
    static TProfiler<0> Prof;
    ut::proc(0, []{ Prof.process_data(); CHECK_EQ(Prof.get_result(ut::prio(0)), 0); });
}

#ifndef UT_V52     // process_profiler is new in develop
UT_TEST(profiler, generic_float_percent)
{
    static process_profiler<float> Prof;
    ut::proc(0, []{ Interval = 3; Prof.advance_counters(); });
    ut::proc(1, []{ Interval = 1; Prof.advance_counters(); Prof.process_data(); });
    ut::at_end([]{
        CHECK(Prof.get_result(ut::prio(0)) == 75.0f);
        CHECK(Prof.get_result(ut::prio(1)) == 25.0f);
        CHECK(Prof.get_result(ut::prio(3)) == 0.0f);
    });
}

UT_TEST(profiler, generic_integral_hundredths)
{
    static process_profiler<uint32_t> Prof;
    ut::proc(0, []{ Interval = 2; Prof.advance_counters(); });
    ut::proc(1, []{ Interval = 1; Prof.advance_counters(); Prof.process_data(); });
    ut::at_end([]{
        CHECK_EQ(Prof.get_result(ut::prio(0)), 6666);
        CHECK_EQ(Prof.get_result(ut::prio(1)), 3333);
    });
}

UT_TEST(profiler, generic_counters_reset_after_processing)
{
    static process_profiler<uint32_t> Prof;
    ut::proc(0, []{
        Interval = 10; Prof.advance_counters();
        Prof.process_data();
        CHECK_EQ(Prof.get_result(ut::prio(0)), 10000);
        OS::sleep(1);
        Interval = 30; Prof.advance_counters();
    });
    ut::proc(1, []{
        OS::sleep(2);
        Interval = 10; Prof.advance_counters();
        Prof.process_data();                // only the new period: 30 vs 10
        CHECK_EQ(Prof.get_result(ut::prio(0)), 7500);
        CHECK_EQ(Prof.get_result(ut::prio(1)), 2500);
    });
}

UT_TEST_DEFECT(profiler, generic_no_time_elapsed, "N4")
{
    static process_profiler<uint32_t> Prof;
    ut::proc(0, []{ Prof.process_data(); CHECK_EQ(Prof.get_result(ut::prio(0)), 0); });
}
#endif // UT_V52

// process_data() takes the counters and resets them in one step. The timer
// runs from a real timer and adds 1 to the counter of the running process P0
// at any instruction. P0 sets the counters to a known state, calls
// process_data() and reads what is left; the time taken by process_data()
// (recovered from the result) plus what is left must equal the interrupts
// that came in. An increment between the copy and the reset would be lost.
namespace
{
    struct TProfProbe : public TProfiler<0>
    {
        volatile uint32_t *raw() { return Counter; }
    };
#ifndef UT_V52
    struct TGenProbe : public process_profiler<uint32_t>
    {
        volatile uint32_t *raw() { return counters; }
    };
#endif
    volatile unsigned ProfIrqs;
    const uint32_t    OTHER = 9990;     // result * (OTHER + n) / 10000 == n for n <= 10

    template<typename P> void check_no_lost_counts(P &Prof)
    {
        const uint_fast8_t me = ut::prio(0), other = ut::prio(1);
        unsigned Lost = 0, Seen = 0;
        while(ProfIrqs < 20000)
        {
            unsigned before;
            {
                TCritSect cs;
                for(uint_fast8_t i = 0; i < OS::PROCESS_COUNT; ++i) Prof.raw()[i] = 0;
                Prof.raw()[other] = OTHER;
                before = ProfIrqs;
            }
            Prof.process_data();
            unsigned left, after;
            {
                TCritSect cs;
                left  = Prof.raw()[me];
                after = ProfIrqs;
            }
            unsigned taken = unsigned(Prof.get_result(me));
            if(after != before) ++Seen;
            if(after - before > 10) continue;       // result no longer exact
            if(taken + left != after - before) ++Lost;
        }
        CHECK_EQ(Lost, 0);
        CHECK(Seen > 0);
    }
}

UT_TEST(profiler, process_data_loses_no_counts_under_interrupts)
{
    static TProfProbe Prof;
    ut::async_ticks(25);
    ut::max_ticks(200000);
    ut::on_systimer([]{ Interval = 1; Prof.advance_counters(); ProfIrqs = ProfIrqs + 1; });
    ut::proc(0, []{ check_no_lost_counts(Prof); });
}

#ifndef UT_V52
UT_TEST(profiler, generic_process_data_loses_no_counts_under_interrupts)
{
    static TGenProbe Prof;
    ut::async_ticks(25);
    ut::max_ticks(200000);
    ut::on_systimer([]{ Interval = 1; Prof.advance_counters(); ProfIrqs = ProfIrqs + 1; });
    ut::proc(0, []{ check_no_lost_counts(Prof); });
}
#endif
