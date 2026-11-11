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

UT_TEST_DEFECT(profiler, generic_no_time_elapsed, "N4")
{
    static process_profiler<uint32_t> Prof;
    ut::proc(0, []{ Prof.process_data(); CHECK_EQ(Prof.get_result(ut::prio(0)), 0); });
}
#endif // UT_V52
