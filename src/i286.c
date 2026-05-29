#include <pico.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include "debug.h"
#include "i286.h"
#include "bios.h"

#define printf(...)

static i286* _cpu;

INLINE static u8 portin(u16 portnum) {
    return _cpu->cb.io_read8(_cpu->cb.io, portnum);
}
INLINE static u16 portin16(u16 portnum) {
    return _cpu->cb.io_read16(_cpu->cb.io, portnum);
}
INLINE static void portout(u16 portnum, u8 v) {
    _cpu->cb.io_write8(_cpu->cb.io, portnum, v);
}
INLINE static void portout16(u16 portnum, u16 v) {
    _cpu->cb.io_write16(_cpu->cb.io, portnum, v);
}

int a20_enabled = 0;

void cpu_set_a20(i286* cpu, int v) {
    (void*)cpu;
    a20_enabled = v;
}

void cpu_set_int2f_handler(i286 *cpu, int2f_handler_t handler, void *opaque) {
    cpu->int2f_handler = handler;
    cpu->int2f_opaque  = opaque;
}

static volatile int irq_pending = 0;
void cpu_raise_irq(void* o) {
    (void*)o;
    irq_pending = 1;
}

static bool fpu_on = false;
void i286_enable_fpu(i286*) {
    fpu_on = 1;
}

void init_umb();
void OpFpu(uint8_t opcode);

uint32_t dwordregs[8];
#define byteregs ((uint8_t*)dwordregs)
#define wordregs ((uint16_t*)dwordregs)

#define StepIP(x)  ip += x

#define getmem8(x, y) read86(segbase(x) + (y))
#define getmem16(x, y)  readw86(segbase(x) + (y))
#define getmem32(x, y)  readdw86(segbase(x) + (y))
#define putmem8(x, y, z)  write86(segbase(x) + (y), z)
#define putmem16(x, y, z) writew86(segbase(x) + (y), z)
#define putmem32(x, y, z) writedw86(segbase(x) + (y), z)
#define signext(value)  (int16_t)(int8_t)(value)
#define signext32(value)  (int32_t)(int16_t)(value)
#define getreg16(regid) wordregs[(regid) << 1]
#define getreg32(regid) dwordregs[regid]
#define getreg8(regid)  byteregs[byteregtable[regid]]
#define putreg16(regid, writeval) wordregs[(regid) << 1] = writeval
#define putreg32(regid, writeval) dwordregs[regid] = writeval
#define putreg8(regid, writeval)  byteregs[byteregtable[regid]] = writeval
#define getsegreg(regid)            segregs[(regid) << 1]
#define putsegreg(regid, writeval)  segregs[(regid) << 1] = writeval
#define segbase(x)  ((uint32_t) (x) << 4)

//#define CPU_ALLOW_ILLEGAL_OP_EXCEPTION
//#define CPU_LIMIT_SHIFT_COUNT
#define CPU_NO_SALC
//#define CPU_SET_HIGH_FLAGS
#define CPU_286_STYLE_PUSH_SP

#ifdef TOTAL_VIRTUAL_MEMORY_KBS
#undef __not_in_flash
#define __not_in_flash(group)
#endif

int videomode = 3;
uint8_t segoverride, reptype;
uint32_t segregs32[6];
uint16_t useseg, oldsp;
uint32_t ip32;
uint8_t tempcf, oldcf, mode, reg, rm, sib;
x86_flags_t x86_flags;
bool operandSizeOverride = false;
bool addressSizeOverride = false;

static const uint8_t __not_in_flash("cpu.regt") byteregtable[8] = {
    regal, regcl, regdl, regbl, regah, regch, regdh, regbh
};

uint8_t nestlev;
uint16_t saveip, savecs, oper1, oper2, res16, temp16, dummy, stacksize, frametemp;
uint32_t disp32;
#define disp16 (*(uint16_t*)&disp32)
uint32_t ea;

static const bool __not_in_flash("cpu.pf") parity[0x100] = {
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
};

__not_in_flash() void modregrm() {
    register uint8_t addrbyte = getmem8(CPU_CS, CPU_IP);
    StepIP(1);
    mode = addrbyte >> 6;
    reg = (addrbyte >> 3) & 7;
    rm = addrbyte & 7;
#ifdef CPU_386_EXTENDED_OPS
    if (addressSizeOverride) {
        // 32-битный адрес
        if (mode != 3 && rm == 4) {
            // SIB присутствует
            sib = getmem8(CPU_CS, CPU_IP);
            StepIP(1);
        }
        switch (mode) {
            case 0:
                if (rm == 5) {
                    disp32 = getmem32(CPU_CS, CPU_IP);
                    StepIP(4);
                } else {
                    disp32 = 0;
                }
                break;
            case 1:
                disp32 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                break;
            case 2:
                disp32 = getmem32(CPU_CS, CPU_IP);
                StepIP(4);
                break;
            default:
                disp32 = 0;
        }
        return;
    }
#endif
    switch (mode) {
        case 0:
            if (rm == 6) {
                disp16 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
            } else {
                disp16 = 0;
            }
            if (((rm == 2) || (rm == 3)) && !segoverride) {
                useseg = CPU_SS;
            }
            break;
        case 1:
            disp16 = signext(getmem8(CPU_CS, CPU_IP));
            StepIP(1);
            if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
                useseg = CPU_SS;
            }
            break;
        case 2:
            disp16 = getmem16(CPU_CS, CPU_IP);
            StepIP(2);
            if (((rm == 2) || (rm == 3) || (rm == 6)) && !segoverride) {
                useseg = CPU_SS;
            }
            break;
        default:
            disp16 = 0;
    }
}

__not_in_flash() void getea(uint8_t rmval) {
    register uint32_t tempea = 0;
#ifdef CPU_386_EXTENDED_OPS
    if (addressSizeOverride) {
        addressSizeOverride = false;
        if (operandSizeOverride) {
            operandSizeOverride = false;
            // Включена 32-битная адресация
            if (mode == 0 && rmval == 6) {
                tempea = disp32; // Используем 32-битный displacement
            } else {
                // Если Mode не 0 или RM не 6, считаем по обычной схеме с SIB
                switch (mode) {
                    case 0:
                        switch (rmval) {
                            case 0: tempea = CPU_EBX + CPU_ESI;
                                break;
                            case 1: tempea = CPU_EBX + CPU_EDI;
                                break;
                            case 2: tempea = CPU_EBP + CPU_ESI;
                                break;
                            case 3: tempea = CPU_EBP + CPU_EDI;
                                break;
                            case 4: tempea = CPU_ESI;
                                break;
                            case 5: tempea = CPU_EDI;
                                break;
                            case 6: tempea = disp32;
                                break; // DISP32
                            case 7: tempea = CPU_EBX;
                                break;
                        }
                        break;
                    case 1:
                    case 2:
                        switch (rmval) {
                            case 0: tempea = CPU_EBX + CPU_ESI + disp32;
                                break;
                            case 1: tempea = CPU_EBX + CPU_EDI + disp32;
                                break;
                            case 2: tempea = CPU_EBP + CPU_ESI + disp32;
                                break;
                            case 3: tempea = CPU_EBP + CPU_EDI + disp32;
                                break;
                            case 4: tempea = CPU_ESI + disp32;
                                break;
                            case 5: tempea = CPU_EDI + disp32;
                                break;
                            case 6: tempea = CPU_EBP + disp32;
                                break;
                            case 7: tempea = CPU_EBX + disp32;
                                break;
                        }
                        break;
                }
            }
            // Учитываем SIB, если нужно
            if (rmval == 4 && mode != 3) {
                // RM == 4 указывает на SIB
                uint8_t sib_scale = sib >> 6;
                uint8_t sib_index = (sib >> 3) & 7;
                uint8_t sib_base = sib & 7;
                uint32_t sib_value = 0;
                // Определим базовый регистр
                switch (sib_base) {
                    case 0: sib_value = CPU_EBX;
                        break;
                    case 1: sib_value = CPU_ECX;
                        break;
                    case 2: sib_value = CPU_EDX;
                        break;
                    case 3: sib_value = CPU_EBX;
                        break;
                    case 4: sib_value = CPU_ESP;
                        break;
                    case 5: sib_value = CPU_EBP;
                        break;
                    case 6: sib_value = CPU_ESI;
                        break;
                    case 7: sib_value = CPU_EDI;
                        break;
                }
                // Учитываем индекс (если есть)
                if (sib_index != 4) {
                    // если индекс не базовый (ESP, EBP и т.д.)
                    uint32_t index_value = getreg32(sib_index);
                    sib_value += (index_value << sib_scale); // Считываем с учётом масштаба
                }
                tempea += sib_value;
            }
            return;
        }
        switch (mode) {
            case 0:
                switch (rmval) {
                    case 0: tempea = CPU_EBX + CPU_ESI;
                        break;
                    case 1: tempea = CPU_EBX + CPU_EDI;
                        break;
                    case 2: tempea = CPU_EBP + CPU_ESI;
                        break;
                    case 3: tempea = CPU_EBP + CPU_EDI;
                        break;
                    case 4: tempea = CPU_ESI;
                        break;
                    case 5: tempea = CPU_EDI;
                        break;
                    case 6: tempea = disp32;
                        break;
                    case 7: tempea = CPU_EBX;
                        break;
                }
                break;
            case 1:
            case 2:
                switch (rmval) {
                    case 0: tempea = CPU_EBX + CPU_ESI + disp32;
                        break;
                    case 1: tempea = CPU_EBX + CPU_EDI + disp32;
                        break;
                    case 2: tempea = CPU_EBP + CPU_ESI + disp32;
                        break;
                    case 3: tempea = CPU_EBP + CPU_EDI + disp32;
                        break;
                    case 4: tempea = CPU_ESI + disp32;
                        break;
                    case 5: tempea = CPU_EDI + disp32;
                        break;
                    case 6: tempea = CPU_EBP + disp32;
                        break;
                    case 7: tempea = CPU_EBX + disp32;
                        break;
                }
                break;
        }
        ea = (tempea & 0xFFFF) + (useseg << 4);
        return;
    }
    if (operandSizeOverride) {
        operandSizeOverride = false;
        // Включена 32-битная адресация
        if (mode == 0 && rmval == 6) {
            tempea = disp32; // Используем 32-битный displacement
        } else {
            // Если Mode не 0 или RM не 6, считаем по обычной схеме с SIB
            switch (mode) {
                case 0:
                    switch (rmval) {
                        case 0: tempea = CPU_BX + CPU_SI;
                            break;
                        case 1: tempea = CPU_BX + CPU_DI;
                            break;
                        case 2: tempea = CPU_BP + CPU_SI;
                            break;
                        case 3: tempea = CPU_BP + CPU_DI;
                            break;
                        case 4: tempea = CPU_SI;
                            break;
                        case 5: tempea = CPU_DI;
                            break;
                        case 6: tempea = disp32;
                            break; // DISP32
                        case 7: tempea = CPU_BX;
                            break;
                    }
                    break;
                case 1:
                case 2:
                    switch (rmval) {
                        case 0: tempea = CPU_BX + CPU_SI + disp32;
                            break;
                        case 1: tempea = CPU_BX + CPU_DI + disp32;
                            break;
                        case 2: tempea = CPU_BP + CPU_SI + disp32;
                            break;
                        case 3: tempea = CPU_BP + CPU_DI + disp32;
                            break;
                        case 4: tempea = CPU_SI + disp32;
                            break;
                        case 5: tempea = CPU_DI + disp32;
                            break;
                        case 6: tempea = CPU_BP + disp32;
                            break;
                        case 7: tempea = CPU_BX + disp32;
                            break;
                    }
                    break;
            }
        }
        // Учитываем SIB, если нужно
        if (rmval == 4 && mode != 3) {
            // RM == 4 указывает на SIB
            uint8_t sib_scale = sib >> 6;
            uint8_t sib_index = (sib >> 3) & 7;
            uint8_t sib_base = sib & 7;
            uint32_t sib_value = 0;
            // Определим базовый регистр
            switch (sib_base) {
                case 0: sib_value = CPU_BX;
                    break;
                case 1: sib_value = CPU_CX;
                    break;
                case 2: sib_value = CPU_DX;
                    break;
                case 3: sib_value = CPU_BX;
                    break;
                case 4: sib_value = CPU_SP;
                    break;
                case 5: sib_value = CPU_BP;
                    break;
                case 6: sib_value = CPU_SI;
                    break;
                case 7: sib_value = CPU_DI;
                    break;
            }
            // Учитываем индекс (если есть)
            if (sib_index != 4) {
                // если индекс не базовый (ESP, EBP и т.д.)
                uint32_t index_value = getreg32(sib_index);
                sib_value += (index_value << sib_scale); // Считываем с учётом масштаба
            }
            tempea += sib_value;
        }
    }
#endif
    switch (mode) {
        case 0:
            switch (rmval) {
                case 0: tempea = CPU_BX + CPU_SI;
                    break;
                case 1: tempea = CPU_BX + CPU_DI;
                    break;
                case 2: tempea = CPU_BP + CPU_SI;
                    break;
                case 3: tempea = CPU_BP + CPU_DI;
                    break;
                case 4: tempea = CPU_SI;
                    break;
                case 5: tempea = CPU_DI;
                    break;
                case 6: tempea = disp16;
                    break;
                case 7: tempea = CPU_BX;
                    break;
            }
            break;

        case 1:
        case 2:
            switch (rmval) {
                case 0: tempea = CPU_BX + CPU_SI + disp16;
                    break;
                case 1: tempea = CPU_BX + CPU_DI + disp16;
                    break;
                case 2: tempea = CPU_BP + CPU_SI + disp16;
                    break;
                case 3: tempea = CPU_BP + CPU_DI + disp16;
                    break;
                case 4: tempea = CPU_SI + disp16;
                    break;
                case 5: tempea = CPU_DI + disp16;
                    break;
                case 6: tempea = CPU_BP + disp16;
                    break;
                case 7: tempea = CPU_BX + disp16;
                    break;
            }
            break;
    }
    ea = (tempea & 0xFFFF) + (useseg << 4);
}

