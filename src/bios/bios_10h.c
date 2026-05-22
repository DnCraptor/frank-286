#include <stdio.h>
#include "i286.h"
#include "bios.h"

#define BIOS_FONT_SEG       0xF000
#define BIOS_FONT8X16_OFF   0xA000
#define BIOS_FONT8X14_OFF   0xB000
#define BIOS_FONT8X8_OFF    0xBE00

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
 * Update hardware text-mode cursor through the VGA CRT Controller.
 *
 * IBM PC compatible VGA/MDA/CGA adapters store the cursor position
 * inside CRTC registers:
 *
 *   index 0Eh = cursor location high byte
 *   index 0Fh = cursor location low byte
 *
 * The cursor position is NOT stored as X/Y coordinates.
 * Instead, hardware uses a linear character-cell index relative
 * to the start of display memory.
 *
 * Example for standard 80x25 text mode:
 *
 *   row 0 col 0  -> position 0
 *   row 0 col 1  -> position 1
 *   row 1 col 0  -> position 80
 *
 * VGA text memory is organized as:
 *
 *   one character cell = 2 bytes
 *     byte 0 = ASCII character
 *     byte 1 = attribute/color
 *
 * BDA fields used:
 *
 *   40:4A = screen columns
 *   40:4C = video page size in bytes
 *   40:63 = CRTC base port
 *
 * Typical CRTC ports:
 *
 *   3D4h = color adapters (CGA/EGA/VGA)
 *   3B4h = monochrome adapters (MDA/Hercules)
 *
 * page_size is measured in BYTES, while cursor position is measured
 * in CHARACTER CELLS, therefore:
 *
 *   page_size / 2
 *
 * is used when converting page offset into character coordinates.
 *
 * Access protocol:
 *
 *   out(crtc, index)
 *   out(crtc+1, value)
 *
 * This matches real VGA hardware programming.
 */
static void bios_10h_set_crtc_cursor(uint8_t page,
                                     uint8_t row,
                                     uint8_t col)
{
    if (page != read86(0x462)) return;
    /*
     * Number of text columns.
     * Usually 80 in mode 03h.
     */
    uint16_t cols = readw86(0x44A);

    /*
     * Video page size in bytes.
     * Standard VGA text page:
     *   80 * 25 * 2 = 4000 bytes
     * BIOS often rounds to 4096 (1000h).
     */
    uint16_t page_size = readw86(0x44C);

    /*
     * CRT controller I/O base port.
     *
     * 3D4h = color text modes
     * 3B4h = monochrome
     */
    uint16_t crtc = readw86(0x463);

    if (cols == 0)
        cols = 80;

    if (page_size == 0)
        page_size = 0x1000;

    if (crtc == 0)
        crtc = 0x3D4;

    /*
     * Convert page-relative X/Y coordinates into
     * absolute character-cell index.
     *
     * page_size is bytes -> divide by 2 because:
     *   one text cell = 2 bytes
     */
    uint16_t pos =
        (uint16_t)(page * (page_size / 2)) +
        (uint16_t)row * cols +
        col;

    /*
     * VGA_CRTC_CURSOR_HI (0Eh)
     */
    cpu_portout8(crtc, 0x0E);
    cpu_portout8(crtc + 1, pos >> 8);

    /*
     * VGA_CRTC_CURSOR_LO (0Fh)
     */
    cpu_portout8(crtc, 0x0F);
    cpu_portout8(crtc + 1, pos & 0xFF);
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
    bios_10h_set_crtc_cursor(page, row, col);
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
    bios_10h_set_crtc_cursor(page, row, col);
    return true;
}

/*
VIDEO - GET FONT INFORMATION (EGA, MCGA, VGA)
AX = 1130h
BH = pointer specifier
00h INT 1Fh pointer
01h INT 43h pointer
02h ROM 8x14 character font pointer
03h ROM 8x8 double dot font pointer
04h ROM 8x8 double dot font (high 128 characters)
05h ROM alpha alternate (9 by 14) pointer (EGA,VGA)
06h ROM 8x16 font (MCGA, VGA)
07h ROM alternate 9x16 font (VGA only) (see #00021)

Return:
ES:BP = specified pointer
CX    = bytes/character of on-screen font (not the requested font!)
DL    = highest character row on screen
*/
static bool bios_10h_1130h() {
    uint8_t spec = CPU_BH;
    uint16_t off;

    /*
     * CX = bytes/character of current on-screen font,
     * not necessarily of requested BH font.
     *
     * In your BDA init 40:85 = 16, so normal mode 03h reports 16.
     */
    CPU_CX = readw86(0x485);
    if (CPU_CX == 0)
        CPU_CX = 16;

    /*
     * DL = highest character row on screen.
     * In normal 80x25 text mode this is 24.
     */
    CPU_DL = read86(0x484);
    if (CPU_DL == 0)
        CPU_DL = 24;

    switch (spec) {
        case 0x02: /* ROM 8x14 */
        case 0x05: /* alternate 9x14: return same 8x14 raster */
            off = BIOS_FONT8X14_OFF;
            break;

        case 0x03: /* ROM 8x8 */
            off = BIOS_FONT8X8_OFF;
            break;

        case 0x04: /* ROM 8x8 high 128 characters */
            off = BIOS_FONT8X8_OFF + 128u * 8u;
            break;

        case 0x00: /* INT 1Fh pointer */
        case 0x01: /* INT 43h/current font pointer */
        case 0x06: /* ROM 8x16 */
        case 0x07: /* alternate 9x16: return same 8x16 raster */
        default:
            off = BIOS_FONT8X16_OFF;
            break;
    }

    CPU_ES = BIOS_FONT_SEG;
    CPU_BP = off;
    cf = 0;
    return true;
}

bool bios_10h() {
    switch(CPU_AH) {
        case 0x02:
            return bios_10h_02h(); // SET CURSOR POSITION
        case 0x0E:
            return bios_10h_0Eh(); // TELETYPE OUTPUT
        default:
            if (CPU_AX == 0x1130)
                return bios_10h_1130h();
            no_handler();
        case 0x74: // ? HUNTER 16 - SET LCD WINDOWS POSITION
         // unsupported
    }
    cf = 1; // unsuported unknown function
    return true;
}

#include "font8x16.h"
// TODO: other fonts 8x14, 8x8

void bios_10h_install_rom_fonts(void)
{
    /*
     * INT 10h/AX=1130h must return a guest-visible ES:BP pointer.
     * Host pointers to font_8x16/vgafont16 are useless for DOS code,
     * so copy compact ROM font tables into emulated F000:xxxx area.
     *
     * Source font is 8x16. 8x14 and 8x8 are derived minimally:
     *   8x16: all 16 rows
     *   8x14: rows 1..14
     *   8x8 : rows 4..11
     */
    for (uint32_t ch = 0; ch < 256; ch++) {
        for (uint32_t y = 0; y < 16; y++) {
            write86(((uint32_t)BIOS_FONT_SEG << 4) +
                    BIOS_FONT8X16_OFF + ch * 16 + y,
                    font_8x16[ch * 16 + y]);
        }

        for (uint32_t y = 0; y < 14; y++) {
            write86(((uint32_t)BIOS_FONT_SEG << 4) +
                    BIOS_FONT8X14_OFF + ch * 14 + y,
                    font_8x16[ch * 16 + y + 1]);
        }

        for (uint32_t y = 0; y < 8; y++) {
            write86(((uint32_t)BIOS_FONT_SEG << 4) +
                    BIOS_FONT8X8_OFF + ch * 8 + y,
                    font_8x16[ch * 16 + y + 4]);
        }
    }
}
