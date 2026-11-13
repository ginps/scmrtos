//******************************************************************************
//*
//*     scmRTOS unit tests: randomized tests with injected preemption
//*
//*     Five processes run a random mix of operations on shared mutexes, a
//*     channel, an event flag, a message and a semaphore, and check invariants
//*     all the way: mutual exclusion, FIFO order and conservation of channel
//*     items, the semaphore limit, minimal duration of sleeps and timeouts,
//*     and that everybody finishes (no lost wake-up, no deadlock).
//*
//*     What makes it a kernel test is where the interrupts come from: the
//*     Linux port calls a hook each time a critical section ends, i.e. at
//*     every point where a real MCU would take a pending interrupt, and the
//*     hook raises the system timer or a user interrupt at random. The
//*     sequence depends only on the seed (runner options --seed=N, --seeds=A-B),
//*     so any failure can be replayed.
//*
//*     fuzz.async_stress uses a real timer instead: interrupts arrive at
//*     arbitrary instructions. It is not reproducible, but it is the test that
//*     notices a missing critical section.
//*
//******************************************************************************
#include "utest.h"
#include <stdio.h>
#ifndef UT_V52
#include <semaphore.h>
#endif

using namespace OS;

namespace
{
    struct TRng
    {
        uint32_t s;
        void     init(uint32_t seed) { s = seed * 2654435761u + 0x9E3779B9u; if(!s) s = 1; next(); next(); }
        uint32_t next()              { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
        unsigned below(unsigned n)   { return next() % n; }
    };

    const unsigned MAX_ITEMS = 500;
    unsigned ITEMS = 25;                        // channel items per producer (P1, P2)
    unsigned TOTAL = 2 * ITEMS;
    unsigned OPS   = 40;                        // random operations of the worker (P4)

    TRng ProcRng[ut::PROC_COUNT], HookRng, IsrRng;

    TMutex               M0, M1;
    channel<uint32_t, 4> Ch;
    TEventFlag           Flag;
    message<int>         Msg;
#ifndef UT_V52
    TSemaphore           Sem(2, 2);
    volatile int         SemHolders;
#endif

    volatile int      Owner0 = -1, Owner1 = -1;
    volatile unsigned Consumed;
    volatile bool     Seen[2 * MAX_ITEMS];
    volatile unsigned InjTicks, InjIrqs, IrqsTaken;
    volatile bool     Inject;                   // interrupt injection is on
    unsigned          SinglePos, SingleKind;    // single-injection mode: one interrupt at the SinglePos-th point
    volatile unsigned Points;                   // critical section exits seen so far (injection points)
    bool              Strict;                   // nobody wakes processes by force: waits last at least their timeout
    bool              Chaos;                    // processes and interrupts call wake_up()/force_wake_up() at random

    //--------------------------------------------------------------------------
    //  Interrupt injection at the end of critical sections
    void critsect_exit_hook()
    {
        if(!Inject)
            return;
        if(SinglePos)
        {
            Points = Points + 1;
            if(Points == SinglePos)
                switch(SingleKind)
                {
                    case 0:  InjTicks = InjTicks + 1; ut::tick();   break;
                    case 1:  InjIrqs = InjIrqs + 1;   ut::raise(0); break;
                    default: InjIrqs = InjIrqs + 1;   ut::raise(1); break;
                }
            return;
        }
        unsigned r = HookRng.below(16);
        if(r < 2)                { InjTicks = InjTicks + 1; ut::tick(); }
        else if(r == 2)          { InjIrqs = InjIrqs + 1; ut::raise(0); }
        else if(r == 3 && Chaos) { InjIrqs = InjIrqs + 1; ut::raise(1); }
    }

    void irq_signal()
    {
        TISRW isr;
        if(IsrRng.below(2)) Flag.signal_isr();
        else                { Msg = 1; Msg.send_isr(); }
    }

    void irq_wakeup()
    {
        TISRW isr;
        OS::TBaseProcess & p = ut::P(IsrRng.below(ut::PROC_COUNT));
        if(IsrRng.below(2)) p.wake_up();
        else                p.force_wake_up();
    }

