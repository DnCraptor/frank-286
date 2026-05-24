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


static bool no_handler() {
    print_line("KEYBOARD BIOS - ERROR: no handler defined", 1);
    char buf[10];
    snprintf(buf, 10, "AX: %04xh", CPU_AX); print_line(buf, 2);
    snprintf(buf, 10, "BX: %04xh", CPU_BX); print_line(buf, 3);
    snprintf(buf, 10, "CX: %04xh", CPU_CX); print_line(buf, 4);
    snprintf(buf, 10, "DX: %04xh", CPU_DX); print_line(buf, 5);
    snprintf(buf, 10, "SI: %04xh", CPU_SI); print_line(buf, 5);
    snprintf(buf, 10, "DI: %04xh", CPU_DI); print_line(buf, 6);
    snprintf(buf, 10, "BP: %04xh", CPU_BP); print_line(buf, 7);
    snprintf(buf, 10, "DS: %04xh", CPU_DS); print_line(buf, 8);
    snprintf(buf, 10, "SS: %04xh", CPU_SS); print_line(buf, 9);
    snprintf(buf, 10, "FS: %04xh", CPU_FS); print_line(buf, 10);
    snprintf(buf, 10, "GS: %04xh", CPU_GS); print_line(buf, 11);
    snprintf(buf, 10, "ES: %04xh", CPU_ES); print_line(buf, 12);
while(1); // remove it
    return true;
}

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
        { char buf[64]; snprintf(buf, sizeof(buf), "pop h=%04x t=%04x s=%04x e=%04x v=%04x",
            readw86(BDA_KBD_HEAD), readw86(BDA_KBD_TAIL),
            readw86(BDA_KBD_START), readw86(BDA_KBD_END),
            readw86(BDA_BASE + readw86(BDA_KBD_HEAD))); print_line(buf, 14); }

        if (kbd_empty()) {
            return false; /* block: re-enter handler next CPU step */
        }

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

    case 0xFF: /* KBUF extensions: add key to tail, DX=scancode/key word */
        CPU_AL = bios_16h_store_key(CPU_DX) ? 0x00 : 0x01;
        cf = 0;
        return true;

    case 0x12: /* get extended shift flags */
        CPU_AL = read86(BDA_KBD_FLAGS1);
        CPU_AH = read86(BDA_KBD_FLAGS2);
        return true;

    default:
        no_handler();
        CPU_AH = 0x86;
        cf = 1;
        return true;
    }
}
