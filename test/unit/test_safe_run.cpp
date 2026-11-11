//******************************************************************************
//*
//*     scmRTOS unit tests: safe_run / ProcListCheck (ext/safe-run)
//*
//*     The checks are compile-time; the static_asserts below are the test.
//*     The run-time test starts the kernel with OS::safe_run().
//*
//******************************************************************************
#include "utest.h"
#include <safe_run.h>

using namespace OS;

namespace
{
    typedef process<pr0, 64*1024>                   A0;
    typedef process<pr1, 32*1024>                   A1;     // stack size does not matter
    typedef process<pr2, 64*1024>                   A2;
    typedef process<pr3, 64*1024>                   A3;
    typedef process<pr4, 64*1024, pssSuspended>     A4;     // nor the start state
    typedef process<prIDLE, 64*1024>                AI;

    static_assert( ProcListCheck<A0, A1, A2, A3, A4>::check(),     "complete set");
    static_assert( ProcListCheck<A4, A2, A0, A3, A1>::check(),     "order does not matter");
    static_assert(!ProcListCheck<A0, A1, A2, A3>::check(),         "missing priority");
    static_assert(!ProcListCheck<A0, A1, A2, A3, A3>::check(),     "duplicated priority");
    static_assert(!ProcListCheck<A0, A1, A2, A3, A4, A4>::check(), "duplicate on top of a full set");
    static_assert(!ProcListCheck<A0, A1, A2, A3, AI>::check(),     "idle priority instead of pr4");
    static_assert(!ProcListCheck<A0, A1, A2, A3, A4, AI>::check(), "idle priority in addition");
    static_assert(!ProcListCheck<>::check(),                       "empty list");
}

UT_TEST(safe_run, compile_time_checks)
{
    // the static_asserts above passed, or this file would not compile;
    // repeat them at run time to have them listed in the report
    CHECK((ProcListCheck<A0, A1, A2, A3, A4>::check()));
    CHECK((!ProcListCheck<A0, A1, A2, A3>::check()));
    CHECK((!ProcListCheck<A0, A1, A2, A3, A3>::check()));
    CHECK((!ProcListCheck<A0, A1, A2, A3, AI>::check()));
}

UT_TEST(safe_run, starts_the_kernel)
{
    ut::use_safe_run();
    ut::proc(0, []{ CHECK(os_running()); ut::trace("P0"); });
    ut::proc(4, []{ ut::trace("P4"); });
    ut::at_end([]{ CHECK_TRACE("P0 P4"); });
}
