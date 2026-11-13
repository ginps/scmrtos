//******************************************************************************
//*
//*     scmRTOS unit tests: process count and process map width
//*
//*     A separate program built for scmRTOS_PROCESS_COUNT = 1, 7, 8, 15, 16
//*     and 31: the boundaries where TProcessMap changes its type and where the
//*     map is completely filled. All N processes take part in every test.
//*
//*     P(0) has the highest priority. The lowest process P(N-1) drives the
//*     test and ends it with ut::finish().
//*
//******************************************************************************
#include "utest.h"
#include <stdio.h>

using namespace OS;

namespace
{
    const int N = scmRTOS_PROCESS_COUNT;

    constexpr TPriority prio_of(int idx)
    {
        return static_cast<TPriority>(scmRTOS_PRIORITY_ORDER ? N - idx : idx);
    }
    constexpr int index_of(TPriority pr)
    {
        return scmRTOS_PRIORITY_ORDER ? N - static_cast<int>(pr) : static_cast<int>(pr);
    }

    void (*Body)(int idx);                      // what every process runs in the current test

    // N process objects: P<N-1> ... P<0>
    template<int I> struct TProcs : TProcs<I - 1>
    {
        OS::process<prio_of(I - 1), 32*1024> proc;
    };
    template<> struct TProcs<0> { };
    TProcs<N> Procs;

    TBaseProcess & P(int idx) { return const_cast<TBaseProcess &>(*get_proc(prio_of(idx))); }

    int      Order[64];
    unsigned Tick[64];
    volatile int Count;

    void record(int idx)
    {
        TCritSect cs;
        Order[Count] = idx; Tick[Count] = ut::ticks(); Count = Count + 1;
    }

    void check_order(bool ascending, int count)
    {
        CHECK_EQ(Count, count);
        for(int k = 0; k < Count && k < count; ++k)
            CHECK_EQ(Order[k], ascending ? k : count - 1 - k);
    }

    NORETURN void start(void (*body)(int))
    {
        Body = body;
        ut::max_ticks(5000);
        OS::run();
    }
}

// the process function of all N processes
namespace OS
{
    template<TPriority pr, size_t stk_size, TProcessStartState pss>
    void process<pr, stk_size, pss>::exec()
    {
        Body(index_of(pr));
        for(;;) OS::sleep();
    }
}

static_assert(sizeof(TProcessMap) * 8 >= static_cast<unsigned>(N) + 1, "process map too narrow");

UT_TEST(wide, all_processes_start_by_priority)
{
    start([](int i){
        record(i);
        if(i == N - 1) { check_order(true, N); ut::finish(); }
    });
}

UT_TEST(wide, staggered_sleeps_wake_in_time_order)
{
    // P(i) sleeps N - i ticks: the lowest wakes first, the highest last
    start([](int i){
        OS::sleep(N - i);
        record(i);
        CHECK_EQ(ut::ticks(), unsigned(N - i));
        if(i == 0) { check_order(false, N); ut::finish(); }
    });
}

UT_TEST(wide, same_tick_wakeups_run_by_priority)
{
    start([](int i){
        OS::sleep(3);
        record(i);
        CHECK_EQ(ut::ticks(), 3);
        if(i == N - 1) { check_order(true, N); ut::finish(); }
    });
}

UT_TEST(wide, event_flag_wakes_every_waiter)
{
    static TEventFlag Flag;
    start([](int i){
        if(i < N - 1 || N == 1)
        {
            if(N > 1) CHECK(Flag.wait(100));
            record(i);
            if(N == 1) ut::finish();
        }
        else
        {
            Flag.signal();                      // all N-1 waiters run before this returns
            check_order(true, N - 1);
            CHECK(!Flag.is_signaled());
            ut::finish();
        }
    });
}

UT_TEST(wide, mutex_handover_by_priority)
{
    static TMutex M;
    start([](int i){
        if(i == N - 1)
        {
            M.lock();
            OS::sleep(2);                       // everybody else queues up at tick 1
            M.unlock();                         // each waiter locks, records, unlocks
            check_order(true, N - 1);
            CHECK(!M.is_locked());
            ut::finish();
        }
        else
        {
            OS::sleep(1);
            M.lock();
            record(i);
            M.unlock();
        }
    });
}

UT_TEST(wide, channel_feeds_every_consumer)
{
    static channel<int, 4> Ch;
    start([](int i){
        if(i == N - 1)
        {
            for(int k = 0; k < N - 1; ++k) Ch.push(k);     // one item per waiting consumer
            check_order(true, N - 1);
            CHECK_EQ(Ch.get_count(), 0);
            ut::finish();
        }
        else
        {
            int x = -1;
            CHECK(Ch.pop(x, 100));
            CHECK_EQ(x, i);                     // the highest waiting consumer takes each item
            record(i);
        }
    });
}

UT_TEST(wide, timeouts_of_all_processes_expire)
{
    static TEventFlag Never;
    start([](int i){
        CHECK(!Never.wait(i + 1));
        CHECK_EQ(ut::ticks(), unsigned(i + 1));
        record(i);
        if(i == N - 1) { check_order(true, N); ut::finish(); }
    });
}

UT_TEST(wide, forced_wakeup_of_each_process)
{
    start([](int i){
        if(i == N - 1)
        {
            for(int k = N - 2; k >= 0; --k)
            {
                CHECK(P(k).is_suspended());
                P(k).force_wake_up();           // P(k) runs at once: it is higher
            }
            check_order(false, N - 1);
            ut::finish();
        }
        else
        {
            OS::sleep();
            record(i);
        }
    });
}
