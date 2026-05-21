#ifndef BIOS_H
#define BIOS_H

#include <stdbool.h>

bool bios_00h(); // Division by zero, etc...
bool bios_10h(); // VIDEO 
bool bios_18h(); // Call internal Basic
bool bios_19h(); // Bootstrap

#endif // BIOS_H
