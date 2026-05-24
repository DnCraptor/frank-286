#ifndef I286_H
#define I286_H

#include <stdint.h>
#include <stdbool.h>
#include "mem.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

typedef int32_t s32;
typedef int16_t s16;
typedef int8_t s8;

typedef uint32_t uword;

typedef struct {
	void *pic;
	int (*pic_read_irq)(void *);

	void *io;
	u8 (*io_read8)(void *, int);
	void (*io_write8)(void *, int, u8);
	u16 (*io_read16)(void *, int);
	void (*io_write16)(void *, int, u16);
	u32 (*io_read32)(void *, int);
	void (*io_write32)(void *, int, u32);
	int (*io_read_string)(void *, int, uint32_t, int, int);
	int (*io_write_string)(void *, int, uint32_t, int, int);

	void *iomem;
} CPU_CB;

typedef struct i286 i286_fwd;
typedef bool (*int2f_handler_t)(i286_fwd *cpu, void *opaque);

typedef struct i286 {
    CPU_CB cb;
    int2f_handler_t int2f_handler;
    void           *int2f_opaque;
} i286;

typedef union {
    uint32_t value;
    struct {
        unsigned CF : 1;  // 0 bit of value
        unsigned _1 : 1;  // 1
        unsigned PF : 1;  // 2
        unsigned _3 : 1;  // 3
        unsigned AF : 1;  // 4
        unsigned _5 : 1;  // 5
        unsigned ZF : 1;  // 6
        unsigned SF : 1;  // 7
        unsigned TF : 1;  // 8
        unsigned IF : 1;  // 9
        unsigned DF : 1;  // 10
        unsigned OF : 1;  // 11
        unsigned _12 : 1;
        unsigned _13 : 1;
        unsigned _14 : 1;
        unsigned _15 : 1;
        unsigned _16 : 1;
        unsigned _17 : 1;
        unsigned AC : 1; // 18 (Alignment Check)	Проверка выравнивания (включается в CPL=3 при CR0.AM=1)
                         // (Alignment Check Exception) — INT 17 (11h)
        unsigned VIF : 1; // 19 (Virtual Interrupt Flag)	Виртуальный IF для виртуализации (введён в 486, но зарезервирован с 386)
        unsigned VIP : 1; // 20 (Virtual Interrupt Pending)	Виртуальное прерывание ожидает (аналогично — введён в 486)
        unsigned ID : 1; // 21 (ID Flag)	Позволяет проверить поддержку CPUID инструкцией
    } bits;
} x86_flags_t;

extern x86_flags_t x86_flags;

#define cf  x86_flags.bits.CF
#define pf  x86_flags.bits.PF
#define af  x86_flags.bits.AF
#define zf  x86_flags.bits.ZF
#define sf  x86_flags.bits.SF
#define tf  x86_flags.bits.TF
#define ifl x86_flags.bits.IF
#define df  x86_flags.bits.DF
#define of  x86_flags.bits.OF

#define regax 0
#define regcx 1
#define regdx 2
#define regbx 3
#define regsp 4
#define regbp 5
#define regsi 6
#define regdi 7

#define reges 0
#define regcs 1
#define regss 2
#define regds 3
#define regfs 4
#define reggs 5

// eax
#define regal 0
#define regah 1
// 2, 3

// ecx
#define regcl 4
#define regch 5
// 6, 7

// edx
#define regdl 8
#define regdh 9
// 10, 11

// ebx
#define regbl 12
#define regbh 13
// 14, 14

#define CPU_AX    wordregs[regax << 1]
#define CPU_BX    wordregs[regbx << 1]
#define CPU_CX    wordregs[regcx << 1]
#define CPU_DX    wordregs[regdx << 1]
#define CPU_SI    wordregs[regsi << 1]
#define CPU_DI    wordregs[regdi << 1]
#define CPU_BP    wordregs[regbp << 1]
#define CPU_SP    wordregs[regsp << 1]

extern uint32_t segregs32[6];
extern uint32_t dwordregs[8];
#define byteregs ((uint8_t*)dwordregs)
#define wordregs ((uint16_t*)dwordregs)
#define segregs ((uint16_t*)segregs32)

extern uint32_t ip32;
#define CPU_IP    (*(uint16_t*)&ip32)
#define ip        (*(uint16_t*)&ip32)

#define CPU_FL_CF    cf
#define CPU_FL_PF    pf
#define CPU_FL_AF    af
#define CPU_FL_ZF    zf
#define CPU_FL_SF    sf
#define CPU_FL_TF    tf
#define CPU_FL_IFL   ifl
#define CPU_FL_DF    df
#define CPU_FL_OF    of

#define FLAG_CF_OF_MASK ((1u << 11) | 1)
#define FLAG_CF_AF_MASK ((1u << 4) | 1)

#define CPU_CS    segregs[regcs << 1]
#define CPU_DS    segregs[regds << 1]
#define CPU_ES    segregs[reges << 1]
#define CPU_SS    segregs[regss << 1]
#define CPU_FS    segregs[regfs << 1]
#define CPU_GS    segregs[reggs << 1]

