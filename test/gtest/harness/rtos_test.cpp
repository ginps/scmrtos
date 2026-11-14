//******************************************************************************
//*
//*     scmRTOS GoogleTest suite: test harness (implementation)
//*
//******************************************************************************
#include "rtos_test.h"

#include <gtest/gtest-spi.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cstdint>

using namespace OS;

//==============================================================================
//
//    Scenario state (meaningful in the forked child)
//
namespace
{
    scmtest::RtosTest *    Current = nullptr;

    std::function<void()>  Bodies  [scmtest::kProcCount];
    std::function<void()>  Restarts[scmtest::kProcCount];
    scmtest::Mode          Modes   [scmtest::kProcCount];
    volatile bool          DoneFlags[scmtest::kProcCount];

    std::function<void()>  IrqBodies[scmtest::kIrqCount];
    bool                   IrqRaw   [scmtest::kIrqCount];

    std::function<void()>  AtEndCheck;
    unsigned               MaxTicksLimit = 5000;

    volatile unsigned      SwitchCount = 0;
    volatile bool          Finishing   = false;
    volatile bool          InChild     = false;
    volatile int           Failures    = 0;
    int                    ResultFd    = -1;

    const TPriority PrioTable[] = { pr0, pr1, pr2, pr3, pr4 };
    static_assert(sizeof(PrioTable) / sizeof(PrioTable[0]) == scmtest::kProcCount, "5 processes");

    void reset_state()
    {
        for(int i = 0; i < scmtest::kProcCount; ++i)
        {
            Bodies[i] = nullptr; Restarts[i] = nullptr;
            Modes[i] = scmtest::Mode::MustFinish; DoneFlags[i] = false;
        }
        for(int i = 0; i < scmtest::kIrqCount; ++i) { IrqBodies[i] = nullptr; IrqRaw[i] = false; }
        AtEndCheck    = nullptr;
        MaxTicksLimit = 5000;
        SwitchCount   = 0;
        Finishing     = false;
        Failures      = 0;
    }

    void check_all_done()
    {
        for(int i = 0; i < scmtest::kProcCount; ++i)
            if(Bodies[i] && Modes[i] != scmtest::Mode::Daemon && !DoneFlags[i])
                return;
        scmtest::RtosTest::Finish();
    }

    //--------------------------------------------------------------------------
    //  Child -> parent failure relay
    //
    //  Record: uint32 type, int32 line, uint32 file length, uint32 message length,
    //          file bytes, message bytes
    //
    void write_all(int fd, const void * p, size_t n)
    {
        const char * c = static_cast<const char *>(p);
        while(n)
        {
            ssize_t w = write(fd, c, n);
            if(w < 0) { if(errno == EINTR) continue; return; }
            c += w; n -= static_cast<size_t>(w);
        }
    }

    class RelayReporter : public ::testing::ScopedFakeTestPartResultReporter
    {
    public:
        RelayReporter() : ScopedFakeTestPartResultReporter(INTERCEPT_ONLY_CURRENT_THREAD, &sink()) { }

        void ReportTestPartResult(const ::testing::TestPartResult & r) override
        {
            if(r.type() == ::testing::TestPartResult::kSuccess)
                return;
            if(r.failed())
                Failures = Failures + 1;
            const char * file = r.file_name() ? r.file_name() : "";
            std::string  msg  = r.message();
            if(!Finishing && os_running())
                msg += "\n    (tick " + std::to_string(get_tick_count()) + ")";
            uint32_t hdr[4] = { static_cast<uint32_t>(r.type()), static_cast<uint32_t>(r.line_number()),
                                static_cast<uint32_t>(strlen(file)), static_cast<uint32_t>(msg.size()) };
            write_all(ResultFd, hdr, sizeof(hdr));
            write_all(ResultFd, file, hdr[2]);
            write_all(ResultFd, msg.data(), hdr[3]);
            if(r.fatally_failed())          // ASSERT_*: the scenario stops here
            {
                fflush(stdout);
                _exit(1);
            }
        }

    private:
        static ::testing::TestPartResultArray & sink() { static ::testing::TestPartResultArray a; return a; }
    };
}

