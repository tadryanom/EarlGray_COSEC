/*
 *      Caution: you examine this file at your peril,
 *  it might contain really bad temporary code.
 */

#include <misc/test.h>
#include <std/string.h>
#include <std/stdarg.h>
#include <std/stdio.h>
#include <dev/timer.h>
#include <arch/i386.h>
#include <kshell.h>


static void test_vsprint(const char *fmt, ...) {
#define buf_size    200
    char buf[buf_size];
    va_list ap;
    va_start(ap, fmt);
//    vsnprintf(buf, buf_size, fmt, ap);

    const char *c = fmt;
    print_mem((const char *)&fmt, 0x100);
    while (*c) {
        switch (*c) {
        case '%':
            switch (*(++c)) {
                case 's': {
                    char *s = va_arg(ap, char *); 
                    k_printf("%%s: 0x%x, *s=0x%x\n", (uint)s, (uint)*s);
                          } break;
                case 'd': case 'u': case 'x': {
                    int arg = va_arg(ap, int);
                    k_printf("%%d: 0x%x\n", (uint)arg);
                                              } break;
                default:
                    k_printf("Unknown operand for %%\n");
                        
            }
        }
        ++c;
    }

    va_end(ap);
    k_printf("\n%s\n", buf);
}

void test_sprintf(void) {
    test_vsprint("%s: %d, %s: %+x\n", "test1", -100, "test2", 200);
}

void test_eflags(void) {
    uint flags = 0;
    i386_eflags(flags);
    k_printf("flags=0x%x\n", flags);
}

static void on_timer(uint counter) {
    if (counter % 100 == 0) {
        k_printf("tick=%d\n", counter);
    }
}

void test_timer(void) {
    timer_set_frequency(0x1000);
    timer_push_ontimer(on_timer);
}


#include <dev/serial.h>
#include <dev/kbd.h>
#include <dev/screen.h>
#include <dev/intrs.h>

volatile bool poll_exit = false;

inline static void print_serial_info(uint16_t port) {
    uint8_t iir;
    inb(port + 1, iir);
    k_printf("(IER=%x\t", (uint)iir);
    inb(port + 2, iir);
    k_printf("IIR=%x\t", (uint)iir);
    inb(port + 5, iir);
    k_printf("LSR=%x\t", (uint)iir);
    inb(port + 6, iir);
    k_printf("MSR=%x", (uint)iir);
    k_printf("PIC=%x)", (uint)irq_get_mask());
}

void on_serial_received(uint8_t b) {
    set_cursor_attr(0x0A);
    cprint(b);
    update_hw_cursor();
}

static void on_press(scancode_t scan) {
    if (scan == 1) 
        poll_exit = true;
    if (scan == 0x53) 
        print_serial_info(COM1_PORT);

    char c = translate_from_scan(null, scan);
    if (c == 0) return;

    while (! serial_is_transmit_empty(COM1_PORT));
    serial_write(COM1_PORT, c);
    
    set_cursor_attr(0x0C);
    cprint(c);
    update_hw_cursor();
}

void poll_serial() {
    poll_exit = false;
    kbd_set_onpress(on_press);

    while (!poll_exit) {
        if (serial_is_received(COM1_PORT)) {
            uint8_t b = serial_read(COM1_PORT);

            set_cursor_attr(0x0A);
            cprint(b);
            update_hw_cursor();
        }
    }
    kbd_set_onpress(null);
}

void test_serial(void) {
    k_printf("IRQs state = 0x%x\n", (uint)irq_get_mask());

    uint8_t saved_color = get_cursor_attr();
    k_printf("Use <Esc> to quit, <Del> for register info\n");
    serial_setup();

    //poll_serial();
    
    poll_exit = false;
    serial_set_on_receive(on_serial_received);
    kbd_set_onpress(on_press);

    while (!poll_exit) cpu_halt();

    kbd_set_onpress(null);
    serial_set_on_receive(null);
    set_cursor_attr(saved_color);
}

void test_kbd(void) {
    k_printf("Use <Esc> to quit\n");
    uint8_t key = 0;
    while (key != 1) {
        key = kbd_wait_scan(true);
        k_printf("%x->%x\t", (uint)key, (uint)translate_from_scan(null, key));
    }
    k_printf("\n");
}


#include <tasks.h>

void test_tasks(void) {
    tasks_setup();
}
