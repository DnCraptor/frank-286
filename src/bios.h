#ifndef BIOS_H
#define BIOS_H

#include <stdbool.h>

bool bios_00h(); // Division by zero, etc...
bool bios_05h(); // PRINT SCREEN / BOUND RANGE EXCEEDED
bool bios_08h(); // IRQ0 timer tick
bool bios_09h(); // IRQ1 keyboard
bool bios_10h(); // VIDEO
bool bios_11h(); // EQUIPMENT LIST
bool bios_12h(); // LOW MEM SIZE
bool bios_13h(); // DISK 
bool bios_14h(); // SERIAL
bool bios_15h(); // TSR
bool bios_16h(); // KEYBOARD
bool bios_18h(); // Call internal Basic
bool bios_19h(); // Bootstrap
bool bios_1Ah(); // Time/Date services

bool bios_16h_store_key(uint16_t ax); // shared with INT 9
void bios_10h_install_rom_fonts(void); // INT 10h support
void install_floppy_dpt(void); // INT 13h support

// Адреса в ROM для DPTE и структур
#define DPTE_ADDR_0   0xFFF50u
#define DPTE_ADDR_1   0xFFF60u

#endif // BIOS_H
