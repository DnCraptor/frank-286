#include <stdio.h>
#include "i286.h"
#include "bios.h"

static bool no_handler() {
    print_line("TSR BIOS - ERROR: no handler defined", 1);
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

// A20 GATE
static bool bios_15h_24h() {
    switch (CPU_AL) {
    case 0x00:              /* disable A20 */
        a20_enabled = 0;
        CPU_AH = 0x00;
        cf = 0;
        return true;
    case 0x01:              /* enable A20 */
        a20_enabled = 1;
        CPU_AH = 0x00;
        cf = 0;
        return true;
    case 0x02:              /* get A20 status */
        CPU_AL = (uint8_t)a20_enabled;
        CPU_AH = 0x00;
        cf = 0;
        return true;
    case 0x03:              /* get A20 support — keyboard ctrl + port 0x92 */
        CPU_BX = 0x0003;
        CPU_AH = 0x00;
        cf = 0;
        return true;
    default:
        cf = 1;
        CPU_AH = 0x86;      /* unsupported subfunction */
        return true;
    }
}

/*
SYSTEM - GET EXTENDED MEMORY SIZE (286+)
AH = 88h

Return:
CF clear if successful
AX = number of contiguous KB starting at absolute address 100000h
CF set on error
AH = status
80h invalid command (PC,PCjr)
86h unsupported function (XT,PS30)

Notes: TSRs which wish to allocate extended memory to themselves often hook this call, and return a reduced memory size.
They are then free to use the memory between the new and old sizes at will..
The standard BIOS only returns memory between 1MB and 16MB; use AH=C7h for memory beyond 16MB.
Not all BIOSes correctly return the carry flag, making this call unreliable unless one first checks whether it is supported through
a mechanism other than calling the function and testing CF. Due to applications not dealing with more than 24-bit descriptors (286),
Windows 3.0 has problems when this function reports more than 15 MB. Some releases of HIMEM.SYS are therefore limited to use only 15 MB,
even when this function reports more.
*/
static bool bios_15h_88h() {
    cf = 0;
    CPU_AX = (uint16_t)cmos_read(0x17) | ((uint16_t)cmos_read(0x18) << 8);
    return true;
}

/*
SYSTEM - GET CONFIGURATION (XT >1986/1/10,AT mdl 3x9,CONV,XT286,PS)
AH = C0h

Return:
CF set if BIOS doesn't support call
CF clear on success
ES:BX -> ROM table (see #00509)
AH = status
00h successful
The PC XT (since 1986/01/10), PC AT (since 1985/06/10), the
PC XT Model 286, the PC Convertible and most PS/2 machines
will clear the CF flag and return the table in ES:BX.
80h unsupported function
The PC and PCjr return AH=80h/CF set
86h unsupported function
The PC XT (1982/11/08), PC Portable, PC AT (1984/01/10),
or PS/2 prior to Model 30 return AH=86h/CF set

Format of ROM configuration table:

Offset  Size    Description     (Table 00509)
00h    WORD    number of bytes following
02h    BYTE    model (see #00515)
03h    BYTE    submodel (see #00515)

04h    BYTE    BIOS revision:
0 for first release, 1 for 2nd, etc.
05h    BYTE    feature byte 1 (see #00510)
06h    BYTE    feature byte 2 (see #00511)
07h    BYTE    feature byte 3 (see #00512)
08h    BYTE    feature byte 4 (see #00513)
09h    BYTE    feature byte 5 (see #00514)
??? (08h) (Phoenix 386 v1.10)
??? (0Fh) (Phoenix 486 v1.03 PCI)
---AWARD BIOS---
0Ah  N BYTEs   AWARD copyright notice
---Phoenix BIOS---
0Ah    BYTE    ??? (00h)
0Bh    BYTE    major version
0Ch    BYTE    minor version (BCD)
0Dh  4 BYTEs   ASCIZ string "PTL" (Phoenix Technologies Ltd)
also on Phoenix Cascade BIOS
---Quadram Quad386---
0Ah 17 BYTEs   ASCII signature string "Quadram Quad386XT"
---Toshiba (Satellite Pro 435CDS at least)---
0Ah  7 BYTEs   signature "TOSHIBA"
11h    BYTE    ??? (8h)
12h    BYTE    ??? (E7h) product ID??? (guess)
13h  3 BYTEs   "JPN"


Bitfields for feature byte 1:

Bit(s)  Description     (Table 00510)
7      DMA channel 3 used by hard disk BIOS
6      2nd interrupt controller (8259) installed
5      Real-Time Clock installed
4      INT 15/AH=4Fh called upon INT 09h
3      wait for external event (INT 15/AH=41h) supported
2      extended BIOS area allocated (usually at top of RAM)
1      bus is Micro Channel instead of ISA
0      system has dual bus (Micro Channel + ISA)

See Also: #00509 - #00511
*/
static bool bios_15h_C0h() {
    /*
     * INT 15h / AH=C0h - GET CONFIGURATION
     *
     * Return:
     *   CF clear
     *   AH = 00h
     *   ES:BX -> ROM configuration table
     *
     * Minimal IBM PC/AT-compatible table:
     *   model        = FCh  IBM PC AT
     *   submodel     = 00h
     *   BIOS rev     = 00h
     *   feature byte = 8259 slave + RTC
     *
     * Feature byte 1:
     *   bit 6 = second interrupt controller installed
     *   bit 5 = RTC installed
     *
     * We do NOT set bit 3, because INT 15h/AH=41h is currently unsupported.
     * We do NOT set bit 2, because EBDA segment at BDA 0040:000E is zero.
     * @See: load_bios_and_reset
     */
    CPU_AH = 0x00;
    CPU_ES = 0xFFF0;
    CPU_BX = 0x0010;
    cf = 0;
    return true;
}

bool bios_15h() {
    switch(CPU_AH) {
        case 0x24:
            return bios_15h_24h(); // A20 GATE
        case 0x88:
            return bios_15h_88h(); // GET EXTENDED MEMORY SIZE (286+)
        case 0xC0:
            return bios_15h_C0h(); // GET CONFIGURATION
        default:
            no_handler();
        case 0x41: // WAIT ON EXTERNAL EVENT (CONVERTIBLE and some others)
        case 0x4F:  /* keyboard intercept — not hooked, pass through */
            // unsupported
    }
    cf = 1;
    CPU_AH = 0x86;   // unsupported function
    return true;
}
