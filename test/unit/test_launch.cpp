//******************************************************************************
//*
//*     scmRTOS unit tests: OS::launch() (ext/launch), C++17
//*
//*     launch() creates all processes itself, so these tests are a separate
//*     program built with UT_EXTERNAL_PROCESSES and 3 processes. Each test
//*     calls OS::launch(), which never returns; a process ends the test with
//*     ut::finish().
//*
//******************************************************************************
#include "utest.h"
#include <launch.h>
#include <string.h>

using namespace OS;
#define T ut::trace

static_assert(scmRTOS_PROCESS_COUNT == 3, "launch tests use 3 processes");

UT_TEST(launch, priorities_by_position)
{
    launch(
        proc<64*1024>("a", []{ T("a"); for(;;) OS::sleep(); }),
        proc<64*1024>("b", []{ T("b"); for(;;) OS::sleep(); }),
        proc<64*1024>("c", []{
            T("c");
            CHECK_EQ(proc_by_idx(0).priority(), pr0);
            CHECK_EQ(proc_by_idx(1).priority(), pr1);
            CHECK_EQ(proc_by_idx(2).priority(), pr2);
            CHECK(&proc_by_prio(pr1) == &proc_by_idx(1));
            for(int k = 0; k < 3; ++k)          // the process objects are properly aligned
                CHECK_EQ(reinterpret_cast<uintptr_t>(&proc_by_idx(k)) % alignof(TBaseProcess), 0);
            CHECK_TRACE("a b c");
            ut::finish();
        })
    );
}

UT_TEST(launch, lambdas_with_captures)
{
    struct TCfg { int period; char tag[8]; };
    TCfg cfg = { 3, "slow" };
    int  base = 100;
    launch(
        proc<64*1024>("x", [cfg, base]{
            OS::sleep(cfg.period);
            T("%s:%d@%u", cfg.tag, base, ut::ticks());
            for(;;) OS::sleep();
        }),
        proc<64*1024>("y", [base]{ T("y:%d", base + 1); for(;;) OS::sleep(); }),
        proc<64*1024>("z", []{ OS::sleep(5); CHECK_TRACE("y:101 slow:100@3"); ut::finish(); })
    );
}

UT_TEST(launch, suspended_process_started_later)
{
    launch(
        proc<64*1024>("main", []{
            CHECK(proc_by_idx(1).is_suspended());
            OS::sleep(4);
            proc_by_prio(pr1).start();
            OS::sleep(1);
            CHECK_TRACE("lazy@4");
            ut::finish();
        }),
        proc<64*1024, pssSuspended>("lazy", []{ T("lazy@%u", ut::ticks()); for(;;) OS::sleep(); }),
        proc<64*1024>("idle-ish", []{ for(;;) OS::sleep(); })
    );
}

UT_TEST(launch, plain_function_bodies)
{
    struct F
    {
        static void a() { T("fa"); for(;;) OS::sleep(); }
        static void b() { T("fb"); OS::sleep(1); CHECK_TRACE("fa fb fc"); ut::finish(); }
        static void c() { T("fc"); for(;;) OS::sleep(); }
    };
    launch(proc<64*1024>("fa", F::a), proc<64*1024>("fb", F::b), proc<64*1024>("fc", F::c));
}

UT_TEST(launch, over_aligned_capture)
{
    struct alignas(64) TBlock { double v[4]; };
    TBlock blk = { { 1, 2, 3, 4 } };
    launch(
        proc<64*1024>("al", [blk]{
            CHECK_EQ(reinterpret_cast<uintptr_t>(&blk) % 64, 0);
            CHECK(blk.v[3] == 4);
            ut::finish();
        }),
        proc<64*1024>("p1", []{ for(;;) OS::sleep(); }),
        proc<64*1024>("p2", []{ for(;;) OS::sleep(); })
    );
}

#if scmRTOS_DEBUG_ENABLE == 1
UT_TEST(launch, debug_names_and_stack_sizes)
{
    launch(
        proc<64*1024>("first", []{
            CHECK(!strcmp(proc_by_idx(0).name(), "first"));
            CHECK(!strcmp(proc_by_idx(2).name(), "third"));
            CHECK_EQ(proc_by_idx(0).stack_size(), 64*1024 / sizeof(stack_item_t));
            // a size that is not a multiple of stack_item_t is rounded up
            CHECK_EQ(proc_by_idx(1).stack_size(), (64*1024 + 3 + sizeof(stack_item_t) - 1) / sizeof(stack_item_t));
            CHECK(proc_by_idx(2).stack_slack() > 0);
            ut::finish();
        }),
        proc<64*1024 + 3>("second", []{ for(;;) OS::sleep(); }),
        proc<64*1024>("third", []{ for(;;) OS::sleep(); })
    );
}
#endif
