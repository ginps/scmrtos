//******************************************************************************
//*     scmRTOS unit tests: TSoftwareTimer (ext/timer) - quick review checks
//******************************************************************************
#include "utest.h"
#include <timer.h>
#include <signal.h>
#include <setjmp.h>
#include <new>
#include <type_traits>

using namespace OS;

static int fired[8];
static void cnt(void* a) { ++fired[(intptr_t)a]; }

UT_TEST(timer, one_shot_fires_once_after_n_ticks)
{
    for (auto& f : fired) f = 0;
    TSoftwareTimer t(cnt, (void*)0, false);
    CHECK(t.start(3));
    TSoftwareTimer::service(); TSoftwareTimer::service();
    CHECK_EQ(fired[0], 0);
    TSoftwareTimer::service();
    CHECK_EQ(fired[0], 1);
    CHECK(!t.is_running());
    for (int i = 0; i < 5; ++i) TSoftwareTimer::service();
    CHECK_EQ(fired[0], 1);
}

UT_TEST(timer, periodic_fires_every_period)
{
    for (auto& f : fired) f = 0;
    TSoftwareTimer t(cnt, (void*)1, true);
    t.start(2);
    for (int i = 0; i < 10; ++i) TSoftwareTimer::service();
    CHECK_EQ(fired[1], 5);
    CHECK(t.stop());
    CHECK(!t.stop());
}

static TSoftwareTimer* self_t;
static void del_self(void*) { self_t->~TSoftwareTimer(); ++fired[2]; }
UT_TEST(timer, callback_may_destroy_own_periodic_timer)
{
    for (auto& f : fired) f = 0;
    alignas(TSoftwareTimer) static unsigned char buf[sizeof(TSoftwareTimer)];
    self_t = new (buf) TSoftwareTimer(del_self, nullptr, true);
    TSoftwareTimer other(cnt, (void*)3, true);
    other.start(1);
    self_t->start(1);
    for (int i = 0; i < 3; ++i) TSoftwareTimer::service();
    CHECK_EQ(fired[2], 1);
    CHECK_EQ(fired[3], 3);
}

// A copy of a running timer claims to be running but is not linked in.
// Either the copy is forbidden at compile time, or stop() on it must be safe.
static sigjmp_buf jb;
[[maybe_unused]] static void on_segv(int) { siglongjmp(jb, 1); }
template<typename Tm> static bool stop_copy_crashes()
{
    if constexpr (std::is_copy_constructible<Tm>::value)
    {
        Tm t(cnt, (void*)4, true);
        t.start(5);
        Tm c(t);
        struct sigaction sa{}, old{};
        sa.sa_handler = on_segv;
        sigaction(SIGSEGV, &sa, &old);
        volatile bool crashed = false;
        if (sigsetjmp(jb, 1) == 0) c.stop();    // walks off the end of the registry
        else crashed = true;
        sigaction(SIGSEGV, &old, nullptr);
        t.stop();
        return crashed;
    }
    return false;
}
UT_TEST(timer, copy_of_running_timer_is_forbidden_or_safe)
{
    CHECK(!stop_copy_crashes<TSoftwareTimer>());
}

UT_TEST(timer, null_callback_just_expires)
{
    for (auto& f : fired) f = 0;
    TSoftwareTimer n(nullptr, nullptr, false);
    TSoftwareTimer t(cnt, (void*)5, false);
    n.start(1);
    t.start(1);
    TSoftwareTimer::service();
    CHECK(!n.is_running());
    CHECK_EQ(fired[5], 1);
}
