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

UT_TEST(round_robin, rotation_takes_no_extra_ticks)
{
    // the next process starts in the same tick in which the previous slot ends
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 2);
        RR.register_process(ut::P(2), 1);
        RRActive = true;
    });
    ut::proc(1, []{ for(int i = 0; i < 4; ++i) { T("1@%u", ut::ticks()); ut::tick(); } });
    ut::proc(2, []{ for(int i = 0; i < 2; ++i) { T("2@%u", ut::ticks()); ut::tick(); } });
    ut::at_end([]{ CHECK_TRACE("1@0 1@1 2@2 1@3 1@4 2@5"); });
}

UT_TEST(round_robin, only_the_first_registered_process_is_active)
{
    // the first registered process gets the first slot even if the others
    // have higher priorities
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(3), 2);
        RR.register_process(ut::P(1), 2);
        RR.register_process(ut::P(2), 2);
        CHECK(ut::P(1).is_suspended());
        CHECK(ut::P(2).is_suspended());
        CHECK(!ut::P(3).is_suspended());
        RRActive = true;
    });
    ut::proc(1, []{ for(int i = 0; i < 2; ++i) { T("1"); ut::tick(); } });
    ut::proc(2, []{ for(int i = 0; i < 2; ++i) { T("2"); ut::tick(); } });
    ut::proc(3, []{ for(int i = 0; i < 2; ++i) { T("3"); ut::tick(); } });
    ut::at_end([]{ CHECK_TRACE("3 3 1 1 2 2"); });
}

UT_TEST(round_robin, sleeping_process_is_skipped_not_woken)
{
    // P2 (second in the list) sleeps inside its slot: the manager passes the
    // turn on and must not wake P2 when its turn comes round again
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 2);
        RR.register_process(ut::P(2), 2);
        RRActive = true;
    });
    ut::proc(1, []{ for(int i = 0; i < 8; ++i) { T("1"); ut::tick(); } });
    ut::proc(2, []{ unsigned t0 = ut::ticks(); OS::sleep(10); T("2:slept%u", ut::ticks() - t0); });
    ut::max_ticks(100);
    // P1: ticks 0, 1; P2 from tick 2: sleeps until 12; P1 goes on meanwhile
    ut::at_end([]{ CHECK_TRACE("1 1 1 1 1 1 1 1 2:slept10"); });
}

UT_TEST_DEFECT(round_robin, first_process_sleeping_in_its_first_slot_is_not_woken, "N8")
{
    // The same for the first registered process in its very first slot. Its
    // "put off by the manager" flag is still set from the constructor, so when
    // the turn comes back to it the manager makes it ready: out of its sleep.
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 2);
        RR.register_process(ut::P(2), 2);
        RRActive = true;
    });
    ut::proc(1, []{ unsigned t0 = ut::ticks(); OS::sleep(10); T("1:slept%u", ut::ticks() - t0); });
    ut::proc(2, []{ for(int i = 0; i < 6; ++i) { T("2"); ut::tick(); } });
    ut::max_ticks(100);
    ut::at_end([]{ CHECK_TRACE("2 2 2 2 2 2 1:slept10"); });
}

UT_TEST(round_robin, woken_inactive_higher_process_is_stopped_at_next_tick)
{
    // P1 is inactive and higher than the active P2. P2 wakes it by force: P1
    // preempts at once, but the manager takes it off again at the next tick.
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(2), 3);
        RR.register_process(ut::P(1), 3);
        RRActive = true;
    });
    ut::proc(1, []{ for(int i = 0; i < 3; ++i) { T("1@%u", ut::ticks()); ut::tick(); } });
    ut::proc(2, []{
        T("2@%u", ut::ticks());
        ut::P(1).force_wake_up();           // P1 runs one iteration, its tick puts it off
        T("2@%u", ut::ticks()); ut::tick();
        T("2@%u", ut::ticks()); ut::tick();
    });
    // P2's slot: ticks 0..2 (P1's stolen iteration ends at tick 1); then P1: 3, 4
    ut::at_end([]{ CHECK_TRACE("2@0 1@0 2@1 2@2 1@3 1@4"); });
}

UT_TEST(round_robin, single_process_gets_every_slot)
{
    // with one process in the list its slot is renewed each time it runs out
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{ RR.register_process(ut::P(1), 2); RRActive = true; });
    ut::proc(1, []{ for(int i = 0; i < 6; ++i) { T("1"); ut::tick(); } CHECK_EQ(ut::ticks(), 6); });
    ut::max_ticks(50);
    ut::at_end([]{ CHECK_TRACE("1 1 1 1 1 1"); });
}

UT_TEST(round_robin, process_whose_sleep_ended_while_inactive_gets_its_turn)
{
    // P2 sleeps inside its slot and its sleep ends during the slot of P1,
    // which is higher: P2 is ready but must wait. The manager stops it at the
    // next tick and has to remember that it owes P2 a turn.
    //   t0-t2  P1's slot              t2     P2's slot: sleeps until t4
    //   t3-t5  P1's slot (P2 is ready from t4, stopped at t5)
    //   t5-t7  P2's slot              t7-t9  P1's slot, then P1 alone
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 2);
        RR.register_process(ut::P(2), 2);
        RRActive = true;
    });
    ut::proc(1, []{ for(int i = 0; i < 12; ++i) { T("1"); ut::tick(); } });
    ut::proc(2, []{
        T("2");
        OS::sleep(2);
        T("2:woke@%u", ut::ticks());
        for(int i = 0; i < 2; ++i) { T("2"); ut::tick(); }
    });
    ut::max_ticks(100);
    ut::at_end([]{ CHECK_TRACE("1 1 2 1 1 2:woke@5 2 2 1 1 1 1 1 1 1 1"); });
}

UT_TEST(round_robin, woken_inactive_first_process_is_stopped_at_next_tick)
{
    // as above, but the process woken while inactive is the first of the list
    ut::on_systimer([]{ if(RRActive) RR.run(); });
    ut::proc(0, []{
        RR.register_process(ut::P(1), 2);
        RR.register_process(ut::P(2), 3);
        RRActive = true;
    });
    ut::proc(1, []{ for(int i = 0; i < 4; ++i) { T("1@%u", ut::ticks()); ut::tick(); } });
    ut::proc(2, []{
        T("2@%u", ut::ticks());
        ut::P(1).force_wake_up();           // P1 preempts, its next tick puts it off again
        T("2@%u", ut::ticks()); ut::tick();
        T("2@%u", ut::ticks()); ut::tick();
    });
    // P1: ticks 0, 1; P2's slot: 2..4 (P1's stolen iteration ends at tick 3); P1 again from 5
    ut::at_end([]{ CHECK_TRACE("1@0 1@1 2@2 1@2 2@3 2@4 1@5"); });
}
