//******************************************************************************
//*
//*     scmRTOS unit tests: defects of the v5.2.0 and develop reports that had
//*     no test ("by code" in the reports), written together with the fixes
//*
//******************************************************************************
#include "utest.h"
#include <string.h>
#include <round-robin.h>

using namespace OS;
using usr::ring_buffer;
#define T ut::trace

//------------------------------------------------------------------------------
//  13: pop from an empty ring_buffer returns a stale element
UT_TEST_DEFECT(ring_buffer, pop_from_empty_returns_no_stale_item, "13")
{
    static ring_buffer<int, 2> b;
    b.push_back(7); b.push_back(8);
    CHECK_EQ(b.pop_front(), 7);
    CHECK_EQ(b.pop_front(), 8);
    CHECK_EQ(b.get_count(), 0);
    CHECK_EQ(b.pop_front(), 0);             // value-initialized, not Buf[First] == 7
    CHECK_EQ(b.pop_back(), 0);
    CHECK_EQ(b.get_count(), 0);
}

//------------------------------------------------------------------------------
//  7: channel::write()/read() with more items than the channel holds
UT_TEST_DEFECT(channel, write_more_than_capacity_completes, "7")
{
    static channel<int, 4> C;
    static int got[6];
    ut::proc(0, []{
        const int d[6] = { 1, 2, 3, 4, 5, 6 };
        C.write(d, 6);                      // 6 > 4: passed on in portions
        T("w@%u", ut::ticks());
    });
    ut::proc(1, []{
        for(int i = 0; i < 6; ++i)
            CHECK(C.pop(got[i], 10));
        T("r@%u", ut::ticks());
    });
    ut::at_end([]{
        CHECK_TRACE("w@0 r@0");
        for(int i = 0; i < 6; ++i) CHECK_EQ(got[i], i + 1);
    });
}

UT_TEST_DEFECT(channel, read_more_than_capacity_fails_at_once, "7")
{
    static channel<int, 4> C;
    ut::proc(0, []{
        int d[6];
        CHECK(!C.read(d, 6, 0));            // can never be satisfied: no endless wait
        CHECK(!C.read(d, 6, 5));
        T("r@%u", ut::ticks());
    });
    ut::at_end([]{ CHECK_TRACE("r@0"); });
}

//------------------------------------------------------------------------------
//  15: round_robin_mgr::register_process() past proc_count
namespace { round_robin_mgr<1> RR1; }
UT_TEST_DEFECT(round_robin, register_past_capacity_is_refused, "15")
{
    ut::proc(0, []{
        RR1.register_process(ut::P(1));     // fills the only slot
        RR1.register_process(ut::P(2));     // no slot: must change nothing
        T("reg");
    });
    ut::proc(1, []{ T("P1"); });
    ut::proc(2, []{ T("P2"); });            // stays ready, runs
    ut::at_end([]{ CHECK_TRACE("reg P1 P2"); });
}

//------------------------------------------------------------------------------
//  10: services called before OS::run() (the set-up part of a test runs there)
UT_TEST_DEFECT(kernel, services_before_run_are_harmless, "10")
{
    static channel<int, 2> C;
    static TMutex M;
    static TEventFlag F;
    CHECK(!OS::os_running());
    OS::sleep(5);                           // cannot wait: returns at once
    int x = 0;
    CHECK(!C.pop(x, 3));                    // empty: fails like a timeout
    C.push(1);                              // no consumer: no context switch
    M.lock();                               // free mutex: taken
    M.unlock();
    F.signal();
    ut::proc(0, []{
        CHECK(!M.is_locked());
        CHECK(F.wait(1));
        int y = 0;
        CHECK(C.pop(y, 1));
        CHECK_EQ(y, 1);
        T("P0");
    });
    ut::proc(1, []{ T("P1"); });            // every process is ready at start
    ut::at_end([]{ CHECK_TRACE("P0 P1"); });
}

//------------------------------------------------------------------------------
//  9: nothing ready - the idle process blocks (a service called from the
//  idle hook); the scheduler must not search an empty ready map
UT_TEST_DEFECT(kernel, idle_process_never_blocks, "9")
{
    static volatile int IdleCalls;
    ut::on_idle([]{ if(IdleCalls++ == 0) OS::sleep(3); });     // a usage error, but must not hang the scheduler
    ut::proc(0, []{ OS::sleep(5); T("P0@%u", ut::ticks()); });
    ut::at_end([]{ CHECK_TRACE("P0@5"); });
}

//------------------------------------------------------------------------------
//  8: process::terminate() of the running process re-initializes the stack
//  it runs on; it must be refused
#if scmRTOS_PROCESS_RESTART_ENABLE == 1
UT_TEST_DEFECT(kernel, terminate_of_running_process_is_refused, "8")
{
    ut::proc(1, []{
        ut::terminate(1);                   // P1 terminates itself
        CHECK(!ut::P(1).is_suspended());    // refused: still ready, stack intact
        OS::sleep(2);
        T("P1@%u", ut::ticks());
    });
    ut::at_end([]{ CHECK_TRACE("P1@2"); });
}
#endif

