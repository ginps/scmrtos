//******************************************************************************
//*
//*     scmRTOS unit tests: round_robin_mgr (ext/round-robin)
//*
//******************************************************************************
#include "utest.h"
#include <round-robin.h>

using namespace OS;
#define T ut::trace

namespace
{
    round_robin_mgr<3> RR;
    volatile bool      RRActive;
}

UT_TEST(round_robin, rotates_by_time_slices)
{
    // P1, P2, P3 never block; time slices 2, 1 and 3 ticks. Each iteration
    // traces the process and ticks the timer, so rotation happens inside tick().
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 2);   // the first one stays active
        RR.register_process(ut::P(2), 1);
        RR.register_process(ut::P(3), 3);
        RRActive = true;
    });
    ut::proc(1, []{ for(int i = 0; i < 4; ++i) { T("1"); ut::tick(); } });
    ut::proc(2, []{ for(int i = 0; i < 2; ++i) { T("2"); ut::tick(); } });
    ut::proc(3, []{ for(int i = 0; i < 6; ++i) { T("3"); ut::tick(); } });
    ut::at_end([]{ CHECK_TRACE("1 1 2 3 3 3 1 1 2 3 3 3"); });
}

UT_TEST(round_robin, finished_process_passes_its_turn)
{
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 1);
        RR.register_process(ut::P(2), 1);
        RR.register_process(ut::P(3), 1);
        RRActive = true;
    });
    ut::proc(1, []{ T("1"); ut::tick(); });                                 // done after one turn
    ut::proc(2, []{ for(int i = 0; i < 3; ++i) { T("2"); ut::tick(); } });
    ut::proc(3, []{ for(int i = 0; i < 3; ++i) { T("3"); ut::tick(); } });
    ut::at_end([]{ CHECK_TRACE("1 2 3 2 3 2 3"); });
}

UT_TEST(round_robin, externally_woken_inactive_process_is_put_off)
{
    // P2 is inactive; P1 wakes it with force_wake_up() during P1's slot.
    // P2 is lower than P1, so it does not run; the manager suspends it again at
    // the next tick, and P2 runs only in its own turn. After both finish, the
    // manager finds nobody to activate (P0 keeps the test running).
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 3);
        RR.register_process(ut::P(2), 3);
        RRActive = true;
        OS::sleep(20);
    });
    ut::proc(1, []{
        T("1"); ut::P(2).force_wake_up(); ut::tick();
        T("1"); ut::tick();
        T("1"); ut::tick();
    });
    ut::proc(2, []{ for(int i = 0; i < 3; ++i) { T("2"); ut::tick(); } });
    ut::at_end([]{ CHECK_TRACE("1 1 1 2 2 2"); });
}
