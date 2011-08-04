#include <dev/timer.h>

#include <dev/intrs.h>
#include <arch/i386.h>

#define PIT_CH0_PORT    0x40
#define PIT_CH1_PORT    0x41
#define PIT_CH3_PORT    0x42
#define PIT_CMD_PORT    0x43

timer_event_f timers[N_TIMERS] = { 0 };

timer_t timer_push_ontimer(timer_event_f ontimer) {
    int i = 0;
    for (i = 0; i < N_TIMERS; ++i)
        if (null == timers[i]) {
            timers[i] = ontimer;
            return i;
        }
    return i;
}

void timer_pop_ontimer(timer_t id) {
    timers[id] = null;
}

void timer_set_frequency(uint hz) {
    uint divisor = PIT_MAX_FREQ / hz;
    outb(PIT_CMD_PORT, 0x36);
    outb(PIT_CH0_PORT, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_PORT, (uint8_t)(divisor >> 8));
}

void timer_setup(void) {
    memset(timers, 0, N_TIMERS * sizeof(timer_event_f));

    irq_mask(true, 0);
    irq_set_handler(0, timer_irq);
}

void timer_irq() {
    static uint32_t counter = 0;
    ++counter;

    int i;
    for (i = 0; i < N_TIMERS; ++i)
        if (timers[i])
            timers[i](counter); 
}