static INLINE void push(uint16_t pushval) {
    CPU_SP = CPU_SP - 2;
    putmem16(CPU_SS, CPU_SP, pushval);
}

static INLINE uint16_t pop() {
    uint16_t tempval = getmem16(CPU_SS, CPU_SP);
    CPU_SP = CPU_SP + 2;
    return tempval;
}

static INLINE uint32_t readrm32(uint8_t rmval) {
    if (mode < 3) {
        getea(rmval);
        return readw86(ea);
    }
    return getreg32(rmval);
}

static INLINE uint16_t readrm16(uint8_t rmval) {
    if (mode < 3) {
        getea(rmval);
        return readw86(ea);
    }
    return getreg16(rmval);
}

static INLINE uint8_t readrm8(uint8_t rmval) {
    if (mode < 3) {
        getea(rmval);
        return read86(ea);
    }
    return getreg8(rmval);
}

static INLINE void writerm16(uint8_t rmval, uint16_t value) {
    if (mode < 3) {
        getea(rmval);
        writew86(ea, value);
    } else {
        putreg16(rmval, value);
    }
}

static INLINE void writerm32(uint8_t rmval, uint32_t value) {
    if (mode < 3) {
        getea(rmval);
        writedw86(ea, value);
    } else {
        putreg32(rmval, value);
    }
}

static INLINE void writerm8(uint8_t rmval, uint8_t value) {
    if (mode < 3) {
        getea(rmval);
        write86(ea, value);
    } else {
        putreg8(rmval, value);
    }
}

static INLINE uint16_t makeflagsword(void) {
#if CPU_386_EXTENDED_OPS
    return 2 | x86_flags.value;
#else
// Это включает OF(11), DF(10), IF(9), TF(8), SF(7), ZF(6), AF(4), PF(2), CF(0) — все стандартные 286 флаги.
    return 2 | (x86_flags.value & 0x0FD7);
#endif
}

static INLINE void decodeflagsword(uint16_t x) {
    x86_flags.value = x;
}

static bool no_handler() {
    print_line("ERROR: no handler defined", 1);
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
    snprintf(buf, 10, "CS: %04xh", CPU_CS); print_line(buf, 13);
    snprintf(buf, 10, "IP: %04xh", CPU_IP); print_line(buf, 14);
    debug_write("ERROR: no handler defined %04x:%04x)\n", CPU_CS, CPU_IP);
while(1); // remove it
    return true;
}

typedef bool (*handler_t)();
static handler_t handlers[256];
static bool rp2350_bios_handler(uint8_t intnum) {

if (intnum != 0x1C && intnum != 8) { // do not show timer
    print_line2("BIOS", 0, 8);
    if (intnum != 0x16 && intnum != 9 && intnum != 0x1A) { // do not show keyboard, we are on int 13h debug stage now
        debug_write("rp2350_bios_handler(%x)\n", intnum);
    }
}
    bool normal_iret_flow = handlers[intnum]();
    uint16_t flags_on_stack = getmem16(CPU_SS, CPU_SP + 4);
    if (normal_iret_flow) {
        // patch FLAGS to be returnable by IRET
        flags_on_stack = (flags_on_stack & ~0x0041) // reset ZF, CF
                       | (x86_flags.value & 0x0041); // set them back from CPU
        putmem16(CPU_SS, CPU_SP + 4, flags_on_stack);
    } else {
        // should be processed on exact handler side, no common logic for this
        // some handlers should turn IF = 1, some - not
        // some patch stack, other - not
    }
if (intnum != 0x1C && intnum != 8) { // do not show timer
    if (intnum != 0x16 && intnum != 9 && intnum != 0x1A) { // do not show keyboard, we are on int 13h debug stage now
    debug_write(
        "RET rp2350_bios_handler %02Xh AX:%04X BX:%04X CX:%04X DX:%04X ES:%04X FLAGS:%04X\n",
        intnum, CPU_AX, CPU_BX, CPU_CX, CPU_DX, CPU_ES, makeflagsword()
    );
    }
}
    return normal_iret_flow;
}

/* Silent no-op stub: used for BIOS hooks that have no default action.
 * Returns true so the main loop performs a clean IRET. */
static bool nop_handler() {
    return true;
}

//  Minimal fake printer: ready, selected, no error.
static bool bios_17h(void) {
    CPU_AH = 0x90;
    return true;
}

i286* i286_new(CPU_CB* *cb) {
    _cpu = (i286*)calloc(sizeof(i286), 1);
    *cb = &_cpu->cb;
    for(int i = 1; i < 256; ++i) {
        handlers[i] = no_handler;
    }
//    handlers[0x00] = bios_00h;
//    handlers[0x01] = nop_handler; // CPU-generated - SINGLE STEP
//    handlers[0x05] = bios_05h; // No print screen impl. there
    handlers[0x08] = bios_08h;
    handlers[0x09] = bios_09h;
    handlers[0x77] = bios_09h_phase2;
//    handlers[0x0F] = nop_handler; // no IRQ7 - PARALLEL PRINTER handler
    handlers[0x10] = bios_10h;
    handlers[0x11] = bios_11h;
    handlers[0x12] = bios_12h;
    handlers[0x13] = bios_13h;
    handlers[0x14] = bios_14h;
    handlers[0x15] = bios_15h;
    handlers[0x16] = bios_16h;
    handlers[0x17] = bios_17h;
    handlers[0x18] = bios_18h;
    handlers[0x19] = bios_19h;
    handlers[0x1A] = bios_1Ah;
//    handlers[0x1C] = nop_handler; /* INT 1Ch: user timer tick hook — no-op until replaced by a TSR */
//    handlers[0x21] = nop_handler; // No DOS functions support on BIOS level
//    handlers[0x29] = nop_handler; // No DOS functions support on BIOS level
//    handlers[0x2A] = nop_handler; // No NETWORK functions support on BIOS level
//    handlers[0x2F] = nop_handler; // No DOS functions support on BIOS level
    return _cpu;
}

void intcall86(uint8_t intnum) {
//    if (intnum == 0x2F && _cpu->int2f_handler && _cpu->int2f_handler(_cpu, _cpu->int2f_opaque)) {
//        return;
//    }
if (intnum != 0x1C && intnum != 8) { // do not show timer
    char buf[80];
    u16 new_cs = getmem16(0, (uint16_t) intnum * 4 + 2);
    u16 new_ip = getmem16(0, (uint16_t) intnum * 4);
    snprintf(buf, 79, "INT %02Xh DOS? %04X:%04X->%04X:%04X AX:%04X", intnum, CPU_CS, CPU_IP, new_cs, new_ip, CPU_AX);
    print_line(buf, 0);

    if (intnum != 0x16 && intnum != 9 && intnum != 0x1A) { // do not show keyboard, we are on int 13h debug stage now
    debug_write(
        "INT %02Xh %04X:%04X->%04X:%04X "
        "AX:%04X BX:%04X CX:%04X DX:%04X ES:%04X FLAGS:%04X\n",
        intnum, CPU_CS, CPU_IP, new_cs, new_ip,
        CPU_AX, CPU_BX, CPU_CX, CPU_DX, CPU_ES, makeflagsword()
    );
    }
}
    push(makeflagsword());
    push(CPU_CS);
    push(ip);
    CPU_CS = getmem16(0, (uint16_t) intnum * 4 + 2);
    CPU_IP = getmem16(0, (uint16_t) intnum * 4);
    ifl = 0;
    tf = 0;
}

static inline void flag_szp8(uint8_t value) {
    zf = value == 0;
    sf = value >> 7;
    pf = parity[value];
}

static inline void flag_szp16(uint16_t value) {
    zf = value == 0;
    sf = value >> 15;
    pf = parity[value & 255];
}

static inline void flag_szp32(uint32_t value) {
    zf = value == 0;
    sf = value >> 31;
    pf = parity[value & 255];
}

static inline void flag_log8(uint8_t value) {
    flag_szp8(value);
    x86_flags.value &= ~FLAG_CF_OF_MASK;
}

static inline void flag_log16(uint16_t value) {
    flag_szp16(value);
    x86_flags.value &= ~FLAG_CF_OF_MASK;
}

static inline void flag_adc8(uint8_t v1, uint8_t v2, uint8_t v3) {
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    uint32_t dst = (uint32_t) v1 + (uint32_t) v2 + (uint32_t) v3;
    flag_szp8((uint8_t) dst);
    of = ((dst ^ (uint32_t)v1) & (dst ^ (uint32_t)v2) & 0x80) != 0;
    cf = (dst & 0xFF00) != 0;
    af = (((uint32_t)v1 ^ (uint32_t)v2 ^ dst) & 0x10) != 0;
}

static inline void flag_adc16(uint16_t v1, uint16_t v2, uint16_t v3) {
    register uint32_t dst = (uint32_t) v1 + (uint32_t) v2 + (uint32_t) v3;
    flag_szp16((uint16_t) dst);
    of = (((dst ^ (uint32_t)v1) & (dst ^ (uint32_t)v2)) & 0x8000) != 0;
    cf = (dst & 0xFFFF0000) != 0;
    af = (((uint32_t)v1 ^ (uint32_t)v2 ^ dst) & 0x10) != 0;
}