    //--------------------------------------------------------------------------
    //  Operations
    void pause(TRng & r)
    {
        switch(r.below(3))
        {
            case 0: break;
            case 1: OS::sleep(1); break;
            case 2: ut::tick(); break;
        }
    }

    void timed_sleep(TRng & r)
    {
        unsigned n = 1 + r.below(3), t0 = ut::ticks();
        OS::sleep(n);
        if(Strict) CHECK(ut::ticks() - t0 >= n);
    }

    void section0(int i, TRng & r)
    {
        M0.lock();
        CHECK_EQ(Owner0, -1); Owner0 = i;
        pause(r);
        CHECK_EQ(Owner0, i);  Owner0 = -1;
        M0.unlock();
    }

    void section1(int i, TRng & r)
    {
        unsigned t = 1 + r.below(4), t0 = ut::ticks();
        if(M1.try_lock(t))
        {
            CHECK_EQ(Owner1, -1); Owner1 = i;
            pause(r);
            CHECK_EQ(Owner1, i);  Owner1 = -1;
            M1.unlock();
        }
        else if(Strict)
            CHECK(ut::ticks() - t0 >= t);
    }

    void nested(int i, TRng & r)
    {
        M0.lock();                              // always M0 first: no lock-order deadlock
        CHECK_EQ(Owner0, -1); Owner0 = i;
        M1.lock();
        CHECK_EQ(Owner1, -1); Owner1 = i;
        pause(r);
        CHECK_EQ(Owner1, i);  Owner1 = -1;
        M1.unlock();
        CHECK_EQ(Owner0, i);  Owner0 = -1;
        M0.unlock();
    }

    void random_op(int i)
    {
        TRng & r = ProcRng[i];
        switch(r.below(Chaos ? 11 : 10))
        {
            case 0: section0(i, r); break;
            case 1: section1(i, r); break;
            case 2: nested(i, r);   break;
            case 3:
            {
                unsigned t = 1 + r.below(5), t0 = ut::ticks();
                if(!Flag.wait(t) && Strict) CHECK(ut::ticks() - t0 >= t);
                break;
            }
            case 4: Flag.signal(); break;
            case 5:
            {
                unsigned t = 1 + r.below(3), t0 = ut::ticks();
                if(!Msg.wait(t) && Strict) CHECK(ut::ticks() - t0 >= t);
                break;
            }
            case 6: Msg = i; Msg.send(); break;
            case 7: timed_sleep(r); break;
            case 8:
            {
                TCritSect cs;                   // an interrupt pending while a service reschedules
                ut::tick();
                if(r.below(2)) Flag.signal(); else { Msg = i; Msg.send(); }
                break;
            }
            case 9:
            {
            #ifndef UT_V52
                unsigned t = 1 + r.below(4), t0 = ut::ticks();
                if(Sem.acquire(t))
                {
                    { TCritSect cs; SemHolders = SemHolders + 1; CHECK(SemHolders <= 2); }
                    pause(r);
                    { TCritSect cs; SemHolders = SemHolders - 1; }
                    CHECK(Sem.release());
                }
                else if(Strict)
                    CHECK(ut::ticks() - t0 >= t);
            #else
                timed_sleep(r);
            #endif
                break;
            }
            case 10:
            {
                OS::TBaseProcess & p = ut::P(r.below(ut::PROC_COUNT));
                if(r.below(2)) p.wake_up(); else p.force_wake_up();
                break;
            }
        }
    }

    void producer(int i, unsigned id)
    {
        for(unsigned k = 0; k < ITEMS; ++k)
        {
            Ch.push(id << 16 | k);
            random_op(i);
        }
    }

    void consumer(int i)
    {
        int last[2] = { -1, -1 };               // FIFO: each consumer sees a producer's items in order
        for(;;)
        {
            { TCritSect cs; if(Consumed >= TOTAL) break; }
            uint32_t x = 0;
            unsigned t = 1 + ProcRng[i].below(8), t0 = ut::ticks();
            if(Ch.pop(x, t))
            {
                unsigned id = x >> 16, k = x & 0xFFFF;
                if(CHECK(id < 2 && k < ITEMS))
                {
                    TCritSect cs;
                    CHECK(int(k) > last[id]);  last[id] = k;
                    CHECK(!Seen[id * ITEMS + k]); Seen[id * ITEMS + k] = true;
                    Consumed = Consumed + 1;
                }
            }
            else if(Strict)
            {
                bool all; { TCritSect cs; all = Consumed >= TOTAL; }
                if(!all) CHECK(ut::ticks() - t0 >= t);
            }
            random_op(i);
        }
    }

