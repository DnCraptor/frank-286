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
bool bios_15h_88h() {
    cf = 0;
    CPU_AX = (uint16_t)cmos_read(0x17) | ((uint16_t)cmos_read(0x18) << 8);
    return true;
}

bool bios_15h() {
    switch(CPU_AH) {
        case 0x88:
            return bios_15h_88h(); // GET EXTENDED MEMORY SIZE (286+)
        default:
            no_handler();
        case 0x41: // WAIT ON EXTERNAL EVENT (CONVERTIBLE and some others)
            // unsupported
    }
    cf = 1;
    CPU_AH = 0x86;   // unsupported function
    return true;
}