static inline void flag_add8(uint8_t v1, uint8_t v2) {
    /* v1 = destination operand, v2 = source operand */
    register uint32_t dst = (uint32_t) v1 + (uint32_t) v2;
    flag_szp8((uint8_t) dst);
    cf = (dst & 0xFF00) != 0;
    of = ((dst ^ (uint32_t)v1) & (dst ^ (uint32_t)v2) & 0x80) != 0;
    af = (((uint32_t)v1 ^ (uint32_t)v2 ^ dst) & 0x10) != 0;
}

static inline void flag_add32(uint32_t v1, uint32_t v2, uint32_t res32) {
    /* v1 = destination operand, v2 = source operand */
    flag_szp32(res32);
    cf = (((uint64_t) v1 + (uint64_t) v2) & 0xF00000000) != 0;
    of = ((res32 ^ v1) & (res32 ^ v2) & 0x8000) != 0;
    af = ((v1 ^ v2 ^ res32) & 0x10) != 0;
}

static inline uint8_t sbb8(uint8_t v1, uint8_t v2, uint8_t v3) {
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    register uint32_t dst = (uint32_t)v1 - (uint32_t)v2 - (uint32_t)v3;
    flag_szp8((uint8_t) dst);
    cf = ((dst >> 8) & 1) != 0;
    of = ((dst ^ v1) & (v1 ^ v2) & 0x80) != 0;
    af = ((v1 ^ v2 ^ dst ^ v3) & 0x10) != 0;
    return (uint8_t)dst;
}

static inline uint16_t sbb16(uint16_t v1, uint16_t v2, uint8_t v3) {
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    register uint32_t dst = (uint32_t)v1 - (uint32_t)v2 - (uint32_t)v3;
    flag_szp16((uint16_t) dst);
    cf = ((dst >> 16) & 1) != 0;
    of = ((dst ^ (uint32_t)v1) & (v1 ^ (uint32_t)v2) & 0x8000) != 0;
    af = ((v1 ^ v2 ^ dst ^ v3) & 0x10) != 0;
    return (uint16_t)dst;
}

static inline uint32_t sbb32(uint32_t v1, uint32_t v2, uint8_t v3) {
    /* v1 = destination operand, v2 = source operand, v3 = carry flag */
    register uint64_t dst = (uint64_t)v1 - (uint64_t)v2 - (uint64_t)v3;
    flag_szp32((uint32_t) dst);
    cf = ((dst >> 32) & 1) != 0;
    of = ((dst ^ v1) & (v1 ^ v2) & 0x80000000) != 0;
    af = ((v1 ^ v2 ^ dst ^ v3) & 0x10) != 0;
    return (uint32_t)dst;
}

static inline void flag_sub8(uint8_t v1, uint8_t v2) {
    /* v1 = destination operand, v2 = source operand */
    uint32_t dst = (uint32_t) v1 - (uint32_t) v2;
    flag_szp8((uint8_t) dst);
    cf = (dst & 0xFF00) != 0;
    of = ((dst ^ (uint32_t)v1) & (v1 ^ v2) & 0x80) != 0;
    af = ((v1 ^ v2 ^ dst) & 0x10) != 0;
}

static inline void flag_sub16(uint16_t v1, uint16_t v2) {
    /* v1 = destination operand, v2 = source operand */
    register uint32_t dst = (uint32_t) v1 - (uint32_t) v2;
    flag_szp16((uint16_t) dst);
    cf = (dst & 0xFFFF0000) != 0;
    of = ((dst ^ (uint32_t)v1) & ((uint32_t)v1 ^ (uint32_t)v2) & 0x8000) != 0;
    af = (((uint32_t)v1 ^ (uint32_t)v2 ^ dst) & 0x10) != 0;
}

#define op_adc8() { res8 = oper1b + oper2b + cf; flag_adc8(oper1b, oper2b, cf); }
#define op_adc16() { res16 = oper1 + oper2 + cf; flag_adc16(oper1, oper2, cf); }
#define op_adc32() { res32 = oper1 + oper2 + cf; flag_adc32(oper1, oper2, cf); }
#define op_add8() { \
    register uint32_t dst = (uint32_t)oper1b + (uint32_t)oper2b; \
    res8 = dst; \
    flag_szp8(res8); \
    cf = (dst & 0xFF00) != 0; \
    of = ((dst ^ (uint32_t)oper1b) & (dst ^ (uint32_t)oper2b) & 0x80) != 0; \
    af = ((oper1b ^ oper2b ^ dst) & 0x10) != 0; \
}
#define op_add16() { \
    register uint32_t dst = (uint32_t)oper1 + (uint32_t)oper2; \
    res16 = dst; \
    flag_szp16(dst); \
    cf = (dst & 0xFFFF0000) != 0; \
    of = (((dst ^ (uint32_t)oper1) & (dst ^ (uint32_t)oper2) & 0x8000) != 0); \
    af = (((oper1 ^ oper2 ^ dst) & 0x10) != 0); \
}
#define op_add32() { res32 = oper1 + oper2; flag_add32(oper1, oper2, res32); }
#define op_and8() { res8 = oper1b & oper2b; flag_log8(res8); }
#define op_and16() { res16 = oper1 & oper2; flag_log16(res16); }
#define op_and32() { res32 = oper1 & oper2; flag_log32(res32); }
#define op_or8() { res8 = oper1b | oper2b; flag_log8(res8); }
#define op_or16() { res16 = oper1 | oper2; flag_log16(res16); }
#define op_or32() { res32 = oper1 | oper2; flag_log32(res32); }
#define op_xor8() { res8 = oper1b ^ oper2b; flag_log8(res8); }
#define op_xor16() { res16 = oper1 ^ oper2; flag_log16(res16); }
#define op_xor32() { res32 = oper1 ^ oper2; flag_log32(res32); }
#define op_sub8() { res8 = oper1b - oper2b; flag_sub8(oper1b, oper2b); }
#define op_sub16() { \
    register uint32_t dst = (uint32_t) oper1 - (uint32_t) oper2; \
    flag_szp16((uint16_t) dst); \
    cf = (dst & 0xFFFF0000) != 0; \
    of = ((dst ^ (uint32_t)oper1) & (oper1 ^ oper2) & 0x8000) != 0; \
    af = ((oper1 ^ oper2 ^ dst) & 0x10) != 0; \
    res16 = (uint16_t) dst; \
}
#define op_sub32() { res32 = oper1 - oper2; flag_sub32(oper1, oper2); }
#define op_sbb8() { res8 = sbb8(oper1b, oper2b, cf); }
#define op_sbb16() { res16 = sbb16(oper1, oper2, cf); }
#define op_sbb32() { res32 = sbb32(oper1, oper2, cf); }

static __not_in_flash() uint8_t op_grp2_8(uint8_t cnt, uint8_t oper1b) {
    uint16_t s = oper1b;
#ifdef CPU_LIMIT_SHIFT_COUNT
    cnt &= 0x1F;
#endif
    switch (reg) {
        case 0: /* ROL r/m8 */
            for (int shift = 1; shift <= cnt; shift++) {
                if (s & 0x80) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                s = s << 1;
                s = s | cf;
            }

            if (cnt == 1) {
                // of = cf ^ ( (s >> 7) & 1);
                if ((s & 0x80) && cf)
                    of = 1;
                else
                    of = 0;
            } else
                of = 0;
            break;

        case 1: /* ROR r/m8 */
            for (int shift = 1; shift <= cnt; shift++) {
                cf = s & 1;
                s = (s >> 1) | (cf << 7);
            }

            if (cnt == 1) {
                of = (s >> 7) ^ ((s >> 6) & 1);
            }
            break;

        case 2: /* RCL r/m8 */
            for (int shift = 1; shift <= cnt; shift++) {
                register bool oldcf = cf;
                if (s & 0x80) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                s = s << 1;
                s = s | oldcf;
            }

            if (cnt == 1) {
                of = cf ^ ((s >> 7) & 1);
            }
            break;

        case 3: /* RCR r/m8 */
            for (int shift = 1; shift <= cnt; shift++) {
                register uint8_t oldcf = cf;
                cf = s & 1;
                s = (s >> 1) | (oldcf << 7);
            }

            if (cnt == 1) {
                of = (s >> 7) ^ ((s >> 6) & 1);
            }
            break;

        case 4:
        case 6: /* SHL r/m8 */
            for (int shift = 1; shift <= cnt; shift++) {
                if (s & 0x80) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                s = (s << 1) & 0xFF;
            }

            if ((cnt == 1) && (cf == (s >> 7))) {
                of = 0;
            } else {
                of = 1;
            }

            flag_szp8((uint8_t) s);
            break;

        case 5: /* SHR r/m8 */
            if ((cnt == 1) && (s & 0x80)) {
                of = 1;
            } else {
                of = 0;
            }

            for (int a = 1; a <= cnt; a++) {
                cf = s & 1;
                s = s >> 1;
            }

            flag_szp8((uint8_t) s);
            break;

        case 7: /* SAR r/m8 */
            for (int a = 1; a <= cnt; a++) {
                unsigned int msb = s & 0x80;
                cf = s & 1;
                s = (s >> 1) | msb;
            }

            of = 0;
            flag_szp8((uint8_t) s);
            break;
    }

    return s & 0xFF;
}

static __not_in_flash() uint16_t op_grp2_16(uint8_t cnt) {
    register uint32_t s = oper1;
#ifdef CPU_LIMIT_SHIFT_COUNT
    cnt &= 0x1F;
#endif
    switch (reg) {
        case 0: /* ROL r/m16 */
            for (int shift = 1; shift <= cnt; shift++) {
                if (s & 0x8000) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                s = s << 1;
                s = s | cf;
            }

            if (cnt == 1) {
                of = cf ^ ((s >> 15) & 1);
            }
            break;

        case 1: /* ROR r/m16 */
            for (int shift = 1; shift <= cnt; shift++) {
                cf = s & 1;
                s = (s >> 1) | (cf << 15);
            }

            if (cnt == 1) {
                of = (s >> 15) ^ ((s >> 14) & 1);
            }
            break;

        case 2: /* RCL r/m16 */
            for (int shift = 1; shift <= cnt; shift++) {
                register bool oldcf = cf;
                if (s & 0x8000) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                s = s << 1;
                s = s | oldcf;
            }

            if (cnt == 1) {
                of = cf ^ ((s >> 15) & 1);
            }
            break;

        case 3: /* RCR r/m16 */
            for (int shift = 1; shift <= cnt; shift++) {
                register uint32_t oldcf = cf;
                cf = s & 1;
                s = (s >> 1) | (oldcf << 15);
            }

            if (cnt == 1) {
                of = (s >> 15) ^ ((s >> 14) & 1);
            }
            break;

        case 4:
        case 6: /* SHL r/m16 */
            for (unsigned int shift = 1; shift <= cnt; shift++) {
                if (s & 0x8000) {
                    cf = 1;
                } else {
                    cf = 0;
                }

                s = (s << 1) & 0xFFFF;
            }

            if ((cnt == 1) && (cf == (s >> 15))) {
                of = 0;
            } else {
                of = 1;
            }

            flag_szp16((uint16_t) s);
            break;

        case 5: /* SHR r/m16 */
            if ((cnt == 1) && (s & 0x8000)) {
                of = 1;
            } else {
                of = 0;
            }

            for (int shift = 1; shift <= cnt; shift++) {
                cf = s & 1;
                s = s >> 1;
            }

            flag_szp16((uint16_t) s);
            break;

        case 7: /* SAR r/m16 */
            for (int shift = 1, msb; shift <= cnt; shift++) {
                msb = s & 0x8000;
                cf = s & 1;
                s = (s >> 1) | msb;
            }

            of = 0;
            flag_szp16((uint16_t) s);
            break;
    }

    return (uint16_t) s & 0xFFFF;
}

static inline void op_div8(uint16_t valdiv, uint8_t divisor) {
    if (divisor == 0 || valdiv / divisor > 0xFF) {
        printf("[op_div8] %d / %d\n", valdiv, divisor);
        intcall86(0);
        return;
    }

    CPU_AH = (uint8_t) (valdiv % divisor);
    CPU_AL = (uint8_t) (valdiv / divisor);
}

