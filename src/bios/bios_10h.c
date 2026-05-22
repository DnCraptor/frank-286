#include <stdio.h>
#include "i286.h"
#include "bios.h"

// Cursor state
extern int cursor_x, cursor_y;
extern int cursor_start, cursor_end;
extern int cursor_blink_state;

static bool no_handler() {
    print_line("VIDEO BIOS - ERROR: no handler defined", 1);
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
VIDEO - SET CURSOR POSITION
AH = 02h
BH = page number
0-3 in modes 2&3
0-7 in modes 0&1
0 in graphics modes
DH = row (00h is top)
DL = column (00h is left)

Return:
Nothing
*/
static bool bios_10h_02h() {
    uint8_t page = CPU_BH;
    uint8_t row = CPU_DH;
    uint8_t col = CPU_DL;
    uint16_t cur;
    /*
     * BDA:
     * 40:50..5F = cursor positions for pages
     * high byte = row
     * low byte  = column
     */
    cur = ((uint16_t)row << 8) | col;
    writew86(0x450 + ((uint16_t)page * 2), cur);

    /*
     * Update visible hardware cursor only for active page.
     */
    if (page == read86(0x462)) {
        cursor_x = col;
        cursor_y = row;
    }
    return true;
}

/*
AH = 0Eh
AL = character to write
BH = page number
BL = foreground color (graphics modes only)

Return:
Nothing

Desc: Display a character on the screen, advancing the cursor and scrolling the screen as necessary

Notes: Characters 07h (BEL), 08h (BS), 0Ah (LF), and 0Dh (CR) are interpreted and do the expected things.
 IBM PC ROMs dated 1981/4/24 and 1981/10/19 require that BH be the same as the current active page
*/
static bool bios_10h_0Eh() {
    uint8_t ch = CPU_AL;
    uint8_t mode = read86(0x449);
    uint16_t cols = readw86(0x44A);
    uint16_t page_size = readw86(0x44C);
    uint8_t active_page = read86(0x462);
    uint8_t page = CPU_BH;
    uint8_t rows_minus_1 = read86(0x484);
    if (cols == 0)
        cols = 80;
    if (rows_minus_1 == 0)
        rows_minus_1 = 24;
    if (page_size == 0)
        page_size = 0x1000;

    /*
     * IBM PC ROM 1981 требовал BH == active page.
     * Для DOS boot/debug output безопаснее игнорировать вывод
     * в неактивную страницу, но не падать.
     */
    if (page != active_page)
        return true;
    /*
     * Только текстовые режимы.
     * mode 7 = MDA, остальное типично B800.
     */
    uint32_t vram_base = (mode == 7) ? 0xB0000u : 0xB8000u;
    uint16_t page_off = (uint16_t)page * page_size;

    uint16_t cur = readw86(0x450 + ((uint16_t)page * 2));
    uint8_t row = (uint8_t)(cur >> 8);
    uint8_t col = (uint8_t)(cur & 0xFF);

    uint8_t rows = rows_minus_1 + 1;
    uint8_t attr = 0x07;

    if (mode != 7) {
        /*
         * Для AH=0Eh BL используется только в graphics modes.
         * В текстовом режиме BIOS пишет с текущим атрибутом.
         * Берём атрибут из текущей позиции, если возможно.
         */
        uint32_t cell = vram_base + page_off + ((uint32_t)row * cols + col) * 2u;
        attr = read86(cell + 1);
        if (attr == 0)
            attr = 0x07;
    }

    switch (ch) {
        case 0x07: /* BEL */
            return true;

        case 0x08: /* BS */
            if (col > 0)
                col--;
            break;

        case 0x0D: /* CR */
            col = 0;
            break;

        case 0x0A: /* LF */
            row++;
            break;

        default: {
            uint32_t cell = vram_base + page_off + ((uint32_t)row * cols + col) * 2u;
            write86(cell + 0, ch);
            write86(cell + 1, attr);

            col++;
            if (col >= cols) {
                col = 0;
                row++;
            }
            break;
        }
    }

    if (row >= rows) {
        uint32_t page_base = vram_base + page_off;
        uint16_t line_bytes = cols * 2;

        for (uint16_t r = 1; r < rows; r++) {
            for (uint16_t x = 0; x < line_bytes; x++)
                write86(page_base + (uint32_t)(r - 1) * line_bytes + x,
                        read86(page_base + (uint32_t)r * line_bytes + x));
        }

        for (uint16_t c = 0; c < cols; c++) {
            uint32_t cell = page_base + (uint32_t)(rows - 1) * line_bytes + c * 2u;
            write86(cell + 0, ' ');
            write86(cell + 1, attr);
        }

        row = rows - 1;
    }

    cur = ((uint16_t)row << 8) | col;
    writew86(0x450 + ((uint16_t)page * 2), cur);
    cursor_x = col;
    cursor_y = row;
    return true;
}

bool bios_10h() {
    switch(CPU_AH) {
        case 0x02:
            return bios_10h_02h(); // SET CURSOR POSITION
        case 0x0E:
            return bios_10h_0Eh(); // TELETYPE OUTPUT
        default:
            no_handler();
    }
    return true;
}
