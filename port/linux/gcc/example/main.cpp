//******************************************************************************
//*
//*     scmRTOS Linux test port: example
//*
//*     Three processes in real time (1 ms system timer):
//*       - Blinker  (pr0): toggles a "LED" every 250 ms;
//*       - Consumer (pr1): receives numbers from a channel and prints them;
//*       - Producer (pr2): an interrupt (user IRQ 0, from a POSIX timer every
//*         100 ms) signals an event flag, the producer sends a counter value.
//*     Exits after 2 seconds.
//*
//*     libc functions (printf, malloc) are not reentrant: a process can be
//*     preempted inside them. Call them under a mutex, as here, or inside
//*     a critical section.
//*
//******************************************************************************
#include <scmRTOS.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>

typedef OS::process<OS::pr0, 64*1024> TBlinker;
typedef OS::process<OS::pr1, 64*1024> TConsumer;
typedef OS::process<OS::pr2, 64*1024> TProducer;

TBlinker  Blinker("Blinker");
TConsumer Consumer("Consumer");
TProducer Producer("Producer");

OS::TEventFlag          TimerEvent;
OS::channel<int, 8>     Numbers;
OS::TMutex              ConsoleMutex;

#define LOG(...) do { OS::TMutexLocker lock(ConsoleMutex); printf("[%5u ms] ", (unsigned)OS::get_tick_count()); printf(__VA_ARGS__); fflush(stdout); } while(0)

//------------------------------------------------------------------------------
// User interrupt 0: a hardware timer in a real system
static void timer_irq()
{
    OS::TISRW isr;
    TimerEvent.signal_isr();
}

namespace OS
{
    template<> void TBlinker::exec()
    {
        bool led = false;
        for(;;)
        {
            sleep(250);
            led = !led;
            LOG("LED %s\n", led ? "on" : "off");
            if(get_tick_count() >= 2000)
            {
                TCritSect cs;
                printf("stack slack, bytes: Blinker %zu, Consumer %zu, Producer %zu\n",
                       Blinker.stack_slack()*sizeof(stack_item_t),
                       Consumer.stack_slack()*sizeof(stack_item_t),
                       Producer.stack_slack()*sizeof(stack_item_t));
                exit(0);
            }
        }
    }

    template<> void TConsumer::exec()
    {
        for(;;)
        {
            int n;
            if(Numbers.pop(n, 500))
                LOG("received %d\n", n);
            else
                LOG("nothing for 500 ms\n");
        }
    }

    template<> void TProducer::exec()
    {
        int counter = 0;
        for(;;)
        {
            TimerEvent.wait();
            Numbers.push(++counter);
        }
    }
}

int main()
{
    // POSIX timer -> user interrupt 0 every 100 ms
    OS::host::set_irq_handler(0, timer_irq);
    struct sigevent sev = {};
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo  = OS::host::irq_signal(0);
    timer_t t;
    timer_create(CLOCK_MONOTONIC, &sev, &t);
    struct itimerspec its = { { 0, 100000000 }, { 0, 100000000 } };
    timer_settime(t, 0, &its, 0);

    OS::run();
}
