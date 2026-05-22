#ifndef BIOS_H
#define BIOS_H

#include <stdbool.h>

bool bios_00h(); // Division by zero, etc...
bool bios_08h(); // IRQ0 timer tick
bool bios_10h(); // VIDEO 
bool bios_13h(); // DISK 
bool bios_18h(); // Call internal Basic
bool bios_19h(); // Bootstrap
bool bios_1Ah(); // Time/Date services

#endif // BIOS_H