static inline void op_idiv8(uint16_t valdiv, int8_t divisor) {
    if (divisor == 0) {
        printf("[op_idiv8] %d / 0\n", valdiv);
        intcall86(0);
        return;
    }
    int16_t dividend = (int16_t) valdiv;
    int16_t quotient  = dividend / divisor;
    int16_t remainder = dividend % divisor;
    if (quotient < -128 || quotient > 127) {
        printf("[op_idiv8] %d / %d overflow\n", dividend, divisor);
        intcall86(0);
        return;
    }
    CPU_AL = (uint8_t)quotient;
    CPU_AH = (uint8_t)remainder;
}

static inline void op_div16(uint32_t valdiv, uint16_t divisor) {
    if (divisor == 0 || valdiv / divisor > 0xFFFF) {
//        CPU_DX = 0;
//        CPU_AX = 0xFFFF;
//        printf("[op_div16] %d / %d\n", valdiv, divisor);
        intcall86(0);
        return;
    }

    CPU_DX = (uint16_t) (valdiv % divisor);
    CPU_AX = (uint16_t) (valdiv / divisor);
}

static inline void op_idiv16(uint32_t valdiv, uint16_t divisor) {
    int32_t dividend = (int32_t)valdiv;
    int16_t divisor_signed = (int16_t)divisor;
    if (divisor_signed == 0) {
        printf("[op_idiv16] %d / 0\n", dividend);
        intcall86(0);
        return;
    }
    int32_t quotient  = dividend / divisor_signed;
    int32_t remainder = dividend % divisor_signed;
    if (quotient < -32768 || quotient > 32767) {
        printf("[op_idiv16] %d / %d overflow\n", dividend, divisor_signed);
        intcall86(0);
        return;
    }
    CPU_AX = (uint16_t)quotient;
    CPU_DX = (uint16_t)remainder;
}


static __not_in_flash() void op_grp3_16() {
    switch (reg) {
        case 0:
        case 1: /* TEST */
            flag_log16(oper1 & getmem16(CPU_CS, CPU_IP));
            StepIP(2);
            break;

        case 2: /* NOT */
            res16 = ~oper1;
            break;

        case 3: /* NEG */
            res16 = (~oper1) + 1;
            flag_sub16(0, oper1);
            if (res16) {
                cf = 1;
            } else {
                cf = 0;
            }
            break;

        case 4: {
            /* MUL */
            register uint32_t temp1 = (uint32_t) oper1 * (uint32_t) CPU_AX;
            CPU_AX = temp1 & 0xFFFF;
            CPU_DX = temp1 >> 16;
            flag_szp16((uint16_t) temp1);
            if (CPU_DX) {
                x86_flags.value |= FLAG_CF_OF_MASK;
            } else {
                x86_flags.value &= ~FLAG_CF_OF_MASK;
            }
#ifdef CPU_CLEAR_ZF_ON_MUL
            zf = 0;
#endif
            break;
        }
        case 5: {
            /* IMUL */
            register int32_t temp1 = (int32_t)(int16_t)CPU_AX * (int32_t)(int16_t)oper1;
			int16_t truncated = (int16_t)temp1;
            CPU_AX = truncated; /* into register ax */
            CPU_DX = (uint16_t)(temp1 >> 16); /* into register dx */
            if (temp1 != (int32_t)truncated) {
                x86_flags.value |= FLAG_CF_OF_MASK;
            } else {
                x86_flags.value &= ~FLAG_CF_OF_MASK;
            }
#ifdef CPU_CLEAR_ZF_ON_MUL
            zf = 0;
#endif
            break;
        }
        case 6: /* DIV */
            op_div16(((uint32_t) CPU_DX << 16) + CPU_AX, oper1);
            break;

        case 7: /* DIV */
            op_idiv16(((uint32_t) CPU_DX << 16) + CPU_AX, oper1);
            break;
    }
}

static __not_in_flash() void op_grp5() {
    switch (reg) {
        case 0: /* INC Ev */
            oper2 = 1;
            tempcf = cf;
            op_add16();
            cf = tempcf;
            writerm16(rm, res16);
            break;

        case 1: /* DEC Ev */
            oper2 = 1;
            tempcf = cf;
            op_sub16();
            cf = tempcf;
            writerm16(rm, res16);
            break;

        case 2: /* CALL Ev */
            push(ip);
            ip = oper1;
            break;

        case 3: /* CALL Mp */
            push(CPU_CS);
            push(ip);
            getea(rm);
            ip = (uint16_t) read86(ea) + (uint16_t) read86(ea + 1) * 256;
            CPU_CS = (uint16_t) read86(ea + 2) + (uint16_t) read86(ea + 3) * 256;
            break;
        
        case 4: /* JMP Ev */
            ip = oper1;
            break;

        case 5: /* JMP Mp */
            getea(rm);
            ip = (uint16_t) read86(ea) + (uint16_t) read86(ea + 1) * 256;
            CPU_CS = (uint16_t) read86(ea + 2) + (uint16_t) read86(ea + 3) * 256;
            break;
        
        case 6: /* PUSH Ev */
            push(oper1);
            break;
    }
}

void i286_reset(i286* cpu) {
    CPU_CS = 0xF000;
    CPU_SS = 0x0000;
    CPU_SP = 0x0000;
//    init_umb();
    ip = 0xFFF0;
}

/* Reset CPU and jump to a flat 32-bit physical address.
   The address is split into a paragraph-aligned segment:offset pair
   so that  seg*16 + off == start_addr  (off stays in [0, 15]).       */
void i286_reset_pm(i286* cpu, uint32_t start_addr) {
    /* Zero all general-purpose registers */
    for (int i = 0; i < 8; i++) dwordregs[i] = 0;

    /* Segment registers */
    CPU_CS = (uint16_t)(start_addr >> 4);   /* paragraph base */
    CPU_SS = 0x0000;
    CPU_DS = 0x0000;
    CPU_ES = 0x0000;
    CPU_FS = 0x0000;
    CPU_GS = 0x0000;

    CPU_SP = 0x0000;

    /* IP = low nibble of start address so that CS*16+IP == start_addr */
    ip = (uint16_t)(start_addr & 0x000F);

    /* Clear flags */
    x86_flags.value = 0;
}

/* ------------------------------------------------------------------ *
 *  A20 gate                                                           *
 * ------------------------------------------------------------------ */
int cpu_get_a20(i286* cpu) {
    (void)cpu;
    return a20_enabled;
}

/* ------------------------------------------------------------------ *
 *  FLAGS accessors                                                    *
 * ------------------------------------------------------------------ */
uword cpu_getflags(i286* cpu) {
    (void)cpu;
    return (uword)makeflagsword();
}

void cpu_setflags(i286* cpu, uword set_mask, uword clear_mask) {
    (void)cpu;
    uint16_t f = makeflagsword();
    f |=  (uint16_t)set_mask;
    f &= ~(uint16_t)clear_mask;
    decodeflagsword(f);
}

/* ------------------------------------------------------------------ *
 *  Carry-flag accessor                                                *
 * ------------------------------------------------------------------ */
