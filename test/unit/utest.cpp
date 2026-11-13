//******************************************************************************
//*
//*     scmRTOS unit tests: minimal test framework (implementation)
//*
//******************************************************************************
#include "utest.h"

#if defined(__has_include)
#if __has_include(<safe_run.h>)
#include <safe_run.h>
#define UT_HAVE_SAFE_RUN 1
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/wait.h>

//==============================================================================
//
//    Test state (lives in the forked child)
//
namespace
{
    ut::TTest * First = 0;
    ut::TTest * Last  = 0;

    ut::fn_t   Bodies[ut::PROC_COUNT];
    ut::TMode  Modes [ut::PROC_COUNT];
    volatile bool Done[ut::PROC_COUNT];
    ut::fn_t   AtEnd        = 0;
    ut::fn_t   SysTimerHook = 0;
    ut::fn_t   CtxSwHook    = 0;
    unsigned   MaxTicks     = 2000;
    bool       SafeRun      = false;
    ut::fn_t   CritSectHook = 0;
    unsigned   Seed         = 1;
    unsigned   AsyncPeriod  = 0;
    volatile unsigned Switches = 0;
    volatile int      Failures = 0;
    volatile bool     Finishing = false;

    char   Trace[8192];
    size_t TraceLen = 0;

    const OS::TPriority PrioTable[] =
    {
        OS::pr0,
    #if scmRTOS_PROCESS_COUNT > 1
        OS::pr1,
    #endif
    #if scmRTOS_PROCESS_COUNT > 2
        OS::pr2,
    #endif
    #if scmRTOS_PROCESS_COUNT > 3
        OS::pr3,
    #endif
    #if scmRTOS_PROCESS_COUNT > 4
        OS::pr4,
    #endif
    };

    __attribute__((unused)) void check_all_done()
    {
        for(int i = 0; i < ut::PROC_COUNT; ++i)
            if(Bodies[i] && Modes[i] != ut::DAEMON && !Done[i])
                return;
        ut::finish();
    }
}

//==============================================================================
//
//    RTOS processes P0..P4 (P4 is declared suspended: the framework starts it
//    unless the test asks for ut::SUSPENDED)
//
#ifndef UT_EXTERNAL_PROCESSES

static_assert(scmRTOS_PROCESS_COUNT == 5, "the unit tests use 5 processes");

typedef OS::process<OS::pr0, 64*1024>                   TP0;
typedef OS::process<OS::pr1, 64*1024>                   TP1;
typedef OS::process<OS::pr2, 64*1024>                   TP2;
typedef OS::process<OS::pr3, 64*1024>                   TP3;
typedef OS::process<OS::pr4, 64*1024, OS::pssSuspended> TP4;

static NORETURN void run_body(int i);
__attribute__((unused)) static void p3_entry();

TP0 P0("P0");
TP1 P1("P1");
TP2 P2("P2");
#ifdef UT_V52
TP3 P3("P3");
#else
TP3 P3("P3", p3_entry);         // develop: the process function given to the constructor
#endif
TP4 P4("P4");

bool ut::P3StartedViaFunction = false;
static void p3_entry() { ut::P3StartedViaFunction = true; run_body(3); }

static NORETURN void run_body(int i)
{
    if(Bodies[i])
    {
        Bodies[i]();
        Done[i] = true;
        check_all_done();
    }
    for(;;)
        OS::sleep();
}

namespace OS
{
    template<> void TP0::exec() { run_body(0); }
    template<> void TP1::exec() { run_body(1); }
    template<> void TP2::exec() { run_body(2); }
    template<> void TP3::exec() { run_body(3); }
    template<> void TP4::exec() { run_body(4); }
}

namespace
{
    // access to the protected start-state map, for the test set-up only
    struct TSuspendedAccess : public OS::TBaseProcess
    {
        static OS::TProcessMap & map() { return SuspendedProcessMap; }
    };
}

#endif // UT_EXTERNAL_PROCESSES

