#include <stdbool.h>
#include <stdint.h>
#include "i286.h"
#include "bios.h"

#define BDA_KBD_FLAGS1  0x417u

#define KBD_FLAG_RSHIFT 0x01u
#define KBD_FLAG_LSHIFT 0x02u
#define KBD_FLAG_CTRL   0x04u
#define KBD_FLAG_ALT    0x08u
#define KBD_FLAG_SCROLL 0x10u
#define KBD_FLAG_NUM    0x20u
#define KBD_FLAG_CAPS   0x40u
#define KBD_FLAG_INS    0x80u

static uint8_t ext_prefix;

static char scan_to_ascii(uint8_t scan, bool shift, bool ctrl, bool caps)
{
    if (ctrl) {
        switch (scan) {
        case 0x1C: return 0x0A; /* Ctrl-Enter, minimal */
        case 0x0E: return 0x7F; /* Ctrl-Backspace */
        default: break;
        }
    }

    switch (scan) {
    case 0x01: return 0x1B;
    case 0x0E: return 0x08;
    case 0x0F: return 0x09;
    case 0x1C: return 0x0D;
    case 0x39: return ' ';
    }

    static const char normal[] = {
        0, 0, '1','2','3','4','5','6','7','8','9','0','-','=', 0, 0,
        'q','w','e','r','t','y','u','i','o','p','[',']', 0, 0,
        'a','s','d','f','g','h','j','k','l',';','\'', '`', 0, '\\',
        'z','x','c','v','b','n','m',',','.','/'
    };
    static const char shifted[] = {
        0, 0, '!','@','#','$','%','^','&','*','(',')','_','+', 0, 0,
        'Q','W','E','R','T','Y','U','I','O','P','{','}', 0, 0,
        'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
        'Z','X','C','V','B','N','M','<','>','?'
    };

    if (scan >= sizeof(normal))
        return 0;

    char c = shift ? shifted[scan] : normal[scan];
    if (c >= 'a' && c <= 'z' && caps)
        c = (char)(c - 'a' + 'A');
    else if (c >= 'A' && c <= 'Z' && caps)
        c = (char)(c - 'A' + 'a');
    return c;
}

bool bios_09h(void)
{
    /* Check OBF (Output Buffer Full) in i8042 status register.
     * If clear — spurious IRQ1, nothing to read. */
    if (!(cpu_portin8(0x64) & 0x01)) {
        cpu_portout8(0x20, 0x20);
        return true;
    }

    uint8_t code = cpu_portin8(0x60);

    if (code == 0xE0 || code == 0xE1) {
        ext_prefix = code;
        cpu_portout8(0x20, 0x20);
        return true;
    }

    bool is_up = (code & 0x80u) != 0;
    uint8_t scan = code & 0x7Fu;
    if (scan == 0) {
        cpu_portout8(0x20, 0x20);
        return true;
    }    
    uint8_t flags = read86(BDA_KBD_FLAGS1);

    switch (scan) {
    case 0x2A:
        if (!is_up) flags |= KBD_FLAG_LSHIFT; else flags &= (uint8_t)~KBD_FLAG_LSHIFT;
        write86(BDA_KBD_FLAGS1, flags);
        ext_prefix = 0;
        cpu_portout8(0x20, 0x20);
        return true;
    case 0x36:
        if (!is_up) flags |= KBD_FLAG_RSHIFT; else flags &= (uint8_t)~KBD_FLAG_RSHIFT;
        write86(BDA_KBD_FLAGS1, flags);
        ext_prefix = 0;
        cpu_portout8(0x20, 0x20);
        return true;
    case 0x1D:
        if (!is_up) flags |= KBD_FLAG_CTRL; else flags &= (uint8_t)~KBD_FLAG_CTRL;
        write86(BDA_KBD_FLAGS1, flags);
        ext_prefix = 0;
        cpu_portout8(0x20, 0x20);
        return true;
    case 0x38:
        if (!is_up) flags |= KBD_FLAG_ALT; else flags &= (uint8_t)~KBD_FLAG_ALT;
        write86(BDA_KBD_FLAGS1, flags);
        ext_prefix = 0;
        cpu_portout8(0x20, 0x20);
        return true;
    case 0x3A:
        if (!is_up) { flags ^= KBD_FLAG_CAPS; write86(BDA_KBD_FLAGS1, flags); }
        ext_prefix = 0;
        cpu_portout8(0x20, 0x20);
        return true;
    case 0x45:
        if (!is_up) { flags ^= KBD_FLAG_NUM; write86(BDA_KBD_FLAGS1, flags); }
        ext_prefix = 0;
        cpu_portout8(0x20, 0x20);
        return true;
    case 0x46:
        if (!is_up) { flags ^= KBD_FLAG_SCROLL; write86(BDA_KBD_FLAGS1, flags); }
        ext_prefix = 0;
        cpu_portout8(0x20, 0x20);
        return true;
    case 0x52:
        if (!is_up && ext_prefix == 0) { flags ^= KBD_FLAG_INS; write86(BDA_KBD_FLAGS1, flags); }
        break;
    default:
        break;
    }

    if (!is_up) {
        bool shift = (flags & (KBD_FLAG_LSHIFT | KBD_FLAG_RSHIFT)) != 0;
        bool ctrl  = (flags & KBD_FLAG_CTRL) != 0;
        bool caps  = (flags & KBD_FLAG_CAPS) != 0;
        uint8_t ascii = (uint8_t)scan_to_ascii(scan, shift, ctrl, caps);
        uint16_t ax = ((uint16_t)scan << 8) | ascii;

        if (ext_prefix == 0xE0)
            ax = ((uint16_t)scan << 8); /* minimal: keep enhanced keys non-ASCII */
        if (ax != 0)
            bios_16h_store_key(ax);
    }

    ext_prefix = 0;
    cpu_portout8(0x20, 0x20);
    return true;
}
