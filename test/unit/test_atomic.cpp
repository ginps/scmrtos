//******************************************************************************
//*
//*     scmRTOS unit tests: read-modify-write operations under asynchronous
//*     interrupts
//*
//*     Some service calls only update a variable and never reach the
//*     scheduler: TEventFlags::clear() and try_wait(), TSemaphore::try_acquire().
//*     Their critical sections cannot be checked by the deterministic tests,
//*     which take interrupts only at critical section boundaries. Here the
//*     system timer runs from a real timer (every 25 us, at any instruction)
//*     and its handler updates the same object from "interrupt level", while a
//*     process hammers the call under test. A lost update shows up as a
//*     vanished flag bit or a wrong token count.
//*
//*     64-bit hosts only (the semaphore test needs a 64-bit size_t).
//*
//*     On correct code these tests always pass. A missing critical section is
//*     caught with a probability that grows with the number of interrupts; the
//*     tests run until INTERRUPTS of them have been taken (about 0.5 s each).
//*
//******************************************************************************
#include "utest.h"

#ifndef UT_V52
#include <event_flags.h>
#include <semaphore.h>

using namespace OS;

namespace
{
    const unsigned INTERRUPTS = 20000;
    volatile unsigned Irqs;

    // reads the value without a critical section (and without its system calls),
    // so the loop under test is short and an interrupt hits the window often
    struct TFlagsProbe : public TEventFlags
    {
        uint32_t raw() const { return Value; }
    };
    struct TSemProbe : public TSemaphore
    {
        TSemProbe(size_t max, size_t init) : TSemaphore(max, init) { }
        size_t raw() const { return Count; }
    };
}

// The interrupt sets bit 1. The process clears it, notes the interrupt count,
// makes the call under test on bit 0 and notes the count again. If an interrupt
// came in between the two notes, it set bit 1 after the process cleared it, and
// nothing but the call under test could have dropped it since.
UT_TEST(atomic, event_flags_clear_keeps_bits_set_by_an_interrupt)
{
    static TFlagsProbe F;
    static unsigned Lost, Checked;
    ut::async_ticks(25);
    ut::max_ticks(10 * INTERRUPTS);
    ut::on_systimer([]{ F.set_isr(0x2); Irqs = Irqs + 1; });
    ut::proc(0, []{
        while(Irqs < INTERRUPTS)
        {
            F.clear(0x2);
            unsigned before = Irqs;
            F.clear(0x1);                               // under test
            unsigned after = Irqs;
            if(after != before) { ++Checked; if(!(F.raw() & 0x2)) ++Lost; }
        }
        CHECK_EQ(Lost, 0);
        CHECK(Checked > 0);                             // interrupts did arrive inside the call
    });
}

UT_TEST(atomic, event_flags_try_wait_keeps_bits_set_by_an_interrupt)
{
    static TFlagsProbe F;
    static unsigned Lost, Checked;
    ut::async_ticks(25);
    ut::max_ticks(10 * INTERRUPTS);
    ut::on_systimer([]{ F.set_isr(0x2); Irqs = Irqs + 1; });
    ut::proc(0, []{
        while(Irqs < INTERRUPTS)
        {
            F.set(0x1);
            F.clear(0x2);
            unsigned before = Irqs;
            bool ok = F.try_wait(0x1, efWaitAny);       // under test: clears bit 0
            unsigned after = Irqs;
            if(after != before) { ++Checked; if(!(F.raw() & 0x2)) ++Lost; }
            CHECK(ok);
        }
        CHECK_EQ(Lost, 0);
        CHECK(Checked > 0);
    });
}

// The semaphore always has tokens, so every try_acquire() decrements the count
// while the interrupt increments it. No token may be lost or invented.
UT_TEST(atomic, semaphore_try_acquire_keeps_tokens_released_by_an_interrupt)
{
    static const size_t INITIAL = size_t(1) << 40;
    static TSemProbe S(size_t(1) << 41, INITIAL);
    static volatile size_t Released;
    static size_t Acquired;
    ut::async_ticks(25);
    ut::max_ticks(10 * INTERRUPTS);
    ut::on_systimer([]{
        if(S.release_isr()) Released = Released + 1;
        Irqs = Irqs + 1;
    });
    ut::proc(0, []{
        while(Irqs < INTERRUPTS)
            if(S.try_acquire()) ++Acquired;
        TCritSect cs;
        CHECK(S.raw() + Acquired == INITIAL + Released);
        CHECK(Released > 0);
    });
}

#endif // UT_V52