//==============================================================================
//
//    RTOS processes P0..P4. P4 is declared pssSuspended; the harness makes it
//    start with the others unless the test asks for Mode::Suspended.
//
typedef OS::process<OS::pr0, 128*1024>                   TP0;
typedef OS::process<OS::pr1, 128*1024>                   TP1;
typedef OS::process<OS::pr2, 128*1024>                   TP2;
typedef OS::process<OS::pr3, 128*1024>                   TP3;
typedef OS::process<OS::pr4, 128*1024, OS::pssSuspended> TP4;

TP0 P0("P0");
TP1 P1("P1");
TP2 P2("P2");
TP3 P3("P3");
TP4 P4("P4");

static NORETURN void run_body(int i, const std::function<void()> & body)
{
    if(body)
    {
        body();
        DoneFlags[i] = true;
        check_all_done();
    }
    for(;;)
        OS::sleep();
}

namespace OS
{
    template<> void TP0::exec() { run_body(0, Bodies[0]); }
    template<> void TP1::exec() { run_body(1, Bodies[1]); }
    template<> void TP2::exec() { run_body(2, Bodies[2]); }
    template<> void TP3::exec() { run_body(3, Bodies[3]); }
    template<> void TP4::exec() { run_body(4, Bodies[4]); }
}

namespace
{
    struct TSuspendedAccess : public OS::TBaseProcess
    {
        static OS::TProcessMap & start_map() { return SuspendedProcessMap; }
    };

    template<int N> void irq_entry()
    {
        if(IrqRaw[N] && IrqBodies[N])
        {
            IrqBodies[N]();
            return;
        }
        OS::TISRW isr;
        if(IrqBodies[N])
            IrqBodies[N]();
        if(Current)
            Current->irq.Handler(N);
    }

    typedef void (*fn_t)();
    const fn_t IrqEntries[] = { irq_entry<0>, irq_entry<1>, irq_entry<2>, irq_entry<3>,
                                irq_entry<4>, irq_entry<5>, irq_entry<6>, irq_entry<7> };
    static_assert(sizeof(IrqEntries) / sizeof(IrqEntries[0]) == scmtest::kIrqCount, "8 interrupts");

#if scmRTOS_PROCESS_RESTART_ENABLE == 1
    template<int I> NORETURN void restart_entry() { run_body(I, Restarts[I]); }
#endif
}

//==============================================================================
//
//    Kernel hooks
//
namespace OS
{
    void idle_process_user_hook()
    {
        if(get_tick_count() >= MaxTicksLimit && !Finishing)
        {
            for(int i = 0; i < scmtest::kProcCount; ++i)
                if(Bodies[i] && Modes[i] != scmtest::Mode::Daemon && !DoneFlags[i])
                    ADD_FAILURE() << "P" << i << " did not finish within " << MaxTicksLimit
                                  << " ticks (every process is blocked)";
            scmtest::RtosTest::Finish();
        }
        OS::sim_tick();
    }

    void system_timer_user_hook()
    {
        if(Current && !Finishing)
            Current->hooks.SystemTimer();
    }

    void context_switch_user_hook()
    {
        SwitchCount = SwitchCount + 1;
        if(Finishing)
            return;
        // Kernel invariant: the scheduler works on the process maps and must
        // never run with the interrupts enabled.
        if(!get_interrupt_state())
            ADD_FAILURE() << "context switch outside a critical section";
        if(Current)
            Current->hooks.ContextSwitch();
    }
}

//==============================================================================
//
//    Fixture: set-up
//
namespace scmtest
{
    RtosTest::RtosTest()  { reset_state(); Current = this; }
    RtosTest::~RtosTest() { if(Current == this) Current = nullptr; }

    void RtosTest::Process(int idx, std::function<void()> body, Mode mode)
    {
        ASSERT_TRUE(idx >= 0 && idx < kProcCount) << "no process P" << idx;
        ASSERT_TRUE(mode != Mode::Suspended || idx == 4) << "only P4 is declared pssSuspended";
        Bodies[idx] = std::move(body);
        Modes[idx]  = mode;
    }

