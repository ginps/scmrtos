//******************************************************************************
//*
//*     Linux port self-test, manual tick mode (deterministic)
//*
//*     Checks the exact order of process switches for: start-up, blocking,
//*     interrupt -> process wake-up with preemption at interrupt exit, a wake-up
//*     made with a non-ISR call (force_wake_up) from an interrupt, preemption
//*     by the system timer, channel hand-over, system timer lock, stack slack.
//*
//******************************************************************************
#include <scmRTOS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef OS::process<OS::pr0, 64*1024> THigh;
typedef OS::process<OS::pr1, 64*1024> TMid;
typedef OS::process<OS::pr2, 64*1024> TLow;
THigh High("High");
TMid  Mid("Mid");
TLow  Low("Low");

OS::TEventFlag     Irq0Event;
OS::channel<int,4> Ch;

static char Log[256];
static void log(const char * s) { TCritSect cs; strcat(Log, s); strcat(Log, " "); }

static void irq0() { OS::TISRW isr; Irq0Event.signal_isr(); }
static void irq1() { OS::TISRW isr; Mid.force_wake_up(); }    // non-ISR call inside an ISR: switch deferred to ISR exit

namespace OS
{
    template<> void THigh::exec()
    {
        log("H0");
        Irq0Event.wait();
        log("H1");
        int x = 0;
        Ch.pop(x);
        log(x == 7 ? "H2" : "H2:bad");
        for(;;) sleep();
    }

    template<> void TMid::exec()
    {
        log("M0");
        sleep(2);
        log("M1");
        Ch.push(7);                 // High preempts here
        log("M2");
        sleep();                    // until irq1
        log("M3");
        for(;;) sleep();
    }

    template<> void TLow::exec()
    {
        log("L0");
        sim_tick();                 // tick 1
        log("L1");
        host::raise_irq(0);         // High wakes and preempts at interrupt exit
        log("L2");
        sim_tick();                 // tick 2: Mid wakes and preempts; Mid wakes High through the channel
        log("L3");
        host::raise_irq(1);         // Mid wakes at interrupt exit
        log("L4");

        lock_system_timer();
        sim_tick();                 // dropped
        unlock_system_timer();
        tick_count_t ticks = get_tick_count();

        {
            TCritSect cs;           // interrupt requested inside a critical section is taken at its end
            sim_tick();
            log(get_tick_count() == ticks ? "C0" : "C0:bad");
        }
        log(get_tick_count() == ticks + 1 ? "C1" : "C1:bad");

        const char * expected = "H0 M0 L0 L1 H1 L2 M1 H2 M2 L3 M3 L4 C0 C1 ";
        bool ok_order = strcmp(Log, expected) == 0;
        bool ok_ticks = ticks == 2;
        size_t slack = Low.stack_slack() * sizeof(stack_item_t);
        bool ok_slack = slack > 0 && slack < Low.stack_size() * sizeof(stack_item_t);

        printf("%s manual: switch order  %s\n", ok_order ? "PASS" : "FAIL", Log);
        if(!ok_order) printf("              expected  %s\n", expected);
        printf("%s manual: tick count %u after a locked tick (expected 2)\n", ok_ticks ? "PASS" : "FAIL", (unsigned)ticks);
        printf("%s manual: stack slack of Low %zu of %zu bytes\n", ok_slack ? "PASS" : "FAIL", slack, Low.stack_size()*sizeof(stack_item_t));
        exit(ok_order && ok_ticks && ok_slack ? 0 : 1);
    }
}

int main()
{
    OS::host::set_irq_handler(0, irq0);
    OS::host::set_irq_handler(1, irq1);
    OS::run();
}