    void worker(int i)
    {
        for(unsigned k = 0; k < OPS; ++k)
            random_op(i);
    }

    void final_checks()
    {
        Inject = false;
        CHECK_EQ(Consumed, TOTAL);
        for(unsigned k = 0; k < TOTAL; ++k) CHECK(Seen[k]);
        CHECK_EQ(Ch.get_count(), 0);
        CHECK_EQ(Owner0, -1); CHECK_EQ(Owner1, -1);
        CHECK(!M0.is_locked()); CHECK(!M1.is_locked());
    #ifndef UT_V52
        CHECK_EQ(SemHolders, 0);
        CHECK_EQ(Sem.get_count(), 2);
    #endif
        printf("    seed %u: %u ticks, %u context switches, injected: %u ticks, %u interrupts\n",
               ut::seed(), ut::ticks(), ut::switches(), InjTicks, InjIrqs);
        if(SinglePos)
            printf("    points: %u\n", (unsigned)Points);
    }

    void setup(bool inject, bool chaos, bool strict, unsigned scale = 1, unsigned workload = 0)
    {
        if(!workload) workload = ut::seed();
        ITEMS *= scale; TOTAL *= scale; OPS *= scale;
        for(int i = 0; i < ut::PROC_COUNT; ++i) ProcRng[i].init(workload * 16 + i);
        HookRng.init(workload * 16 + 8);
        IsrRng.init(workload * 16 + 9);
        Chaos = chaos; Strict = strict;
        ut::max_ticks(200000);
        ut::irq(0, irq_signal);
        ut::irq(1, irq_wakeup);
        ut::on_critsect_exit(critsect_exit_hook);
        ut::proc(0, []{ consumer(0); });
        ut::proc(1, []{ producer(1, 0); });
        ut::proc(2, []{ producer(2, 1); });
        ut::proc(3, []{ consumer(3); });
        ut::proc(4, []{ worker(4); });
        ut::at_end(final_checks);
        Inject = inject;
    }
}

// On a tree with defect 2 a consumer's timed pop() can hang for ever: the fuzz
// tests find it within a few seeds, so there they are expected to fail.
#ifdef UT_DEFECT2_PRESENT
#define UT_FUZZ_TEST(name) UT_TEST_DEFECT(fuzz, name, "2")
#else
#define UT_FUZZ_TEST(name) UT_TEST(fuzz, name)
#endif

// Deterministic for a given seed. Nobody forces wake-ups, so on top of the
// safety invariants every sleep and every timed-out wait must last at least
// as long as requested.
UT_FUZZ_TEST(injected_preemption)
{
    setup(true, false, true);
}

// As above plus random wake_up()/force_wake_up() from processes and from an
// interrupt: waits may end early by design, the safety invariants must hold.
UT_FUZZ_TEST(injected_preemption_with_forced_wakeups)
{
    setup(true, true, false);
}

// Exhaustive for one interrupt: a fixed workload, and exactly one interrupt at
// the N-th point where an interrupt can be taken. Seed 3*(N-1)+1 injects a
// system timer tick there, +2 an interrupt that signals the flag or the
// message, +3 one that wakes a process by force. With seeds 1..3*points (the
// test prints "points: N"; run.sh does that) every point of the scenario is
// tried with every kind of interrupt.
UT_FUZZ_TEST(single_interrupt_at_every_point)
{
    unsigned idx = ut::seed() - 1;
    SingleKind = idx % 3;
    SinglePos  = idx / 3 + 1;
    setup(true, false, SingleKind != 2, 1, 12345);
}

// The same workload without injection: the baseline for the two tests above.
UT_FUZZ_TEST(no_injection)
{
    setup(false, false, true);
}

// Interrupts from a real timer every 40 us, at arbitrary instructions.
UT_FUZZ_TEST(async_stress)
{
    setup(false, false, true, 20);
    ut::async_ticks(40);
}
