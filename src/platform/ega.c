#include "platform/ega.h"

#include <string.h>

#include "host.h"
#include "enhanced/enhanced.h"

static u8 planes[4][EGA_PLANE_SIZE];
static u16 start_addr;
static int line_compare = 200;
static bool dirty = true;
static u8 written[EGA_PLANE_SIZE];     /* ENH: pixels (bit 7 = leftmost) written since ega_clear_written */
static int scale = 1;                  /* ENH: output = 320x200 times this */
static u8 fg[EGA_PLANE_SIZE];          /* ENH: pixels of the foreground layer (see ega_set_layer) */
static int layer;
/* Mode 0Dh defaults after INT 10h AH=0 (200-line values), overscan 0. */
static u8 palette_reg[17] = { 0, 1, 2, 3, 4, 5, 6, 7, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0 };

const u32 ega_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static bool compose(u32 *xrgb)
{
    static bool enh_shown;
    if (!dirty && !enh_shown) return false;
    dirty = false;
    u32 rgb[16];
    for (int i = 0; i < 16; i++) {
        u8 v = palette_reg[i];              /* 200-line decoding: bits 0-2 BGR, bit 4 intensity */
        rgb[i] = ega_palette[(v & 7) | (v >> 1 & 8)];
    }
    int w = 320 * scale;
    for (int y = 0; y < 200; y++) {
        u16 row = y < line_compare ? (u16)(start_addr + y * EGA_BYTES_PER_LINE)
                                   : (u16)((y - line_compare) * EGA_BYTES_PER_LINE);
        u32 *out = xrgb + (size_t)y * scale * w;
        for (int b = 0; b < EGA_BYTES_PER_LINE; b++) {
            u16 o = (u16)(row + b);
            u8 p0 = planes[0][o], p1 = planes[1][o], p2 = planes[2][o], p3 = planes[3][o];
            for (int bit = 7; bit >= 0; bit--) {
                int c = (p0 >> bit & 1) | (p1 >> bit & 1) << 1 | (p2 >> bit & 1) << 2 | (p3 >> bit & 1) << 3;
                for (int i = 0; i < scale; i++) *out++ = rgb[c];
            }
        }
        /* ENH: the picture scaled up; the rows below repeat the first */
        u32 *first = xrgb + (size_t)y * scale * w;
        for (int j = 1; j < scale; j++) memcpy(first + (size_t)j * w, first, sizeof(u32) * (size_t)w);
    }
    enh_shown = enh_compose(xrgb, rgb);     /* ENH: the enhanced view over the picture */
    return true;
}

void ega_init(void)
{
    memset(planes, 0, sizeof planes);
    start_addr = 0;
    line_compare = 200;
    dirty = true;
    memset(written, 0, sizeof written);
    scale = enh_res_scale();
    host_set_frame_source(compose, 320 * scale, 200 * scale);
}

u8 *ega_plane(int plane) { return planes[plane & 3]; }

void ega_set_start(u16 offset)
{
    if (offset != start_addr) { start_addr = offset; dirty = true; }
}

void ega_set_line_compare(int line)
{
    if (line < 0) line = 0;
    if (line > 200) line = 200;
    if (line != line_compare) { line_compare = line; dirty = true; }
}

void ega_touch(void) { dirty = true; }

void ega_set_palette_reg(int reg, u8 value)
{
    if (reg < 0 || reg > 16) return;
    if (palette_reg[reg] != value) { palette_reg[reg] = value; dirty = true; }
}

u8 ega_get_palette_reg(int reg)
{
    return (reg >= 0 && reg <= 16) ? palette_reg[reg] : 0;
}

/* ENH: written mask (enhanced.c coverage) */
void ega_mark_written(u16 offset, u8 pixels)
{
    written[offset] |= pixels;
    if (layer == EGA_LAYER_FG) fg[offset] |= pixels;
    else if (layer == EGA_LAYER_BG) fg[offset] &= (u8)~pixels;
}
void ega_set_layer(int l) { layer = l; }
const u8 *ega_fg(void) { return fg; }
void ega_clear_fg(u16 offset, u16 n)
{
    for (u16 i = 0; i < n; i++) fg[(u16)(offset + i)] = 0;
}
const u8 *ega_written(void) { return written; }
void ega_clear_written(u16 offset, u16 n)
{
    for (u16 i = 0; i < n; i++) written[(u16)(offset + i)] = 0;
}
u16 ega_start(void) { return start_addr; }
int ega_line_compare(void) { return line_compare; }
