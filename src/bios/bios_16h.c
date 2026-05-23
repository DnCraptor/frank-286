#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "i286.h"
#include "bios.h"

#define BDA_BASE        0x400u
#define BDA_KBD_FLAGS1  0x417u
#define BDA_KBD_FLAGS2  0x418u
#define BDA_KBD_HEAD    0x41Au
#define BDA_KBD_TAIL    0x41Cu
#define BDA_KBD_START   0x480u
#define BDA_KBD_END     0x482u

static uint16_t kbd_start(void) { uint16_t v = readw86(BDA_KBD_START); return v ? v : 0x001E; }
static uint16_t kbd_end(void)   { uint16_t v = readw86(BDA_KBD_END);   return v ? v : 0x003E; }

static uint16_t kbd_next(uint16_t p)
{
    p += 2;
    if (p >= kbd_end()) p = kbd_start();
    return p;
}

static bool kbd_empty(void)
{
    return readw86(BDA_KBD_HEAD) == readw86(BDA_KBD_TAIL);
}

static uint16_t kbd_peek(void)
{
    return readw86(BDA_BASE + readw86(BDA_KBD_HEAD));
}

static uint16_t kbd_pop(void)
{
    uint16_t head = readw86(BDA_KBD_HEAD);
    uint16_t ax = readw86(BDA_BASE + head);
    writew86(BDA_KBD_HEAD, kbd_next(head));
    return ax;
}

bool bios_16h_store_key(uint16_t ax)
{
    uint16_t tail = readw86(BDA_KBD_TAIL);
    uint16_t next = kbd_next(tail);

    if (next == readw86(BDA_KBD_HEAD))
        return false;

    writew86(BDA_BASE + tail, ax);
    writew86(BDA_KBD_TAIL, next);
    return true;
}

bool bios_16h(void)
{
    switch (CPU_AH) {
    case 0x00: /* read keystroke */
    case 0x10: /* enhanced read keystroke */
        if (kbd_empty()) {
            /* Set IF=1 in the flags word already pushed on stack by intcall86,
             * so that after any IRQ's IRET we still have interrupts enabled. */
            uint32_t sp_phys = ((uint32_t)CPU_SS << 4) + (uint16_t)CPU_SP;
            writew86(sp_phys + 4, readw86(sp_phys + 4) | 0x0200); /* IF bit */
            ifl = 1; /* allow IRQs while waiting for keypress */
            return false; /* block: re-enter handler next CPU step */
        }
        { char buf[64]; snprintf(buf, sizeof(buf), "pop h=%04x t=%04x s=%04x e=%04x v=%04x",
            readw86(BDA_KBD_HEAD), readw86(BDA_KBD_TAIL),
            readw86(BDA_KBD_START), readw86(BDA_KBD_END),
            readw86(BDA_BASE + readw86(BDA_KBD_HEAD))); print_line(buf, 14); }

        CPU_AX = kbd_pop();
        cf = 0;
        zf = 0;        
        return true;

    case 0x01: /* check keystroke */
    case 0x11: /* enhanced check keystroke */
        if (kbd_empty()) {
            zf = 1;
            return true;
        }
        CPU_AX = kbd_peek();
        zf = 0;
        return true;

    case 0x02: /* get shift flags */
        CPU_AL = read86(BDA_KBD_FLAGS1);
        return true;

    case 0x05: /* store keystroke: CH=scancode, CL=ASCII */
        CPU_AL = bios_16h_store_key(((uint16_t)CPU_CH << 8) | CPU_CL) ? 0x00 : 0x01;
        return true;

    case 0x12: /* get extended shift flags */
        CPU_AL = read86(BDA_KBD_FLAGS1);
        CPU_AH = read86(BDA_KBD_FLAGS2);
        return true;

    default:
        CPU_AX = 0;
        cf = 1;
        return true;
    }
}
