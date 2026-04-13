/*
 * Iris 2350 - HSTX HDMI Display Driver (640x480x16)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>
#include <string.h>

#define DISPLAY_WIDTH  640
#define DISPLAY_HEIGHT 480
#define FB_WIDTH       320      /* bytes per row (4bpp: 2 pixels/byte) */
#define FB_HEIGHT      480
#define FB_STRIDE      320
#define NUM_COLORS     16

extern uint8_t *display_draw_buffer_ptr;
extern uint8_t *display_show_buffer_ptr;

void display_init(void);
void display_set_pixel(int x, int y, uint8_t color);
void display_clear(uint8_t color);
void display_wait_vsync(void);

/* Fast 8-wide glyph blitter (4bpp mode).
 * x must be even (always true at 8px grid). Writes directly to framebuffer.
 * glyph = pointer to font row data (h bytes, 1 byte/row, bit0=leftmost).
 * fg, bg = 4-bit color indices. */
void display_blit_glyph_8wide(int x, int y, const uint8_t *glyph,
                               int h, uint8_t fg, uint8_t bg);

/* Fast horizontal span fill (no bounds check). 4bpp mode. */
void display_hline_fast(int x0, int y, int w, uint8_t color);

/* 16-color CGA palette (RGB888 values) */
extern const uint32_t display_palette_rgb888[16];

/* Set a palette entry at runtime (index 0..15, rgb888 = 0xRRGGBB) */
void display_set_palette_entry(uint8_t index, uint32_t rgb888);

#endif
