//******************************************************************************
//*
//*     Linux port self-test, real-time mode (1 ms SIGALRM ticks, preemptive)
//*
//*     pr0  IrqWaiter : waits for an event flag set by user IRQ 0 every 0.5 ms
//*     pr1  Sleeper   : sleep(10) x 30, measures wall-clock duration
//*     pr2  Consumer  : pops a sequence from a channel and checks it
//*     pr3  Counter   : bursts of increments of a mutex-protected counter, sleep(1)
//*     pr4  Producer  : pushes the sequence as fast as it can and increments the
//*                      same counter under the mutex (never sleeps)
//*
//*     Producer is CPU-bound, so every switch to the processes above it is a
//*     preemption from an interrupt; Counter preempts it while it holds the mutex
//*     and has to block on it.
//*
//******************************************************************************
#include <scmRTOS.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

typedef OS::process<OS::pr0, 64*1024> TIrqWaiter;
typedef OS::process<OS::pr1, 64*1024> TSleeper;
typedef OS::process<OS::pr2, 64*1024> TConsumer;
typedef OS::process<OS::pr3, 64*1024> TCounter;
typedef OS::process<OS::pr4, 64*1024> TProducer;
TIrqWaiter IrqWaiter; TSleeper Sleeper; TConsumer Consumer; TProducer Producer; TCounter Counter;

OS::TEventFlag       IrqEvent;
OS::channel<unsigned, 8> Ch;
OS::TMutex           CountMutex;

static volatile unsigned IrqCount, IrqWakes, IrqTimeouts, Consumed, SeqErrors, ProtectedCount, CounterLoops, ProducerLoops, CounterBlocked;

static void irq0() { OS::TISRW isr; IrqCount = IrqCount + 1; IrqEvent.signal_isr(); }

static double now_ms() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

namespace OS
{
    template<> void TIrqWaiter::exec()
    {
        for(;;)
        {
            if(IrqEvent.wait(50)) IrqWakes = IrqWakes + 1;
            else                  IrqTimeouts = IrqTimeouts + 1;
        }
    }

    template<> void TSleeper::exec()
    {
        const int N = 30;
        double t_start = now_ms();
        tick_count_t tick_start = get_tick_count();
        double min = 1e9, max = 0;
        for(int i = 0; i < N; ++i)
        {
            double t0 = now_ms();
            sleep(10);
            double d = now_ms() - t0;
            if(d < min) min = d;
            if(d > max) max = d;
        }
        double wall = now_ms() - t_start;
        unsigned ticks = get_tick_count() - tick_start;

        TCritSect cs;
        unsigned counted;
        { counted = ProtectedCount; }
        // OS time is exact: sleep(10) right after a tick lasts 10 ticks. Wall-clock time is
        // only checked loosely: when the host deschedules this Linux process, SIGALRMs merge
        // and ticks are lost, so single sleeps may last longer than 10 ms.
        bool ok_sleep = ticks == unsigned(N*10) && min >= 8.9 && wall >= N*9 && wall < N*10*3.0;
        bool ok_irq   = IrqWakes > 100 && IrqTimeouts == 0;
        bool ok_chan  = Consumed > 1000 && SeqErrors == 0;
        bool ok_mutex = counted == CounterLoops + ProducerLoops && CounterLoops > 1000 && CounterBlocked > 10;
        printf("%s realtime: sleep(10) x %d: min %.2f ms, max %.2f ms, total %.0f ms, %u ticks\n", ok_sleep ? "PASS" : "FAIL", N, min, max, wall, ticks);
        printf("%s realtime: irq0 raised %u, waiter woke %u times, %u timeouts\n", ok_irq ? "PASS" : "FAIL", IrqCount, IrqWakes, IrqTimeouts);
        printf("%s realtime: channel: %u items consumed in order, %u sequence errors\n", ok_chan ? "PASS" : "FAIL", Consumed, SeqErrors);
        printf("%s realtime: mutex-protected counter %u = %u + %u increments, Counter found the mutex locked %u times\n",
               ok_mutex ? "PASS" : "FAIL", counted, CounterLoops, ProducerLoops, CounterBlocked);
        exit(ok_sleep && ok_irq && ok_chan && ok_mutex ? 0 : 1);
    }

    template<> void TConsumer::exec()
    {
        unsigned expected = 0;
        for(;;)
        {
            unsigned x;
            if(!Ch.pop(x, 100)) continue;
            if(x != expected) SeqErrors = SeqErrors + 1;
            expected = x + 1;
            Consumed = Consumed + 1;
        }
    }

    template<> void TCounter::exec()
    {
        for(;;)
        {
            for(int k = 0; k < 50; ++k)
            {
                if(CountMutex.is_locked()) CounterBlocked = CounterBlocked + 1;
                CountMutex.lock();
                unsigned c = ProtectedCount;
                for(volatile int d = 0; d < 200; ++d) { }   // widen the window for preemption
                ProtectedCount = c + 1;
                CounterLoops = CounterLoops + 1;
                CountMutex.unlock();
            }
            sleep(1);
        }
    }

    template<> void TProducer::exec()
    {
        for(unsigned i = 0;; ++i)
        {
            Ch.push(i);
            CountMutex.lock();
            unsigned c = ProtectedCount;
            for(volatile int d = 0; d < 2000; ++d) { }      // CPU work inside the mutex
            ProtectedCount = c + 1;
            ProducerLoops = ProducerLoops + 1;
            CountMutex.unlock();
        }
    }
}

int main()
{
    OS::host::set_irq_handler(0, irq0);
    struct sigevent sev = {};
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo  = OS::host::irq_signal(0);
    timer_t t;
    timer_create(CLOCK_MONOTONIC, &sev, &t);
    struct itimerspec its = { { 0, 500000 }, { 0, 500000 } };
    timer_settime(t, 0, &its, 0);
    OS::run();
}