    void RtosTest::Irq(int n, std::function<void()> body)
    {
        ASSERT_TRUE(n >= 0 && n < kIrqCount) << "no interrupt " << n;
        IrqBodies[n] = std::move(body);
        IrqRaw[n]    = false;
    }

    void RtosTest::RawIrq(int n, std::function<void()> handler)
    {
        ASSERT_TRUE(n >= 0 && n < kIrqCount) << "no interrupt " << n;
        ASSERT_TRUE(static_cast<bool>(handler));
        IrqBodies[n] = std::move(handler);
        IrqRaw[n]    = true;
    }

    void RtosTest::AtEnd(std::function<void()> check) { AtEndCheck = std::move(check); }
    void RtosTest::MaxTicks(unsigned n)               { MaxTicksLimit = n; }
    void RtosTest::VerifyAtEnd(void * mock)           { extra_mocks_.push_back(mock); }

    //--------------------------------------------------------------------------
    //  Run time
    //
    void RtosTest::Finish()
    {
        TCritSect cs;
        if(Finishing)
            return;
        Finishing = true;
        if(AtEndCheck)
            AtEndCheck();
        if(Current)
        {
            ::testing::Mock::VerifyAndClearExpectations(&Current->probe);
            ::testing::Mock::VerifyAndClearExpectations(&Current->hooks);
            ::testing::Mock::VerifyAndClearExpectations(&Current->irq);
            for(void * m : Current->extra_mocks_)
                ::testing::Mock::VerifyAndClearExpectations(m);
        }
        fflush(stdout);
        fflush(stderr);
        _exit(Failures ? 1 : 0);
    }

    void RtosTest::Tick(unsigned n)       { for(unsigned i = 0; i < n; ++i) OS::sim_tick(); }
    unsigned RtosTest::Ticks()            { return get_tick_count(); }
    unsigned RtosTest::Switches()         { return SwitchCount; }
    void RtosTest::Raise(int n)           { OS::host::raise_irq(n); }
    TPriority RtosTest::Prio(int idx)     { return PrioTable[idx]; }
    bool RtosTest::Done(int idx)          { return DoneFlags[idx]; }

    TBaseProcess & RtosTest::P(int idx)
    {
        return const_cast<TBaseProcess &>(*get_proc(PrioTable[idx]));
    }

    void RtosTest::Mark(const std::string & what)
    {
        TCritSect cs;
        if(Current)
            Current->probe.Mark(what);
    }

#if scmRTOS_PROCESS_RESTART_ENABLE == 1
    void RtosTest::Terminate(int idx, std::function<void()> new_body)
    {
        fn_t entry = nullptr;
        if(new_body)
        {
            Restarts[idx] = std::move(new_body);
            static const fn_t Entries[] = { restart_entry<0>, restart_entry<1>, restart_entry<2>,
                                            restart_entry<3>, restart_entry<4> };
            entry = Entries[idx];
        }
        DoneFlags[idx] = false;
        switch(idx)
        {
            case 0: P0.terminate(entry); break;
            case 1: P1.terminate(entry); break;
            case 2: P2.terminate(entry); break;
            case 3: P3.terminate(entry); break;
            case 4: P4.terminate(entry); break;
        }
    }
#endif

    //--------------------------------------------------------------------------
    //  RunOS(): child
    //
    static NORETURN void child_main()
    {
        InChild = true;
        new RelayReporter;                          // lives until _exit()

        for(int i = 0; i < kIrqCount; ++i)          // an unexpected interrupt fails the test (irq is strict)
            OS::host::set_irq_handler(i, IrqEntries[i]);

        bool any = false;
        for(int i = 0; i < kProcCount; ++i)
            if(Bodies[i])
                any = true;
        if(!any)
            RtosTest::Finish();

        if(Modes[4] != Mode::Suspended)
            TSuspendedAccess::start_map() |= get_prio_tag(pr4);

        OS::run();
    }

