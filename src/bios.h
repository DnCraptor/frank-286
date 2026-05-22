#ifndef BIOS_H
#define BIOS_H

#include <stdbool.h>

bool bios_00h(); // Division by zero, etc...
bool bios_08h(); // IRQ0 timer tick
bool bios_09h(); // IRQ1 keyboard
bool bios_10h(); // VIDEO 
bool bios_13h(); // DISK 
bool bios_15h(); // TSR
bool bios_16h(); // KEYBOARD
bool bios_18h(); // Call internal Basic
bool bios_19h(); // Bootstrap
bool bios_1Ah(); // Time/Date services

bool bios_16h_store_key(uint16_t ax); // shared with INT 9

#endif // BIOS_H