#define CPU_EAX   dwordregs[regax]
#define CPU_EBX   dwordregs[regbx]
#define CPU_ECX   dwordregs[regcx]
#define CPU_EDX   dwordregs[regdx]
#define CPU_ESI   dwordregs[regsi]
#define CPU_EDI   dwordregs[regdi]
#define CPU_EBP   dwordregs[regbp]
#define CPU_ESP   dwordregs[regsp]

#define CPU_AL    byteregs[regal]
#define CPU_BL    byteregs[regbl]
#define CPU_CL    byteregs[regcl]
#define CPU_DL    byteregs[regdl]
#define CPU_AH    byteregs[regah]
#define CPU_BH    byteregs[regbh]
#define CPU_CH    byteregs[regch]
#define CPU_DH    byteregs[regdh]

void modregrm();
void getea(uint8_t rmval);

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)
#define INLINE __always_inline

extern int a20_enabled;
static inline u32 cpu_a20_addr(u32 addr) {
    return a20_enabled ? addr : (addr & 0x000FFFFF);
}

static inline u8 read86(const u32 address) {
    return pload8(cpu_a20_addr(address));
}
static inline u16 readw86(const u32 address) {
    return pload16(cpu_a20_addr(address));
}
static inline u32 readdw86(const u32 address) {
    return pload32(cpu_a20_addr(address));
}

static inline void write86(const u32 address, u8 v) {
    pstore8(cpu_a20_addr(address), v);
}

static inline void writew86(const u32 address, u16 v) {
    pstore16(cpu_a20_addr(address), v);
}

static inline void writedw86(const u32 address, u32 v) {
    pstore32(cpu_a20_addr(address), v);
}

int cpu_get_a20(i286* cpu);

uword cpu_getflags(i286* cpu);
void cpu_setflags(i286* cpu, uword set_mask, uword clear_mask);

u8 cpu_get_al(i286 *cpu);
u8 cpu_get_ah(i286 *cpu);
u8 cpu_get_bl(i286 *cpu);
u8 cpu_get_bh(i286 *cpu);
u8 cpu_get_cl(i286 *cpu);
u8 cpu_get_ch(i286 *cpu);
u8 cpu_get_dl(i286 *cpu);
u8 cpu_get_dh(i286 *cpu);
void cpu_set_al(i286 *cpu, u8 val);

u16 cpu_get_ax(i286* cpu);
u16 cpu_get_bx(i286* cpu);
u16 cpu_get_cx(i286* cpu);
u16 cpu_get_dx(i286* cpu);
u16 cpu_get_es(i286* cpu);
u16 cpu_get_di(i286* cpu);
void cpu_set_bx(i286 *cpu, u16 val);
void cpu_set_cx(i286 *cpu, u16 val);
void cpu_set_dx(i286 *cpu, u16 val);
void cpu_set_di(i286 *cpu, u16 val);
void cpu_set_a20(i286* cpu, int v);
void cpu_setax(i286* cpu, int v);
void cpu_setexc(i286* cpu, int v, int f);
void cpu_set_cf(i286* cpu, int v);

bool cpu_load8(i286* cpu, u16 seg, u16 off, u8* res);
bool cpu_load16(i286* cpu, u16 seg, u16 off, u16* res);
bool cpu_load32(i286* cpu, u16 seg, u16 off, u32* res);

bool cpu_store8(i286* cpu, u16 seg, u16 off, u8 val);
bool cpu_store16(i286* cpu, u16 seg, u16 off, u16 val);
bool cpu_store32(i286* cpu, u16 seg, u16 off, u32 val);

void cpu_set_int2f_handler(i286 *cpu, int2f_handler_t handler, void *opaque);

void i286_step(i286 *cpu, uint32_t n);
void cpu_raise_irq(void*);

void cpu_portout8(u16 port, u8 val);
u8   cpu_portin8 (u16 port);
void intcall86(uint8_t intnum);
i286* i286_new(CPU_CB* *cb);
void i286_enable_fpu(i286 *);
void i286_reset(i286 *cpu);
void i286_reset_pm(i286 *cpu, uint32_t start_addr);

/* Read one CMOS register via I/O ports (matches what real BIOS does). */
static inline uint8_t cmos_read(uint8_t reg)
{
    cpu_portout8(0x70, reg);
    return cpu_portin8(0x71);
}

/* Write one CMOS register via I/O ports. */
static inline void cmos_write(uint8_t reg, uint8_t val)
{
    cpu_portout8(0x70, reg);
    cpu_portout8(0x71, val);
}

inline static void print_char(char c, int char_row, int char_pos) {
    writew86(0xB8000 + char_row * 160 + char_pos * 2, 0x0F00 | c);
}

inline static void print_line(const char* s, int row) {
    if (!s) return;
    for (int col = 0; col < 80 && s[col]; ++col) {
        print_char(s[col], row, col);
    }
}

// 0xFFE00..0xFFEFF
inline static bool fake_bios_area() {
    u32 ip32 = (((u32)CPU_CS << 4) + CPU_IP) >> 8;
    return ip32 == 0xFFE;
}

#endif // I286_H
