#include "i286.h"
#include "bios.h"

/*
PRINT SCREEN
Desc: Dump the current text screen to the first printer

Notes: Normally invoked by the INT 09 handler when PrtSc key is pressed, but may be invoked directly by applications.
Byte at 0050h:0000h contains status used by default handler 00h not active 01h PrtSc in progress FFh last PrtSc encountered error.
Default handler is at F000h:FF54h in IBM PC and 100%-compatible BIOSes.
Since the BOUND instruction also calls INT 05h, but returns control to the BOUND instruction, a failed BOUND check will cause an
infinite loop of PrtScreens unless the INT 05 handler is aware of the problem and checks whether the interrupt was invoked by a BOUND instruction
*/
bool bios_05h(void) {
    if (CPU_CS == 0xFFFF && CPU_IP >= 0xFF00) { // print screen
        pstore8(0x500, 0xFF); // error on the attempt
        return true;
    }
    print_line("BOUND EXCEPTION", 23);
    return false; // bound exception, TODO: ???
}