//==============================================================================
//
//    Kernel hooks
//
namespace OS
{
    void idle_process_user_hook()
    {
        if(ut::ticks() >= MaxTicks)
        {
            for(int i = 0; i < ut::PROC_COUNT; ++i)
                if(Bodies[i] && Modes[i] != ut::DAEMON && !Done[i])
                {
                    printf("    P%d did not finish within %u ticks\n", i, MaxTicks);
                    Failures = Failures + 1;
                }
            ut::finish();
        }
        OS::sim_tick();
    }

#if scmRTOS_SYSTIMER_HOOK_ENABLE == 1
    void system_timer_user_hook()
    {
        if(SysTimerHook)
            SysTimerHook();
    }
#endif

#if scmRTOS_CONTEXT_SWITCH_USER_HOOK_ENABLE == 1
    void context_switch_user_hook()
    {
        Switches = Switches + 1;
        // Kernel invariant: the scheduler works on the process maps and the
        // process table, so it must never be entered with the interrupts enabled.
        // (Scheme 1 calls this hook from the context switch interrupt, where
        // that is always true; scheme 0 calls it from the scheduler itself.)
        if(!get_interrupt_state() && !Finishing)
        {
            printf("    context switch requested outside a critical section (tick %u)\n", ut::ticks());
            Failures = Failures + 1;
        }
        if(CtxSwHook)
            CtxSwHook();
    }
#endif
}

//==============================================================================
//
//    Framework API
//
void ut::proc(int idx, fn_t body, TMode mode)
{
    if(idx < 0 || idx >= PROC_COUNT)
        return;
    Bodies[idx] = body;
    Modes[idx]  = mode;
}

void ut::irq(int n, fn_t handler)           { OS::host::set_irq_handler(n, handler); }
void ut::at_end(fn_t check)                 { AtEnd = check; }
void ut::on_systimer(fn_t hook)             { SysTimerHook = hook; }
void ut::on_context_switch(fn_t hook)       { CtxSwHook = hook; }
void ut::max_ticks(unsigned n)              { MaxTicks = n; }
void ut::use_safe_run()                     { SafeRun = true; }
void ut::on_critsect_exit(fn_t hook)        { CritSectHook = hook; }
void ut::async_ticks(unsigned period_us)    { AsyncPeriod = period_us; }
unsigned ut::seed()                         { return Seed; }

extern "C" void ut_critsect_exit_hook()
{
    if(CritSectHook && !Finishing)
        CritSectHook();
}

void ut::finish()
{
    TCritSect cs;
    if(Finishing)
        return;
    Finishing = true;
    if(AtEnd)
        AtEnd();
    fflush(stdout);
    exit(Failures ? 1 : 0);
}

void ut::tick(unsigned n)
{
    for(unsigned i = 0; i < n; ++i)
        OS::sim_tick();
}

unsigned ut::ticks()                        { return OS::get_tick_count(); }
unsigned ut::switches()                     { return Switches; }
void     ut::raise(int n)                   { OS::host::raise_irq(n); }
OS::TPriority ut::prio(int idx)             { return PrioTable[idx]; }
bool     ut::done(int idx)                  { return Done[idx]; }

OS::TBaseProcess & ut::P(int idx)
{
    return const_cast<OS::TBaseProcess &>(*OS::get_proc(PrioTable[idx]));
}

#if scmRTOS_PROCESS_RESTART_ENABLE == 1 && !defined(UT_EXTERNAL_PROCESSES)
void ut::terminate(int idx, fn_t new_body)
{
    // new_body runs instead of the process function; it must never return
#ifdef UT_V52
    (void)new_body;                     // v5.2.0: terminate() always restarts exec()
    switch(idx)
    {
        case 0: P0.terminate(); break;
        case 1: P1.terminate(); break;
        case 2: P2.terminate(); break;
        case 3: P3.terminate(); break;
        case 4: P4.terminate(); break;
    }
#else
    switch(idx)
    {
        case 0: P0.terminate(new_body); break;
        case 1: P1.terminate(new_body); break;
        case 2: P2.terminate(new_body); break;
        case 3: P3.terminate(new_body); break;
        case 4: P4.terminate(new_body); break;
    }
#endif
}
#endif

//------------------------------------------------------------------------------
void ut::trace(const char * fmt, ...)
{
    TCritSect cs;
    char tok[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tok, sizeof(tok), fmt, ap);
    va_end(ap);
    size_t n = strlen(tok);
    if(TraceLen + n + 2 >= sizeof(Trace))
        return;
    if(TraceLen)
        Trace[TraceLen++] = ' ';
    memcpy(Trace + TraceLen, tok, n);
    TraceLen += n;
    Trace[TraceLen] = 0;
}

const char * ut::trace_str()                { return Trace; }
void ut::trace_clear()                      { TCritSect cs; TraceLen = 0; Trace[0] = 0; }

//------------------------------------------------------------------------------
bool ut::check(bool ok, const char * expr, const char * file, int line)
{
    if(!ok)
    {
        TCritSect cs;
        printf("    %s:%d: CHECK(%s) failed (tick %u)\n", file, line, expr, ticks());
        Failures = Failures + 1;
    }
    return ok;
}

