//******************************************************************************
//*
//*     scmRTOS unit tests: WatchdogExtension (ext/watchdog)
//*
//*     WatchdogExtension::tick() is driven from the system timer hook. The
//*     reboot hook records the event and ends the test.
//*
//******************************************************************************
#include "utest.h"
#include <watch_dog_os_ext.h>
#include <new>
#include <string.h>

using namespace OS;
#define T ut::trace

namespace
{
    WatchdogExtension * Wdg;
    unsigned Inits, Feeds;
    int      RebootPrio = -1;
    unsigned RebootTick;
    unsigned TimerIrqs;     // system timer interrupts so far; the hook runs before
                            // the kernel increments the tick count, so count them here

    void start_watchdog()
    {
        static WatchdogExtension w;         // constructed in the test's own process
        Wdg = &w;
        ut::on_systimer([]{ ++TimerIrqs; Wdg->tick(); });
    }
}

namespace OS
{
    void wdg_init_user_hook()            { ++Inits; }
    void wdg_feed_user_hook()            { ++Feeds; }
    void wdg_force_reboot_user_hook(const TPriority pr)
    {
        RebootPrio = pr;
        RebootTick = TimerIrqs;
        ut::finish();                       // the extension never returns from a reboot
    }
}

UT_TEST(watchdog, init_hook_called_once)
{
    start_watchdog();
    CHECK_EQ(Inits, 1);
}

UT_TEST(watchdog, fed_process_never_reboots)
{
    start_watchdog();
    Wdg->register_process(ut::P(1), 5);
    ut::proc(1, []{ for(int i = 0; i < 30; ++i) { OS::sleep(4); Wdg->alive(); } });
    ut::at_end([]{ CHECK_EQ(RebootPrio, -1); CHECK_EQ(Feeds, 120); });
}

UT_TEST(watchdog, missed_alive_reboots_at_timeout)
{
    start_watchdog();
    Wdg->register_process(ut::P(1), 5);
    ut::proc(1, []{ OS::sleep(2); Wdg->alive(); OS::sleep(100); }, ut::DAEMON);
    ut::max_ticks(200);
    ut::at_end([]{
        CHECK_EQ(RebootPrio, ut::prio(1));
        CHECK_EQ(RebootTick, 7);            // last alive() at tick 2, timeout 5
    });
}

UT_TEST(watchdog, asleep_pauses_monitoring)
{
    start_watchdog();
    Wdg->register_process(ut::P(1), 5);
    ut::proc(1, []{
        OS::sleep(2);                       // 3 ticks left
        Wdg->asleep();
        OS::sleep(50);                      // not monitored
        Wdg->alive();                       // tick 52: monitoring resumes, 3 ticks left
        OS::sleep(2);
        Wdg->alive();                       // tick 54: reloaded to 5
        OS::sleep(100);
    }, ut::DAEMON);
    ut::max_ticks(300);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(1)); CHECK_EQ(RebootTick, 59); });
}

UT_TEST(watchdog, asleep_twice_reboots_at_once)
{
    start_watchdog();
    Wdg->register_process(ut::P(2), 10);
    ut::proc(2, []{ OS::sleep(3); Wdg->asleep(); Wdg->asleep(); T("unreachable"); }, ut::DAEMON);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(2)); CHECK_EQ(RebootTick, 3); CHECK_TRACE(""); });
}

UT_TEST(watchdog, the_starving_process_is_reported)
{
    start_watchdog();
    Wdg->register_process(ut::P(0), 10);
    Wdg->register_process(ut::P(3), 4);
    ut::proc(0, []{ for(;;) { OS::sleep(3); Wdg->alive(); } }, ut::DAEMON);
    ut::proc(3, []{ OS::sleep(1000); }, ut::DAEMON);
    ut::max_ticks(100);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(3)); CHECK_EQ(RebootTick, 4); });
}

UT_TEST(watchdog, highest_and_lowest_process_are_monitored)
{
    // nobody is "asleep" initially, whatever its priority
    start_watchdog();
    Wdg->register_process(ut::P(0), 6);
    Wdg->register_process(ut::P(4), 9);
    ut::proc(0, []{ OS::sleep(1000); }, ut::DAEMON);
    ut::proc(4, []{ OS::sleep(1000); }, ut::DAEMON);
    ut::max_ticks(100);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(0)); CHECK_EQ(RebootTick, 6); });
}

UT_TEST(watchdog, lowest_process_is_monitored)
{
    start_watchdog();
    Wdg->register_process(ut::P(4), 9);
    ut::proc(4, []{ OS::sleep(1000); }, ut::DAEMON);
    ut::max_ticks(100);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(4)); CHECK_EQ(RebootTick, 9); });
}

UT_TEST(watchdog, constructor_clears_the_table)
{
    // an object that is not static: its memory is not zero beforehand
    alignas(WatchdogExtension) static unsigned char raw[sizeof(WatchdogExtension)];
    memset(raw, 0xA5, sizeof(raw));
    Wdg = new (raw) WatchdogExtension;
    ut::on_systimer([]{ ++TimerIrqs; Wdg->tick(); });
    Wdg->register_process(ut::P(1), 4);
    ut::proc(1, []{ for(int i = 0; i < 10; ++i) { OS::sleep(3); Wdg->alive(); } });
    ut::at_end([]{ CHECK_EQ(RebootPrio, -1); CHECK_EQ(Feeds, 30); });
}

UT_TEST(watchdog, unregistered_processes_are_not_monitored)
{
    start_watchdog();
    Wdg->register_process(ut::P(1), 3);
    ut::proc(1, []{ for(int i = 0; i < 10; ++i) { OS::sleep(2); Wdg->alive(); } });
    ut::proc(2, []{ OS::sleep(15); });      // never calls alive(), not registered
    ut::at_end([]{ CHECK_EQ(RebootPrio, -1); });
}

UT_TEST(watchdog, zero_timeout_registration_reboots_at_next_tick)
{
    // a zero timeout is treated as corrupted state
    start_watchdog();
    Wdg->register_process(ut::P(1), 0);
    ut::proc(1, []{ OS::sleep(50); }, ut::DAEMON);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(1)); CHECK_EQ(RebootTick, 1); });
}

UT_TEST_DEFECT(watchdog, alive_from_unregistered_process, "N3")
{
    start_watchdog();
    ut::proc(1, []{ Wdg->alive(); T("unreachable"); }, ut::DAEMON);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(1)); });
}

UT_TEST_DEFECT(watchdog, asleep_from_unregistered_process, "N3")
{
    start_watchdog();
    ut::proc(1, []{ Wdg->asleep(); T("unreachable"); }, ut::DAEMON);
    ut::at_end([]{ CHECK_EQ(RebootPrio, ut::prio(1)); });
}
