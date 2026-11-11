//******************************************************************************
//*
//*     scmRTOS unit tests: minimal test framework
//*
//*     Runs on the Linux test port (port/linux/gcc) in manual tick mode.
//*
//*     Every test runs in its own forked Linux process, so a test that hangs,
//*     crashes or corrupts the kernel cannot affect the others.
//*
//*     A test body (UT_TEST) is the set-up: it runs before OS::run() and
//*     assigns bodies to the RTOS processes P0..P4 (P0 has the highest
//*     priority), optional interrupt handlers and an end check. Then the
//*     framework starts the kernel. The test ends when every assigned body
//*     returns, or when a body calls ut::finish(). The system timer ticks only
//*     when all processes are blocked (from the idle process), or when a body
//*     calls ut::tick() - so every test is deterministic.
//*
//*     A test without process bodies is a plain unit test: it runs in main()
//*     and the kernel is not started.
//*
//******************************************************************************
#ifndef UTEST_H
#define UTEST_H

#include <scmRTOS.h>

namespace ut
{
    typedef void (*fn_t)();

    const int PROC_COUNT = scmRTOS_PROCESS_COUNT;          // P0..P4

    enum TMode
    {
        MUST_FINISH,      // the test waits for this body to return
        DAEMON,           // may block forever; the test does not wait for it
        SUSPENDED         // like MUST_FINISH, but the process starts suspended (P4 only)
    };

    //--------------------------------------------------------------------------
    //  Set-up (call from the UT_TEST body)
    //
    void proc(int idx, fn_t body, TMode mode = MUST_FINISH);   // body of process P<idx>
    void irq(int n, fn_t handler);                             // user interrupt n (0..7) handler
    void at_end(fn_t check);                                   // runs when the test ends
    void on_systimer(fn_t hook);                               // called in every system timer interrupt
    void on_context_switch(fn_t hook);                         // called on every context switch
    void max_ticks(unsigned n);                                // tick limit, default 2000
    void use_safe_run();                                       // start with OS::safe_run() instead of OS::run()

    //--------------------------------------------------------------------------
    //  Run time (call from processes or interrupt handlers)
    //
    void      finish();                                        // end the test now
    void      tick(unsigned n = 1);                            // n system timer ticks right here
    unsigned  ticks();                                         // OS::get_tick_count()
    unsigned  switches();                                      // context switches so far
    void      raise(int n);                                    // request user interrupt n
    OS::TBaseProcess & P(int idx);                             // process P<idx>
    OS::TPriority      prio(int idx);                          // priority of P<idx>
    bool      done(int idx);                                   // body of P<idx> has returned
    extern bool P3StartedViaFunction;                          // P3 is constructed with a function (develop)
#if scmRTOS_PROCESS_RESTART_ENABLE == 1
    void      terminate(int idx, fn_t new_body = 0);           // OS::process::terminate()
#endif

    //--------------------------------------------------------------------------
    //  Trace: tokens separated by single spaces, checked with CHECK_TRACE
    //
    void        trace(const char * fmt, ...) __attribute__((format(printf, 1, 2)));
    const char* trace_str();
    void        trace_clear();

    //--------------------------------------------------------------------------
    //  Checks (use the macros below)
    //
    bool check     (bool ok, const char * expr, const char * file, int line);
    bool check_eq  (long long a, long long b, const char * ea, const char * eb, const char * file, int line);
    bool check_str (const char * a, const char * b, const char * ea, const char * file, int line);

    //--------------------------------------------------------------------------
    //  Registration
    //
    struct TTest
    {
        const char * suite;
        const char * name;
        fn_t         body;
        const char * defect;        // id of a known defect this test exposes, or 0
        TTest      * next;
    };
    struct Registrar { Registrar(TTest * t); };
}

#define UT_CAT_(a, b) a##b
#define UT_CAT(a, b)  UT_CAT_(a, b)

#define UT_TEST_IMPL(suite, name, defect)                                                    \
    static void UT_CAT(ut_body_, UT_CAT(suite, UT_CAT(_, name)))();                          \
    static ut::TTest UT_CAT(ut_test_, UT_CAT(suite, UT_CAT(_, name))) =                      \
        { #suite, #name, UT_CAT(ut_body_, UT_CAT(suite, UT_CAT(_, name))), defect, 0 };      \
    static ut::Registrar UT_CAT(ut_reg_, UT_CAT(suite, UT_CAT(_, name)))                     \
        (&UT_CAT(ut_test_, UT_CAT(suite, UT_CAT(_, name))));                                 \
    static void UT_CAT(ut_body_, UT_CAT(suite, UT_CAT(_, name)))()

// Ordinary test.
#define UT_TEST(suite, name)            UT_TEST_IMPL(suite, name, 0)

// Test that exposes a known defect: its failure is reported as expected,
// its success as "defect fixed". 'id' refers to the analysis report.
#define UT_TEST_DEFECT(suite, name, id) UT_TEST_IMPL(suite, name, id)

#define CHECK(c)          ut::check((c), #c, __FILE__, __LINE__)
#define CHECK_EQ(a, b)    ut::check_eq((long long)(a), (long long)(b), #a, #b, __FILE__, __LINE__)
#define CHECK_TRACE(s)    ut::check_str(ut::trace_str(), (s), "trace", __FILE__, __LINE__)

#endif // UTEST_H