    //--------------------------------------------------------------------------
    //  RunOS(): parent
    //
    static double now_ms()
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
    }

    static bool drain(int fd, std::string & to)
    {
        char buf[4096];
        for(;;)
        {
            ssize_t n = read(fd, buf, sizeof(buf));
            if(n > 0) { to.append(buf, static_cast<size_t>(n)); continue; }
            if(n < 0 && (errno == EAGAIN || errno == EINTR)) return true;
            return false;                           // EOF or error: closed
        }
    }

    void RtosTest::RunOS()
    {
        ASSERT_FALSE(InChild) << "RunOS() called from a scenario";

        const char * env_timeout = getenv("SCMTEST_TIMEOUT_MS");
        const double timeout_ms  = env_timeout ? atof(env_timeout) : 10000;
        const bool   verbose     = getenv("SCMTEST_VERBOSE") != nullptr;

        int res[2], out[2];
        ASSERT_EQ(pipe(res), 0);
        ASSERT_EQ(pipe(out), 0);
        fflush(stdout);
        fflush(stderr);

        pid_t pid = fork();
        ASSERT_GE(pid, 0) << "fork() failed";
        if(pid == 0)
        {
            close(res[0]);
            close(out[0]);
            ResultFd = res[1];
            dup2(out[1], 1);
            dup2(out[1], 2);
            close(out[1]);
            setvbuf(stdout, nullptr, _IOLBF, 0);
            child_main();
        }
        close(res[1]);
        close(out[1]);
        fcntl(res[0], F_SETFL, O_NONBLOCK);
        fcntl(out[0], F_SETFL, O_NONBLOCK);

        std::string results, output;
        bool res_open = true, out_open = true, exited = false, hung = false;
        int  status = 0;
        const double t0 = now_ms();
        while(res_open || out_open || !exited)
        {
            pollfd p[2] = { { res[0], POLLIN, 0 }, { out[0], POLLIN, 0 } };
            poll(p, 2, 5);
            if(res_open) res_open = drain(res[0], results);
            if(out_open) out_open = drain(out[0], output);
            if(!exited && waitpid(pid, &status, WNOHANG) == pid)
                exited = true;
            if(!exited && now_ms() - t0 > timeout_ms)
            {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                exited = hung = true;
                drain(res[0], results);
                drain(out[0], output);
                break;
            }
        }
        close(res[0]);
        close(out[0]);

        // Re-report the failures of the child at their own file and line
        int relayed = 0;
        size_t pos = 0;
        while(pos + 16 <= results.size())
        {
            uint32_t hdr[4];
            memcpy(hdr, results.data() + pos, sizeof(hdr));
            pos += sizeof(hdr);
            if(pos + hdr[2] + hdr[3] > results.size())
                break;
            std::string file(results, pos, hdr[2]);  pos += hdr[2];
            std::string msg (results, pos, hdr[3]);  pos += hdr[3];
            auto type = static_cast<::testing::TestPartResult::Type>(hdr[0]);
            GTEST_MESSAGE_AT_(file.empty() ? nullptr : file.c_str(), static_cast<int>(hdr[1]), msg.c_str(), type);
            if(type != ::testing::TestPartResult::kSkip)
                ++relayed;
        }

        if(hung)
            ADD_FAILURE() << "the scenario hung: killed after " << timeout_ms << " ms";
        else if(WIFSIGNALED(status))
            ADD_FAILURE() << "the scenario crashed: " << strsignal(WTERMSIG(status));
        else if(WEXITSTATUS(status) != 0 && relayed == 0)
            ADD_FAILURE() << "the scenario exited with code " << WEXITSTATUS(status);

        if(!output.empty() && (verbose || HasFailure()))
            printf("---- scenario output ----\n%s---- end of scenario output ----\n", output.c_str());

        // The expectations were set in this (parent) process too, and were
        // verified in the child. Clear the parent copies silently.
        ::testing::TestPartResultArray sink;
        {
            ::testing::ScopedFakeTestPartResultReporter quiet(
                ::testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &sink);
            ::testing::Mock::VerifyAndClearExpectations(&probe);
            ::testing::Mock::VerifyAndClearExpectations(&hooks);
            ::testing::Mock::VerifyAndClearExpectations(&irq);
            for(void * m : extra_mocks_)
                ::testing::Mock::VerifyAndClearExpectations(m);
        }
    }
}