void cpu_set_cf(i286* cpu, int v) {
    (void)cpu;
    cf = v ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 *  AX / setexc helpers used by INT handlers                          *
 * ------------------------------------------------------------------ */
void cpu_setax(i286* cpu, int v) {
    (void)cpu;
    CPU_AX = (uint16_t)(v & 0xFFFF);
}

void cpu_setexc(i286* cpu, int v, int f) {
    /* v = exception/error code -> AX, f = CF value */
    (void)cpu;
    CPU_AX = (uint16_t)(v & 0xFFFF);
    cf = f ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 *  8-bit register getters / setters                                  *
 * ------------------------------------------------------------------ */
u8 cpu_get_al(i286 *cpu)           { (void)cpu; return CPU_AL; }
u8 cpu_get_ah(i286 *cpu)           { (void)cpu; return CPU_AH; }
u8 cpu_get_bl(i286 *cpu)           { (void)cpu; return CPU_BL; }
u8 cpu_get_bh(i286 *cpu)           { (void)cpu; return CPU_BH; }
u8 cpu_get_cl(i286 *cpu)           { (void)cpu; return CPU_CL; }
u8 cpu_get_ch(i286 *cpu)           { (void)cpu; return CPU_CH; }
u8 cpu_get_dl(i286 *cpu)           { (void)cpu; return CPU_DL; }
u8 cpu_get_dh(i286 *cpu)           { (void)cpu; return CPU_DH; }

void cpu_set_al(i286 *cpu, u8 val) { (void)cpu; CPU_AL = val; }

void cpu_portout8(u16 port, u8 val)  { portout(port, val); }
u8   cpu_portin8 (u16 port)          { return portin(port); }

/* ------------------------------------------------------------------ *
 *  16-bit register getters / setters                                 *
 * ------------------------------------------------------------------ */
u16 cpu_get_ax(i286 *cpu)           { (void)cpu; return CPU_AX; }
u16 cpu_get_bx(i286 *cpu)           { (void)cpu; return CPU_BX; }
u16 cpu_get_cx(i286 *cpu)           { (void)cpu; return CPU_CX; }
u16 cpu_get_dx(i286 *cpu)           { (void)cpu; return CPU_DX; }
u16 cpu_get_es(i286 *cpu)           { (void)cpu; return CPU_ES; }
u16 cpu_get_di(i286 *cpu)           { (void)cpu; return CPU_DI; }

void cpu_set_bx(i286 *cpu, u16 val) { (void)cpu; CPU_BX = val; }
void cpu_set_cx(i286 *cpu, u16 val) { (void)cpu; CPU_CX = val; }
void cpu_set_dx(i286 *cpu, u16 val) { (void)cpu; CPU_DX = val; }
void cpu_set_di(i286 *cpu, u16 val) { (void)cpu; CPU_DI = val; }

/* ------------------------------------------------------------------ *
 *  Flat-memory load/store helpers (seg:off -> linear address)        *
 * ------------------------------------------------------------------ */
static inline uint32_t _linear(u16 seg, u16 off) {
    return ((uint32_t)seg << 4) + off;
}

bool cpu_load8(i286* cpu, u16 seg, u16 off, u8* res) {
    (void)cpu;
    *res = read86(_linear(seg, off));
    return true;
}

bool cpu_load16(i286* cpu, u16 seg, u16 off, u16* res) {
    (void)cpu;
    *res = readw86(_linear(seg, off));
    return true;
}

bool cpu_load32(i286* cpu, u16 seg, u16 off, u32* res) {
    (void)cpu;
    *res = readdw86(_linear(seg, off));
    return true;
}

bool cpu_store8(i286* cpu, u16 seg, u16 off, u8 val) {
    (void)cpu;
    write86(_linear(seg, off), val);
    return true;
}

bool cpu_store16(i286* cpu, u16 seg, u16 off, u16 val) {
    (void)cpu;
    writew86(_linear(seg, off), val);
    return true;
}

bool cpu_store32(i286* cpu, u16 seg, u16 off, u32 val) {
    (void)cpu;
    writedw86(_linear(seg, off), val);
    return true;
}

void __not_in_flash() i286_step(i286* cpu, uint32_t execloops) {
    static uint16_t firstip;
    static bool was_TF;

    //counterticks = (uint64_t) ( (double) timerfreq / (double) 65536.0);
    //tickssource();
    for (uint32_t loopcount = 0; loopcount < execloops; loopcount++) {
        if (unlikely(ifl && irq_pending)) {
            int intno = cpu->cb.pic_read_irq(cpu->cb.pic);
            if (intno >= 0) {
                if (intno != 0x0F && intno != 0x77) {
                    // реальный IRQ — обрабатываем
                    intcall86((uint8_t)intno);
                }
                // spurious (0x0F / 0x77) — просто игнорируем, не сбрасываем флаг
            } else {
                irq_pending = 0;
            }
        }        
        if (fake_bios_area()) {
            if (rp2350_bios_handler(CPU_IP)) { // normal flow IRET is expected
                CPU_IP = 0x0006;
                CPU_CS = 0xFFF0; // reusable IRET (pc.c)
            }
            else {// internal using INT in JMP style (INT 19h...)
                continue; // to allow to recheck IRQ before next step
            }
        }
        reptype = 0;
        segoverride = 0;
        useseg = CPU_DS;
        uint8_t docontinue = 0;
        firstip = CPU_IP;
        register uint8_t opcode;

        while (!docontinue) {
            ///         CPU_CS &= 0xFFFF;
            ///         CPU_IP &= 0xFFFF;
            //            savecs = CPU_CS;
            //            saveip = ip;
            // W/A-hack: last byte of interrupts table (actually should not be ever used as CS:IP)
      //      if (unlikely(CPU_CS == XMS_FN_CS && ip == XMS_FN_IP)) {
                // hook for XMS
        //        opcode = xms_handler(); // always returns RET TODO: far/short ret?
          //  } else {
                opcode = getmem8(CPU_CS, CPU_IP);
            //}

            StepIP(1);

            switch (opcode) {
                /* segment prefix check */
                case 0x2E: /* segment CPU_CS */
                    useseg = CPU_CS;
                    segoverride = 1;
                    break;

                case 0x3E: /* segment CPU_DS */
                    useseg = CPU_DS;
                    segoverride = 1;
                    break;

                case 0x26: /* segment CPU_ES */
                    useseg = CPU_ES;
                    segoverride = 1;
                    break;

                case 0x36: /* segment CPU_SS */
                    useseg = CPU_SS;
                    segoverride = 1;
                    break;

                case 0x64: /* segment CPU_FS */
                    useseg = CPU_FS;
                    segoverride = 1;
                    break;

                case 0x65: /* segment CPU_GS */
                    useseg = CPU_GS;
                    segoverride = 1;
                    break;

                case 0xF0: /* LOCK (блокировка шины, для атомарных операций) */
                    /// TODO:
                    break;

                case 0xF2: /* REPNE/REPNZ */
                    reptype = 2;
                    break;

                /* repetition prefix check */
                case 0xF3: /* REP/REPE/REPZ */
                    reptype = 1;
                    break;

                default:
                    docontinue = 1;
                    break;
            }
        }

        register uint32_t res32;
        register uint8_t res8;
        register uint8_t oper1b;
        register uint8_t oper2b;
        switch (opcode) {
            case 0x0: /* 00 ADD Eb Gb */
                modregrm();
                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_add8();
                writerm8(rm, res8);
                break;

            case 0x1: /* 01 ADD Ev Gv */
                modregrm();
                if (operandSizeOverride) {
                    register uint32_t oper1 = readrm32(rm);
                    register uint32_t oper2 = getreg32(reg);
                    op_add32();
                    writerm32(rm, res32);
                } else {
                    register uint32_t oper1 = readrm16(rm);
                    register uint32_t oper2 = getreg16(reg);
                    op_add16();
                    writerm16(rm, res16);
                }
                break;

            case 0x2: /* 02 ADD Gb Eb */
                modregrm();
                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_add8();
                putreg8(reg, res8);
                break;

            case 0x3: {
                /* 03 ADD Gv Ev */
                modregrm();
                register uint32_t oper1 = getreg16(reg);
                register uint32_t oper2 = readrm16(rm);
                op_add16();
                putreg16(reg, res16);
                break;
            }
            case 0x4: /* 04 ADD CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                op_add8();
                CPU_AL = res8;
                break;

            case 0x5: {
                /* 05 ADD eAX Iv */
                register uint32_t oper1 = CPU_AX;
                register uint32_t oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                op_add16();
                CPU_AX = res16;
                break;
            }
            case 0x6: /* 06 PUSH CPU_ES */
                push(CPU_ES);
                break;

            case 0x7: /* 07 POP CPU_ES */
                CPU_ES = pop();
                break;

            case 0x8: /* 08 OR Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_or8();
                writerm8(rm, res8
                );
                break;

            case 0x9: /* 09 OR Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_or16();
                writerm16(rm, res16
                );
                break;

            case 0xA: /* 0A OR Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_or8();
                putreg8(reg, res8
                );
                break;

            case 0xB: /* 0B OR Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_or16();
                /*                if ((oper1 == 0xF802) && (oper2 == 0xF802)) {
                                    sf = 0;    *//* cheap hack to make Wolf 3D think we're a 286 so it plays */ /*
                }*/

                putreg16(reg, res16);
                break;

            case 0xC: /* 0C OR CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                op_or8();
                CPU_AL = res8;
                break;

            case 0xD: /* 0D OR eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                op_or16();
                CPU_AX = res16;
                break;

            case 0xE: /* 0E PUSH CPU_CS */
                push(CPU_CS);
                break;

#ifdef CPU_8086 //only the 8086/8088 does this.
            case 0xF: //0F POP CS
                CPU_CS = pop();
                break;
#else
                /*
                            case 0xF: // 286 protected mode
                            break;
                */
#endif

            case 0x10: /* 10 ADC Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_adc8();
                writerm8(rm, res8);
                break;

            case 0x11: /* 11 ADC Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_adc16();
                writerm16(rm, res16);
                break;

            case 0x12: /* 12 ADC Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_adc8();
                putreg8(reg, res8);
                break;

            case 0x13: /* 13 ADC Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_adc16();
                putreg16(reg, res16
                );
                break;

            case 0x14: /* 14 ADC CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                op_adc8();
                CPU_AL = res8;
                break;

            case 0x15: /* 15 ADC eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                op_adc16();
                CPU_AX = res16;
                break;

            case 0x16: /* 16 PUSH CPU_SS */
                push(CPU_SS);
                break;

            case 0x17: /* 17 POP CPU_SS */
                CPU_SS = pop();
                break;

            case 0x18: /* 18 SBB Eb Gb */
                modregrm();
                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_sbb8();
                writerm8(rm, res8);
                break;

            case 0x19: /* 19 SBB Ev Gv */
                modregrm();
                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_sbb16();
                writerm16(rm, res16);
                break;

            case 0x1A: /* 1A SBB Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_sbb8();
                putreg8(reg, res8
                );
                break;

            case 0x1B: /* 1B SBB Gv Ev */
                modregrm();
                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_sbb16();
                putreg16(reg, res16);
                break;

            case 0x1C: /* 1C SBB CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                op_sbb8();
                CPU_AL = res8;
                break;

            case 0x1D: /* 1D SBB eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                op_sbb16();
                CPU_AX = res16;
                break;

            case 0x1E: /* 1E PUSH CPU_DS */
                push(CPU_DS);
                break;

            case 0x1F: /* 1F POP CPU_DS */
                CPU_DS = pop();
                break;

            case 0x20: /* 20 AND Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_and8();
                writerm8(rm, res8);
                break;

            case 0x21: /* 21 AND Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_and16();
                writerm16(rm, res16
                );
                break;

            case 0x22: /* 22 AND Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_and8();
                putreg8(reg, res8
                );
                break;

            case 0x23: /* 23 AND Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_and16();
                putreg16(reg, res16
                );
                break;

            case 0x24: /* 24 AND CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                op_and8();
                CPU_AL = res8;
                break;

            case 0x25: /* 25 AND eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                op_and16();
                CPU_AX = res16;
                break;

            case 0x27: /* 27 DAA */
            {
                uint8_t old_al;
                old_al = CPU_AL;
                if (((CPU_AL & 0x0F) > 9) || af) {
                    oper1 = (uint16_t) CPU_AL + 0x06;
                    CPU_AL = oper1 & 0xFF;
                    if (oper1 & 0xFF00)
                        cf = 1;
                    if ((oper1 & 0x000F) < (old_al & 0x0F))
                        af = 1;
                }
                if (((CPU_AL & 0xF0) > 0x90) || cf) {
                    oper1 = (uint16_t) CPU_AL + 0x60;
                    CPU_AL = oper1 & 0xFF;
                    if (oper1 & 0xFF00)
                        cf = 1;
                    else
                        cf = 0;
                }
                flag_szp8(CPU_AL);
                break;
            }

            case 0x28: /* 28 SUB Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_sub8();
                writerm8(rm, res8
                );
                break;

            case 0x29: {
                /* 29 SUB Ev Gv */
                modregrm();
                register uint32_t oper1 = readrm16(rm);
                register uint32_t oper2 = getreg16(reg);
                register uint32_t dst = oper1 - oper2;
                flag_szp16((uint16_t) dst);
                cf = (dst & 0xFFFF0000) != 0;
                of = ((dst ^ oper1) & (oper1 ^ oper2) & 0x8000) != 0;
                af = ((oper1 ^ oper2 ^ dst) & 0x10) != 0;
                writerm16(rm, (uint16_t) dst);
                break;
            }
            case 0x2A: /* 2A SUB Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_sub8();
                putreg8(reg, res8
                );
                break;

            case 0x2B: /* 2B SUB Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_sub16();
                putreg16(reg, res16
                );
                break;

            case 0x2C: /* 2C SUB CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                op_sub8();
                CPU_AL = res8;
                break;

            case 0x2D: /* 2D SUB eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                op_sub16();
                CPU_AX = res16;
                break;

            case 0x2F: /* 2F DAS */
            {
                uint8_t old_al;
                old_al = CPU_AL;
                if (((CPU_AL & 0x0F) > 9) || af) {
                    oper1 = (uint16_t) CPU_AL - 0x06;
                    CPU_AL = oper1 & 0xFF;
                    if (oper1 & 0xFF00)
                        cf = 1;
                    if ((oper1 & 0x000F) >= (old_al & 0x0F))
                        af = 1;
                }
                if (((CPU_AL & 0xF0) > 0x90) || cf) {
                    oper1 = (uint16_t) CPU_AL - 0x60;
                    CPU_AL = oper1 & 0xFF;
                    if (oper1 & 0xFF00)
                        cf = 1;
                    else
                        cf = 0;
                }
                flag_szp8(CPU_AL);
                break;
            }

            case 0x30: /* 30 XOR Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                op_xor8();
                writerm8(rm, res8
                );
                break;

            case 0x31: /* 31 XOR Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                op_xor16();
                writerm16(rm, res16
                );
                break;

            case 0x32: /* 32 XOR Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                op_xor8();
                putreg8(reg, res8
                );
                break;

            case 0x33: /* 33 XOR Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                op_xor16();
                putreg16(reg, res16
                );
                break;

            case 0x34: /* 34 XOR CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                op_xor8();
                CPU_AL = res8;
                break;

            case 0x35: /* 35 XOR eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                op_xor16();
                CPU_AX = res16;
                break;

            case 0x37: /* 37 AAA ASCII */
                if (((CPU_AL & 0xF) > 9) || (af == 1)) {
                    CPU_AX = CPU_AX + 0x106;
                    x86_flags.value |= FLAG_CF_AF_MASK;
                } else {
                    x86_flags.value &= ~FLAG_CF_AF_MASK;
                }

                CPU_AL = CPU_AL & 0xF;
                break;

            case 0x38: /* 38 CMP Eb Gb */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getreg8(reg);
                flag_sub8(oper1b, oper2b
                );
                break;

            case 0x39: /* 39 CMP Ev Gv */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getreg16(reg);
                flag_sub16(oper1, oper2
                );
                break;

            case 0x3A: /* 3A CMP Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                flag_sub8(oper1b, oper2b
                );
                break;

            case 0x3B: /* 3B CMP Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                flag_sub16(oper1, oper2
                );
                break;

            case 0x3C: /* 3C CMP CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                flag_sub8(oper1b, oper2b
                );
                break;

            case 0x3D: /* 3D CMP eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                flag_sub16(oper1, oper2
                );
                break;

            case 0x3F: /* 3F AAS ASCII */
                if (((CPU_AL & 0xF) > 9) || (af == 1)) {
                    CPU_AX = CPU_AX - 6;
                    CPU_AH = CPU_AH - 1;
                    x86_flags.value |= FLAG_CF_AF_MASK;
                } else {
                    x86_flags.value &= ~FLAG_CF_AF_MASK;
                }

                CPU_AL = CPU_AL & 0xF;
                break;

            case 0x40: {
                /* 40 INC eAX */
                register uint32_t oper1 = CPU_AX;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_AX = (uint16_t) dst;
                break;
            }
            case 0x41: {
                /* 41 INC eCX */
                register uint32_t oper1 = CPU_CX;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_CX = (uint16_t) dst;
                break;
            }
            case 0x42: {
                /* 42 INC eDX */
                register uint32_t oper1 = CPU_DX;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_DX = (uint16_t) dst;
                break;
            }
            case 0x43: {
                /* 43 INC eBX */
                register uint32_t oper1 = CPU_BX;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_BX = (uint16_t) dst;
                break;
            }
            case 0x44: {
                /* 44 INC eSP */
                register uint32_t oper1 = CPU_SP;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_SP = (uint16_t) dst;
                break;
            }
            case 0x45: {
                /* 45 INC eBP */
                register uint32_t oper1 = CPU_BP;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_BP = (uint16_t) dst;
                break;
            }
            case 0x46: {
                /* 46 INC eSI */
                register uint32_t oper1 = CPU_SI;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_SI = (uint16_t) dst;
                break;
            }
            case 0x47: {
                /* 47 INC eDI */
                register uint32_t oper1 = CPU_DI;
                register uint32_t dst = oper1 + 1;
                flag_szp16(dst);
                of = (((dst ^ oper1) & (dst ^ 1) & 0x8000) != 0);
                af = (((oper1 ^ 1 ^ dst) & 0x10) != 0);
                CPU_DI = (uint16_t) dst;
                break;
            }
            case 0x48: /* 48 DEC eAX */
                oldcf = cf;
                oper1 = CPU_AX;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_AX = res16;
                break;

            case 0x49: /* 49 DEC eCX */
                oldcf = cf;
                oper1 = CPU_CX;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_CX = res16;
                break;

            case 0x4A: /* 4A DEC eDX */
                oldcf = cf;
                oper1 = CPU_DX;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_DX = res16;
                break;

            case 0x4B: /* 4B DEC eBX */
                oldcf = cf;
                oper1 = CPU_BX;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_BX = res16;
                break;

            case 0x4C: /* 4C DEC eSP */
                oldcf = cf;
                oper1 = CPU_SP;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_SP = res16;
                break;

            case 0x4D: /* 4D DEC eBP */
                oldcf = cf;
                oper1 = CPU_BP;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_BP = res16;
                break;

            case 0x4E: /* 4E DEC eSI */
                oldcf = cf;
                oper1 = CPU_SI;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_SI = res16;
                break;

            case 0x4F: /* 4F DEC eDI */
                oldcf = cf;
                oper1 = CPU_DI;
                oper2 = 1;
                op_sub16();
                cf = oldcf;
                CPU_DI = res16;
                break;

            case 0x50: /* 50 PUSH eAX */
                push(CPU_AX);
                break;

            case 0x51: /* 51 PUSH eCX */
                push(CPU_CX);
                break;

            case 0x52: /* 52 PUSH eDX */
                push(CPU_DX);
                break;

            case 0x53: /* 53 PUSH eBX */
                push(CPU_BX);
                break;

            case 0x54: /* 54 PUSH eSP */
#ifdef CPU_286_STYLE_PUSH_SP
                push(CPU_SP);
#else
                push(CPU_SP - 2);
#endif
                break;

            case 0x55: /* 55 PUSH eBP */
                push(CPU_BP);
                break;

            case 0x56: /* 56 PUSH eSI */
                push(CPU_SI);
                break;

            case 0x57: /* 57 PUSH eDI */
                push(CPU_DI);
                break;

            case 0x58: /* 58 POP eAX */
                CPU_AX = pop();
                break;

            case 0x59: /* 59 POP eCX */
                CPU_CX = pop();
                break;

            case 0x5A: /* 5A POP eDX */
                CPU_DX = pop();
                break;

            case 0x5B: /* 5B POP eBX */
                CPU_BX = pop();
                break;

            case 0x5C: /* 5C POP eSP */
                CPU_SP = pop();
                break;

            case 0x5D: /* 5D POP eBP */
                CPU_BP = pop();
                break;

            case 0x5E: /* 5E POP eSI */
                CPU_SI = pop();
                break;

            case 0x5F: /* 5F POP eDI */
                CPU_DI = pop();
                break;

#ifndef CPU_8086
            case 0x60: /* 60 PUSHA (80186+) */
                oldsp = CPU_SP;
                push(CPU_AX);
                push(CPU_CX);
                push(CPU_DX);
                push(CPU_BX);
                push(oldsp);
                push(CPU_BP);
                push(CPU_SI);
                push(CPU_DI);
                break;

            case 0x61: /* 61 POPA (80186+) */
                CPU_DI = pop();
                CPU_SI = pop();
                CPU_BP = pop();
                CPU_SP += 2;
                CPU_BX = pop();
                CPU_DX = pop();
                CPU_CX = pop();
                CPU_AX = pop();
                break;

            case 0x62: /* 62 BOUND Gv, Ev (80186+) */
                modregrm();

                getea(rm);
                if (
                    signext32(getreg16(reg)
                    ) <
                    signext32(getmem16(ea >> 4, ea & 15)
                    )) {
                    intcall86(5); //bounds check exception
                } else {
                    ea += 2;
                    if (
                        signext32(getreg16(reg)
                        ) >
                        signext32(getmem16(ea >> 4, ea & 15)
                        )) {
                        intcall86(5); //bounds check exception
                    }
                }
                break;
#if CPU_386_EXTENDED_OPS
            case 0x66: /* Operand-Size Override (изменяет размер операндов: 16 ↔ 32 бит) */
                operandSizeOverride = true;
                break;
            case 0x67: /* Address-Size Override (изменяет размер адреса: 16 ↔ 32 бит) */
                addressSizeOverride = true;
                break;
#endif
            case 0x68: /* 68 PUSH Iv (80186+) */
                push(getmem16(CPU_CS, CPU_IP)
                );
                StepIP(2);
                break;

            case 0x69: {
                /* 69 IMUL Gv Ev Iv (80186+) */
                modregrm();
                register int32_t temp1 = (int32_t)(int16_t)readrm16(rm);
                register int32_t temp2 = (int32_t)(int16_t)getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                temp1 *= temp2;
                putreg16(reg, (int16_t)temp1);
                if (temp1 != (int32_t)(int16_t)temp1) {
                    x86_flags.value |= FLAG_CF_OF_MASK;
                } else {
                    x86_flags.value &= ~FLAG_CF_OF_MASK;
                }
                break;
            }
            case 0x6A: /* 6A PUSH Ib (80186+) */
                push((uint16_t) signext(getmem8(CPU_CS, CPU_IP)));
                StepIP(1);
                break;

            case 0x6B: {
                /* 6B IMUL Gv Eb Ib (80186+) */
                modregrm();
                register int32_t temp1 = (int32_t)(int16_t)readrm16(rm);
                register int32_t temp2 = (int32_t)(int16_t)signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                temp1 *= temp2;
				putreg16(reg, (int16_t)temp1);
                if (temp1 != (int32_t)(int16_t)temp1) {
                    x86_flags.value |= FLAG_CF_OF_MASK;
                } else {
                    x86_flags.value &= ~FLAG_CF_OF_MASK;
                }
                break;
            }
            case 0x6C: /* 6E INSB */
                if (reptype && (CPU_CX == 0)) {
                    break;
                }

                putmem8(CPU_ES, CPU_DI, portin(CPU_DX));
                if (df) {
                    CPU_SI = CPU_SI - 1;
                    CPU_DI = CPU_DI - 1;
                } else {
                    CPU_SI = CPU_SI + 1;
                    CPU_DI = CPU_DI + 1;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0x6D: /* 6F INSW */
                if (reptype && (CPU_CX == 0)) {
                    break;
                }

                putmem16(CPU_ES, CPU_DI, portin16(CPU_DX));
                if (df) {
                    CPU_SI = CPU_SI - 2;
                    CPU_DI = CPU_DI - 2;
                } else {
                    CPU_SI = CPU_SI + 2;
                    CPU_DI = CPU_DI + 2;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0x6E: /* 6E OUTSB */
                if (reptype && (CPU_CX == 0)) {
                    break;
                }

                portout(CPU_DX, getmem8(useseg, CPU_SI));
                if (df) {
                    CPU_SI = CPU_SI - 1;
                    CPU_DI = CPU_DI - 1;
                } else {
                    CPU_SI = CPU_SI + 1;
                    CPU_DI = CPU_DI + 1;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0x6F: /* 6F OUTSW */
                if (reptype && (CPU_CX == 0)) {
                    break;
                }

                portout16(CPU_DX, getmem16(useseg, CPU_SI));
                if (df) {
                    CPU_SI = CPU_SI - 2;
                    CPU_DI = CPU_DI - 2;
                } else {
                    CPU_SI = CPU_SI + 2;
                    CPU_DI = CPU_DI + 2;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;
#endif

            case 0x70: /* 70 JO Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (of) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x71: /* 71 JNO Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!of) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x72: /* 72 JB Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (cf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x73: /* 73 JNB Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!cf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x74: /* 74 JZ Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (zf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x75: /* 75 JNZ Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!zf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x76: /* 76 JBE Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (cf || zf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x77: /* 77 JA Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!cf && !zf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x78: /* 78 JS Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (sf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x79: /* 79 JNS Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!sf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x7A: /* 7A JPE Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (pf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x7B: /* 7B JPO Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!pf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x7C: /* 7C JL Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (sf != of) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x7D: /* 7D JGE Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (sf == of) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x7E: /* 7E JLE Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if ((sf != of) || zf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x7F: /* 7F JG Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!
                    zf && (sf
                           == of)) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0x80:
            case 0x82: /* 80/82 GRP1 Eb Ib */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                switch (reg) {
                    case 0:
                        op_add8();
                        break;
                    case 1:
                        op_or8();
                        break;
                    case 2:
                        op_adc8();
                        break;
                    case 3:
                        op_sbb8();
                        break;
                    case 4:
                        op_and8();
                        break;
                    case 5:
                        op_sub8();
                        break;
                    case 6:
                        op_xor8();
                        break;
                    case 7:
                        flag_sub8(oper1b, oper2b
                        );
                        break;
                    default:
                        break; /* to avoid compiler warnings */
                }

                if (reg < 7) {
                    writerm8(rm, res8
                    );
                }
                break;

            case 0x81: /* 81 GRP1 Ev Iv */
            case 0x83: /* 83 GRP1 Ev Ib */
                modregrm();

                oper1 = readrm16(rm);
                if (opcode == 0x81) {
                    oper2 = getmem16(CPU_CS, CPU_IP);
                    StepIP(2);
                } else {
                    oper2 = signext(getmem8(CPU_CS, CPU_IP));
                    StepIP(1);
                }

                switch (reg) {
                    case 0:
                        op_add16();
                        break;
                    case 1:
                        op_or16();
                        break;
                    case 2:
                        op_adc16();
                        break;
                    case 3:
                        op_sbb16();
                        break;
                    case 4:
                        op_and16();
                        break;
                    case 5:
                        op_sub16();
                        break;
                    case 6:
                        op_xor16();
                        break;
                    case 7:
                        flag_sub16(oper1, oper2
                        );
                        break;
                    default:
                        break; /* to avoid compiler warnings */
                }

                if (reg < 7) {
                    writerm16(rm, res16
                    );
                }
                break;

            case 0x84: /* 84 TEST Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                oper2b = readrm8(rm);
                flag_log8(oper1b
                          & oper2b);
                break;

            case 0x85: /* 85 TEST Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                oper2 = readrm16(rm);
                flag_log16(oper1
                           & oper2);
                break;

            case 0x86: /* 86 XCHG Gb Eb */
                modregrm();

                oper1b = getreg8(reg);
                putreg8(reg, readrm8(rm)
                );
                writerm8(rm, oper1b
                );
                break;

            case 0x87: /* 87 XCHG Gv Ev */
                modregrm();

                oper1 = getreg16(reg);
                putreg16(reg, readrm16(rm)
                );
                writerm16(rm, oper1
                );
                break;

            case 0x88: /* 88 MOV Eb Gb */
                modregrm();

                writerm8(rm, getreg8(reg)
                );
                break;

            case 0x89: /* 89 MOV Ev Gv */
                modregrm();

                writerm16(rm, getreg16(reg)
                );
                break;

            case 0x8A: /* 8A MOV Gb Eb */
                modregrm();

                putreg8(reg, readrm8(rm)
                );
                break;

            case 0x8B: /* 8B MOV Gv Ev */
                modregrm();

                putreg16(reg, readrm16(rm)
                );
                break;

            case 0x8C: /* 8C MOV Ew Sw */
                modregrm();

                writerm16(rm, getsegreg(reg)
                );
                break;

            case 0x8D: /* 8D LEA Gv M */
                modregrm();

                getea(rm);
                putreg16(reg, ea
                         -
                         segbase(useseg)
                );
                break;

            case 0x8E: /* 8E MOV Sw Ew */
                modregrm();

                putsegreg(reg, readrm16(rm)
                );
                break;

            case 0x8F: /* 8F POP Ev */
                modregrm();

                writerm16(rm, pop()
                );
                break;

            case 0x90: /* 90 NOP */
                break;

            case 0x91: /* 91 XCHG eCX eAX */
                oper1 = CPU_CX;
                CPU_CX = CPU_AX;
                CPU_AX = oper1;
                break;

            case 0x92: /* 92 XCHG eDX eAX */
                oper1 = CPU_DX;
                CPU_DX = CPU_AX;
                CPU_AX = oper1;
                break;

            case 0x93: /* 93 XCHG eBX eAX */
                oper1 = CPU_BX;
                CPU_BX = CPU_AX;
                CPU_AX = oper1;
                break;

            case 0x94: /* 94 XCHG eSP eAX */
                oper1 = CPU_SP;
                CPU_SP = CPU_AX;
                CPU_AX = oper1;
                break;

            case 0x95: /* 95 XCHG eBP eAX */
                oper1 = CPU_BP;
                CPU_BP = CPU_AX;
                CPU_AX = oper1;
                break;

            case 0x96: /* 96 XCHG eSI eAX */
                oper1 = CPU_SI;
                CPU_SI = CPU_AX;
                CPU_AX = oper1;
                break;

            case 0x97: /* 97 XCHG eDI eAX */
                oper1 = CPU_DI;
                CPU_DI = CPU_AX;
                CPU_AX = oper1;
                break;

            case 0x98: /* 98 CBW */
                if ((CPU_AL & 0x80) == 0x80) {
                    CPU_AH = 0xFF;
                } else {
                    CPU_AH = 0;
                }
                break;

            case 0x99: /* 99 CWD */
                if ((CPU_AH & 0x80) == 0x80) {
                    CPU_DX = 0xFFFF;
                } else {
                    CPU_DX = 0;
                }
                break;

            case 0x9A: /* 9A CALL Ap */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                push(CPU_CS);
                push(CPU_IP);
                CPU_IP = oper1;
                CPU_CS = oper2;
                break;

            case 0x9B: /* 9B WAIT */
                /// TODO:
                break;

            case 0x9C: /* 9C PUSHF */
                push(makeflagsword());
                break;

            case 0x9D: /* 9D POPF */
#ifdef CPU_SET_HIGH_FLAGS
                decodeflagsword(pop() | 0xF800);
#else
                decodeflagsword(pop() & 0x0FFF);
#endif
                break;

            case 0x9E: /* 9E SAHF */
                decodeflagsword((makeflagsword() & 0xFF00) | CPU_AH);
                break;

            case 0x9F: /* 9F LAHF */
                CPU_AH = makeflagsword() & 0xFF;
                break;

            case 0xA0: /* A0 MOV CPU_AL Ob */
                CPU_AL = getmem8(useseg, getmem16(CPU_CS, CPU_IP));
                StepIP(2);
                break;

            case 0xA1: /* A1 MOV eAX Ov */
                oper1 = getmem16(useseg, getmem16(CPU_CS, CPU_IP));
                StepIP(2);
                CPU_AX = oper1;
                break;

            case 0xA2: /* A2 MOV Ob CPU_AL */
                putmem8(useseg, getmem16(CPU_CS, CPU_IP), CPU_AL);
                StepIP(2);
                break;

            case 0xA3: /* A3 MOV Ov eAX */
                putmem16(useseg, getmem16(CPU_CS, CPU_IP), CPU_AX);
                StepIP(2);
                break;

            case 0xA4: /* A4 MOVSB */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                putmem8(CPU_ES, CPU_DI, getmem8(useseg, CPU_SI)
                );
                if (df) {
                    CPU_SI = CPU_SI - 1;
                    CPU_DI = CPU_DI - 1;
                } else {
                    CPU_SI = CPU_SI + 1;
                    CPU_DI = CPU_DI + 1;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xA5: /* A5 MOVSW */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                putmem16(CPU_ES, CPU_DI, getmem16(useseg, CPU_SI)
                );
                if (df) {
                    CPU_SI = CPU_SI - 2;
                    CPU_DI = CPU_DI - 2;
                } else {
                    CPU_SI = CPU_SI + 2;
                    CPU_DI = CPU_DI + 2;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xA6: /* A6 CMPSB */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                oper1b = getmem8(useseg, CPU_SI);
                oper2b = getmem8(CPU_ES, CPU_DI);
                if (df) {
                    CPU_SI = CPU_SI - 1;
                    CPU_DI = CPU_DI - 1;
                } else {
                    CPU_SI = CPU_SI + 1;
                    CPU_DI = CPU_DI + 1;
                }

                flag_sub8(oper1b, oper2b
                );
                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                if ((reptype == 1) && !zf) {
                    break;
                } else if ((reptype == 2) && (zf == 1)) {
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xA7: /* A7 CMPSW */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                oper1 = getmem16(useseg, CPU_SI);
                oper2 = getmem16(CPU_ES, CPU_DI);
                if (df) {
                    CPU_SI = CPU_SI - 2;
                    CPU_DI = CPU_DI - 2;
                } else {
                    CPU_SI = CPU_SI + 2;
                    CPU_DI = CPU_DI + 2;
                }

                flag_sub16(oper1, oper2
                );
                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                if ((reptype == 1) && !zf) {
                    break;
                }

                if ((reptype == 2) && (zf == 1)) {
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xA8: /* A8 TEST CPU_AL Ib */
                oper1b = CPU_AL;
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                flag_log8(oper1b
                          & oper2b);
                break;

            case 0xA9: /* A9 TEST eAX Iv */
                oper1 = CPU_AX;
                oper2 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                flag_log16(oper1
                           & oper2);
                break;

            case 0xAA: /* AA STOSB */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                putmem8(CPU_ES, CPU_DI, CPU_AL
                );
                if (df) {
                    CPU_DI = CPU_DI - 1;
                } else {
                    CPU_DI = CPU_DI + 1;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xAB: /* AB STOSW */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                putmem16(CPU_ES, CPU_DI, CPU_AX
                );
                if (df) {
                    CPU_DI = CPU_DI - 2;
                } else {
                    CPU_DI = CPU_DI + 2;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xAC: /* AC LODSB */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                CPU_AL = getmem8(useseg, CPU_SI);
                if (df) {
                    CPU_SI = CPU_SI - 1;
                } else {
                    CPU_SI = CPU_SI + 1;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xAD: /* AD LODSW */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                oper1 = getmem16(useseg, CPU_SI);
                CPU_AX = oper1;
                if (df) {
                    CPU_SI = CPU_SI - 2;
                } else {
                    CPU_SI = CPU_SI + 2;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xAE: /* AE SCASB */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                oper1b = CPU_AL;
                oper2b = getmem8(CPU_ES, CPU_DI);
                flag_sub8(oper1b, oper2b
                );
                if (df) {
                    CPU_DI = CPU_DI - 1;
                } else {
                    CPU_DI = CPU_DI + 1;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                if ((reptype == 1) && !zf) {
                    break;
                } else if ((reptype == 2) && (zf == 1)) {
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xAF: /* AF SCASW */
                if (
                    reptype && (CPU_CX
                                == 0)) {
                    break;
                }

                oper1 = CPU_AX;
                oper2 = getmem16(CPU_ES, CPU_DI);
                flag_sub16(oper1, oper2
                );
                if (df) {
                    CPU_DI = CPU_DI - 2;
                } else {
                    CPU_DI = CPU_DI + 2;
                }

                if (reptype) {
                    CPU_CX = CPU_CX - 1;
                }

                if ((reptype == 1) && !zf) {
                    break;
                } else if ((reptype == 2) && (zf == 1)) {
                    //did i fix a typo bug? this used to be & instead of &&
                    break;
                }

                loopcount++;
                if (!reptype) {
                    break;
                }

                CPU_IP = firstip;
                break;

            case 0xB0: /* B0 MOV CPU_AL Ib */
                CPU_AL = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB1: /* B1 MOV CPU_CL Ib */
                CPU_CL = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB2: /* B2 MOV CPU_DL Ib */
                CPU_DL = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB3: /* B3 MOV CPU_BL Ib */
                CPU_BL = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB4: /* B4 MOV CPU_AH Ib */
                CPU_AH = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB5: /* B5 MOV CPU_CH Ib */
                CPU_CH = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB6: /* B6 MOV CPU_DH Ib */
                CPU_DH = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB7: /* B7 MOV CPU_BH Ib */
                CPU_BH = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                break;

            case 0xB8: /* B8 MOV eAX Iv */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                CPU_AX = oper1;
                break;

            case 0xB9: /* B9 MOV eCX Iv */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                CPU_CX = oper1;
                break;

            case 0xBA: /* BA MOV eDX Iv */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                CPU_DX = oper1;
                break;

            case 0xBB: /* BB MOV eBX Iv */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                CPU_BX = oper1;
                break;

            case 0xBC: /* BC MOV eSP Iv */
                CPU_SP = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                break;

            case 0xBD: /* BD MOV eBP Iv */
                CPU_BP = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                break;

            case 0xBE: /* BE MOV eSI Iv */
                CPU_SI = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                break;

            case 0xBF: /* BF MOV eDI Iv */
                CPU_DI = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                break;

            case 0xC0: /* C0 GRP2 byte imm8 (80186+) */
                modregrm();

                oper1b = readrm8(rm);
                oper2b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                writerm8(rm, op_grp2_8(oper2b, oper1b));
                break;

            case 0xC1: /* C1 GRP2 word imm8 (80186+) */
                modregrm();

                oper1 = readrm16(rm);
                oper2 = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                writerm16(rm, op_grp2_16((uint8_t) oper2)
                );
                break;

            case 0xC2: /* C2 RET Iw */
                oper1 = getmem16(CPU_CS, CPU_IP);
                CPU_IP = pop();
                CPU_SP = CPU_SP + oper1;
                break;

            case 0xC3: /* C3 RET */
                CPU_IP = pop();
                break;

            case 0xC4: /* C4 LES Gv Mp */
                modregrm();

                getea(rm);
                putreg16(reg, read86(ea) + read86(ea + 1) * 256);
                CPU_ES = read86(ea + 2) + read86(ea + 3) * 256;
                break;

            case 0xC5: /* C5 LDS Gv Mp */
                modregrm();

                getea(rm);
                putreg16(reg, read86(ea) + read86(ea + 1) * 256);
                CPU_DS = read86(ea + 2) + read86(ea + 3) * 256;
                break;

            case 0xC6: /* C6 MOV Eb Ib */
                modregrm();

                writerm8(rm, getmem8(CPU_CS, CPU_IP)
                );
                StepIP(1);
                break;

            case 0xC7: /* C7 MOV Ev Iv */
                modregrm();

                writerm16(rm, getmem16(CPU_CS, CPU_IP)
                );
                StepIP(2);
                break;

            case 0xC8: /* C8 ENTER (80186+) */
                stacksize = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                nestlev = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                push(CPU_BP);
                frametemp = CPU_SP;
                if (nestlev) {
                    for (
                        temp16 = 1;
                        temp16 < nestlev;
                        ++temp16) {
                        CPU_BP = CPU_BP - 2;
                        push(CPU_BP);
                    }

                    push(frametemp); //CPU_SP);
                }

                CPU_BP = frametemp;
                CPU_SP = CPU_BP - stacksize;

                break;

            case 0xC9: /* C9 LEAVE (80186+) */
                CPU_SP = CPU_BP;
                CPU_BP = pop();
                break;

            case 0xCA: /* CA RETF Iw */
                oper1 = getmem16(CPU_CS, CPU_IP);
                CPU_IP = pop();
                CPU_CS = pop();
                CPU_SP = CPU_SP + oper1;
                break;

            case 0xCB: /* CB RETF */
                CPU_IP = pop();
                CPU_CS = pop();
                break;

            case 0xCC: /* CC INT 3 */
                intcall86(3);
                break;

            case 0xCD: /* CD INT Ib */
                oper1b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                intcall86(oper1b);
                break;

            case 0xCE: /* CE INTO */
                if (of) {
                    intcall86(4);
                }
                break;

            case 0xCF: /* CF IRET */
                CPU_IP = pop();
                CPU_CS = pop();
#ifdef CPU_SET_HIGH_FLAGS
                decodeflagsword(pop() | 0xF000);
#else
                decodeflagsword(pop() & 0x0FFF);
#endif


                /*
                 * if (net.enabled) net.canrecv = 1;
                 */
                break;

            case 0xD0: /* D0 GRP2 Eb 1 */
                modregrm();

                oper1b = readrm8(rm);
                writerm8(rm, op_grp2_8(1, oper1b));
                break;

            case 0xD1: /* D1 GRP2 Ev 1 */
                modregrm();

                oper1 = readrm16(rm);
                writerm16(rm, op_grp2_16(1));
                break;

            case 0xD2: /* D2 GRP2 Eb CPU_CL */
                modregrm();

                oper1b = readrm8(rm);
                writerm8(rm, op_grp2_8(CPU_CL, oper1b));
                break;

            case 0xD3: /* D3 GRP2 Ev CPU_CL */
                modregrm();

                oper1 = readrm16(rm);
                writerm16(rm, op_grp2_16(CPU_CL)
                );
                break;

            case 0xD4: /* D4 AAM I0 */
                oper1 = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                if (!oper1) {
                    intcall86(0);
                    break;
                } /* division by zero */

                CPU_AH = (CPU_AL / oper1) & 255;
                CPU_AL = (CPU_AL % oper1) & 255;
                flag_szp16(CPU_AX);
                break;

            case 0xD5: /* D5 AAD I0 */
                oper1 = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                CPU_AL = (CPU_AH * oper1 + CPU_AL) & 255;
                CPU_AH = 0;
                flag_szp16(CPU_AH
                           * oper1 + CPU_AL);
                sf = 0;
                break;

            case 0xD6: /* D6 XLAT on V20/V30, SALC on 8086/8088 */
#ifndef CPU_NO_SALC
                CPU_AL = CPU_FL_CF ? 0xFF : 0x00;
                break;
#endif

            case 0xD7: /* D7 XLAT */
                CPU_AL = read86(useseg * 16 + (CPU_BX) + CPU_AL);
                break;

            case 0xD8:
            case 0xD9:
            case 0xDA:
            case 0xDB:
            case 0xDC:
            case 0xDE:
            case 0xDD:
            case 0xDF: /* escape to x87 FPU */
                if (fpu_on) OpFpu(opcode);
                else {
                    intcall86(7); /* #NM - device not available, no FPU */
                }
                break;

            case 0xE0: /* E0 LOOPNZ Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                CPU_CX = CPU_CX - 1;
                if ((CPU_CX) && !zf) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0xE1: /* E1 LOOPZ Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                CPU_CX = CPU_CX - 1;
                if (CPU_CX && (zf == 1)) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0xE2: /* E2 LOOP Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                CPU_CX = CPU_CX - 1;
                if (CPU_CX) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0xE3: /* E3 JCXZ Jb */
                temp16 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                if (!CPU_CX) {
                    CPU_IP = CPU_IP + temp16;
                }
                break;

            case 0xE4: /* E4 IN CPU_AL Ib */
                oper1b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                CPU_AL = (uint8_t) portin(oper1b);
                break;

            case 0xE5: /* E5 IN eAX Ib */
                oper1b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                CPU_AX = portin16(oper1b);
                break;

            case 0xE6: /* E6 OUT Ib CPU_AL */
                oper1b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                portout(oper1b, CPU_AL
                );
                break;

            case 0xE7: /* E7 OUT Ib eAX */
                oper1b = getmem8(CPU_CS, CPU_IP);
                StepIP(1);
                portout16(oper1b, CPU_AX
                );
                break;

            case 0xE8: /* E8 CALL Jv */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                push(CPU_IP);
                CPU_IP = CPU_IP + oper1;
                break;

            case 0xE9: /* E9 JMP Jv */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                CPU_IP = CPU_IP + oper1;
                break;

            case 0xEA: /* EA JMP Ap */
                oper1 = getmem16(CPU_CS, CPU_IP);
                StepIP(2);
                oper2 = getmem16(CPU_CS, CPU_IP);
                CPU_IP = oper1;
                CPU_CS = oper2;
                break;

            case 0xEB: /* EB JMP Jb */
                oper1 = signext(getmem8(CPU_CS, CPU_IP));
                StepIP(1);
                CPU_IP = CPU_IP + oper1;
                break;

            case 0xEC: /* EC IN CPU_AL regdx */
                oper1 = CPU_DX;
                CPU_AL = (uint8_t) portin(oper1);
                break;

            case 0xED: /* ED IN eAX regdx */
                oper1 = CPU_DX;
                CPU_AX = portin16(oper1);
                break;

            case 0xEE: /* EE OUT regdx CPU_AL */
                oper1 = CPU_DX;
                portout(oper1, CPU_AL
                );
                break;

            case 0xEF: /* EF OUT regdx eAX */
                oper1 = CPU_DX;
                portout16(oper1, CPU_AX);
                break;

            case 0xF0: /* F0 LOCK */
                break;

            case 0xF4: /* F4 HLT */
                /// TODO:
                //hltstate = 1;
                break;

            case 0xF5: /* F5 CMC */
                if (!cf) {
                    cf = 1;
                } else {
                    cf = 0;
                }
                break;

            case 0xF6: /* F6 GRP3a Eb */
                modregrm();
                oper1b = readrm8(rm);
                oper1 = signext(oper1b);
                switch (reg) {
                    case 0:
                    case 1: /* TEST */
                        flag_log8(oper1b & getmem8(CPU_CS, CPU_IP));
                        StepIP(1);
                        break;

                    case 2: /* NOT */
                        res8 = ~oper1b;
                        break;

                    case 3: /* NEG */
                        res8 = (~oper1b) + 1;
                        flag_sub8(0, oper1b);
                        if (res8 == 0) {
                            cf = 0;
                        } else {
                            cf = 1;
                        }
                        break;

                    case 4: {
                        /* MUL */
                        register uint32_t temp1 = (uint32_t) oper1b * (uint32_t) CPU_AL;
                        CPU_AX = temp1 & 0xFFFF;
                        flag_szp8((uint8_t) temp1);
                        if (CPU_AH) {
                            x86_flags.value |= FLAG_CF_OF_MASK;
                        } else {
                            x86_flags.value &= ~FLAG_CF_OF_MASK;
                        }
#ifdef CPU_CLEAR_ZF_ON_MUL
                        zf = 0;
#endif
                        break;
                    }
                    case 5: {
                        /* IMUL */
                        oper1 = signext(oper1b);
                        register int32_t temp1 = (int32_t)(int8_t)signext(CPU_AL);
                        register int32_t temp2 = (int32_t)(int8_t)oper1;
						temp1 *= temp2;
						int16_t result = (int16_t)temp1;
						int8_t truncated = (int8_t)result;
						if (result != (int16_t)truncated) {
							x86_flags.value |= FLAG_CF_OF_MASK; // CF=OF=1
						} else {
							x86_flags.value &= ~FLAG_CF_OF_MASK; // CF=OF=0
						}
						CPU_AL = truncated;
						CPU_AH = (uint8_t)(result >> 8);
#ifdef CPU_CLEAR_ZF_ON_MUL
                        zf = 0;
#endif
                        break;
                    }
                    case 6: /* DIV */
                        op_div8(CPU_AX, oper1b);
                        break;

                    case 7: /* IDIV */
                        op_idiv8(CPU_AX, oper1b);
                        break;
                }

                if ((reg > 1) && (reg < 4)) {
                    writerm8(rm, res8
                    );
                }
                break;

            case 0xF7: /* F7 GRP3b Ev */
                modregrm();

                oper1 = readrm16(rm);
                op_grp3_16();
                if ((reg > 1) && (reg < 4)) {
                    writerm16(rm, res16
                    );
                }
                break;

            case 0xF8: /* F8 CLC */
                cf = 0;
                break;

            case 0xF9: /* F9 STC */
                cf = 1;
                break;

            case 0xFA: /* FA CLI */
                ifl = 0;
                break;

            case 0xFB: /* FB STI */
                ifl = 1;
                break;

            case 0xFC: /* FC CLD */
                df = 0;
                break;

            case 0xFD: /* FD STD */
                df = 1;
                break;

            case 0xFE: /* FE GRP4 Eb */
                modregrm();
                oper1b = readrm8(rm);
                oper2b = 1;
                if (!reg) {
                    tempcf = cf;
                    op_add8();
                    cf = tempcf;
                    writerm8(rm, res8);
                } else {
                    tempcf = cf;
                    res8 = oper1b - oper2b;
                    flag_sub8(oper1b, oper2b);
                    cf = tempcf;
                    writerm8(rm, res8);
                }
                break;

            case 0xFF: /* FF GRP5 Ev */
                modregrm();

                oper1 = readrm16(rm);
                op_grp5();
                break;

            default:
#ifdef CPU_ALLOW_ILLEGAL_OP_EXCEPTION
                intcall86(6); /* trip invalid opcode exception. this occurs on the 80186+, 8086/8088 CPUs treat them as NOPs. */
                /* technically they aren't exactly like NOPs in most cases, but for our pursoses, that's accurate enough. */
                printf("[CPU] Invalid opcode 0x%02x exception at %04X:%04X\r\n", opcode, CPU_CS, firstip);
#endif
                break;
        }
        if (was_TF) {
            was_TF = false;
            intcall86(1);
        }
        if (tf) {
            was_TF = true;
        }
    }
}