bool ut::check_eq(long long a, long long b, const char * ea, const char * eb, const char * file, int line)
{
    if(a != b)
    {
        TCritSect cs;
        printf("    %s:%d: CHECK_EQ(%s, %s) failed: %lld != %lld (tick %u)\n", file, line, ea, eb, a, b, ticks());
        Failures = Failures + 1;
    }
    return a == b;
}

bool ut::check_str(const char * a, const char * b, const char * ea, const char * file, int line)
{
    if(strcmp(a, b) != 0)
    {
        TCritSect cs;
        printf("    %s:%d: %s mismatch\n      actual:   \"%s\"\n      expected: \"%s\"\n", file, line, ea, a, b);
        Failures = Failures + 1;
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
ut::Registrar::Registrar(TTest * t)
{
    t->next = 0;
    if(Last) Last->next = t; else First = t;
    Last = t;
}

//==============================================================================
//
//    Test runner
//
static NORETURN void run_child(ut::TTest * t)
{
    t->body();                                  // set-up

    bool any = false;
    for(int i = 0; i < ut::PROC_COUNT; ++i)
        if(Bodies[i])
            any = true;
    if(!any)                                    // plain unit test
        ut::finish();

#ifndef UT_EXTERNAL_PROCESSES
    if(Modes[4] != ut::SUSPENDED)
        TSuspendedAccess::map() |= OS::get_prio_tag(OS::pr4);
#ifdef UT_HAVE_SAFE_RUN
    if(SafeRun)
        OS::safe_run(P0, P1, P2, P3, P4);
#endif
#endif
    if(AsyncPeriod)
    {
        disable_interrupts();                   // no timer signal before the kernel installs its handler
        struct sigevent sev;
        memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo  = OS::host::SIG_SYSTEM_TIMER;
        timer_t tm;
        timer_create(CLOCK_MONOTONIC, &sev, &tm);
        struct itimerspec its;
        its.it_interval.tv_sec = 0; its.it_interval.tv_nsec = AsyncPeriod * 1000L;
        its.it_value = its.it_interval;
        timer_settime(tm, 0, &its, 0);
    }
    OS::run();
}

static double now_ms()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static bool matches(const ut::TTest * t, int argc, char ** argv)
{
    char full[256];
    snprintf(full, sizeof(full), "%s.%s", t->suite, t->name);
    bool any_filter = false;
    for(int i = 1; i < argc; ++i)
    {
        if(argv[i][0] == '-')
            continue;
        any_filter = true;
        if(strstr(full, argv[i]))
            return true;
    }
    return !any_filter;
}

// Run one test in a forked child; returns true if it passed. The child's output goes to 'out'.
static bool run_one(ut::TTest * t, unsigned seed, double timeout_ms, char *& out, char * reason, size_t reason_size, double & dt)
{
    fflush(stdout);
    int fds[2];
    if(pipe(fds) != 0) { perror("pipe"); exit(2); }
    pid_t pid = fork();
    if(pid == 0)
    {
        close(fds[0]);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        setvbuf(stdout, 0, _IOLBF, 0);
        Seed = seed;
        run_child(t);
    }
    close(fds[1]);
    fcntl(fds[0], F_SETFL, O_NONBLOCK);

    std::size_t cap = 1 << 16, len = 0;
    out = static_cast<char*>(malloc(cap));
    int status = 0;
    bool hung = false;
    double t0 = now_ms();
    for(;;)
    {
        pollfd p = { fds[0], POLLIN, 0 };
        poll(&p, 1, 2);
        for(;;)
        {
            if(len + 4096 > cap) { cap *= 2; out = static_cast<char*>(realloc(out, cap)); }
            ssize_t n = read(fds[0], out + len, 4096);
            if(n <= 0) break;
            len += n;
        }
        if(waitpid(pid, &status, WNOHANG) == pid)
            break;
        if(now_ms() - t0 > timeout_ms)
        {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            hung = true;
            break;
        }
    }
    for(;;)
    {
        if(len + 4096 > cap) { cap *= 2; out = static_cast<char*>(realloc(out, cap)); }
        ssize_t n = read(fds[0], out + len, 4096);
        if(n <= 0) break;
        len += n;
    }
    close(fds[0]);
    out[len] = 0;
    dt = now_ms() - t0;

    reason[0] = 0;
    if(hung)                            snprintf(reason, reason_size, "hung, killed after %.0f ms", timeout_ms);
    else if(WIFSIGNALED(status))        snprintf(reason, reason_size, "crashed: %s", strsignal(WTERMSIG(status)));
    else if(WEXITSTATUS(status) != 0)   snprintf(reason, reason_size, "failed");
    else                                return true;
    return false;
}

int main(int argc, char ** argv)
{
    bool verbose = false, list = false, fail_fast = false;
    double timeout_ms = 5000;
    unsigned seed_from = 1, seed_to = 1;
    for(int i = 1; i < argc; ++i)
    {
        if(!strcmp(argv[i], "-v")) verbose = true;
        else if(!strcmp(argv[i], "-l")) list = true;
        else if(!strcmp(argv[i], "-x")) fail_fast = true;
        else if(!strncmp(argv[i], "--timeout=", 10)) timeout_ms = atof(argv[i] + 10);
        else if(!strncmp(argv[i], "--seed=", 7)) seed_from = seed_to = strtoul(argv[i] + 7, 0, 10);
        else if(!strncmp(argv[i], "--seeds=", 8))
        {
            char * dash = 0;
            seed_from = strtoul(argv[i] + 8, &dash, 10);
            seed_to   = *dash == '-' ? strtoul(dash + 1, 0, 10) : seed_from;
        }
        else if(!strcmp(argv[i], "-h"))
        {
            printf("usage: %s [-l] [-v] [-x] [--timeout=ms] [--seed=N | --seeds=A-B] [filter...]\n"
                   "  -l            list tests\n"
                   "  -v            show the output of passed tests too\n"
                   "  -x            stop at the first unexpected failure\n"
                   "  --seeds=A-B   run every selected test once per seed (ut::seed()); for the fuzz tests\n"
                   "  filter        run tests whose 'suite.name' contains one of the filters\n", argv[0]);
            return 0;
        }
    }

    if(list)
    {
        for(ut::TTest * t = First; t; t = t->next)
            if(matches(t, argc, argv))
                printf("%s.%s%s%s\n", t->suite, t->name, t->defect ? "  [defect " : "", t->defect ? t->defect : "");
        return 0;
    }

    int total = 0, passed = 0, failed = 0, known = 0, fixed = 0;
    for(ut::TTest * t = First; t; t = t->next)
    {
        if(!matches(t, argc, argv))
            continue;
        ++total;

        // all seeds of one test count as one test; the first failing seeds are shown
        bool ok = true;
        unsigned bad_seeds = 0, runs = 0;
        const unsigned MAX_BAD_SEEDS = 5;
        char reason[64] = "";
        double dt_total = 0;
        for(unsigned seed = seed_from; seed <= seed_to; ++seed)
        {
            char * out = 0;
            char r[64];
            double dt = 0;
            bool one = run_one(t, seed, timeout_ms, out, r, sizeof(r), dt);
            ++runs;
            dt_total += dt;
            if(!one)
            {
                if(ok) snprintf(reason, sizeof(reason), "%s", r);
                ok = false;
                if(++bad_seeds <= 3 && seed_to != seed_from)
                    printf("    seed %u: %s\n%s", seed, r, out);
            }
            if(seed_to == seed_from)
            {
                const char * tag;
                if(ok && !t->defect)        { tag = "[  OK  ]"; }
                else if(ok)                 { tag = "[ FIXD ]"; }
                else if(t->defect)          { tag = "[ KNWN ]"; }
                else                        { tag = "[ FAIL ]"; }
                printf("%s %s.%s", tag, t->suite, t->name);
                if(!ok)                     printf("  (%s)", reason);
                if(t->defect)               printf("  [defect %s%s]", t->defect, ok ? " not reproduced" : "");
                if(dt > 500)                printf("  %.0f ms", dt);
                printf("\n");
                if((!ok || verbose) && out && out[0])
                    fputs(out, stdout);
            }
            free(out);
            if(bad_seeds >= MAX_BAD_SEEDS)      // the point is made; failing seeds are slow (hangs)
                break;
        }
        if(seed_to != seed_from)
        {
            const char * tag = ok ? (t->defect ? "[ FIXD ]" : "[  OK  ]") : (t->defect ? "[ KNWN ]" : "[ FAIL ]");
            printf("%s %s.%s  (%u seeds, %u failed%s, %.1f s)\n", tag, t->suite, t->name, runs, bad_seeds,
                   bad_seeds >= MAX_BAD_SEEDS && runs < seed_to - seed_from + 1 ? ", stopped" : "", dt_total / 1000);
        }

        if(ok)              { ++passed; if(t->defect) ++fixed; }
        else if(t->defect)  ++known;
        else                ++failed;
        if(fail_fast && failed)
            break;
    }

    printf("\n%d tests: %d passed, %d failed, %d known defects reproduced, %d known defects not reproduced\n",
           total, passed, failed, known, fixed);
    printf("SUMMARY total=%d passed=%d failed=%d known=%d fixed=%d\n", total, passed, failed, known, fixed);
    return failed ? 1 : 0;
}
