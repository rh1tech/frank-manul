/*
 * Iris 2350 - HSTX HDMI Display Driver (640x480x16)
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Uses DispHSTX library for DVI output via HSTX peripheral.
 * Single video mode: 640x480, 4bpp paletted (16 colors).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "display.h"
#include "disphstx.h"
#include <string.h>
#include <stdio.h>
#include <stdalign.h>

// Windows 95 16-color palette (RGB888)
const uint32_t display_palette_rgb888[16] = {
    0x000000, // 0  Black
    0x000080, // 1  Blue (navy)
    0x008000, // 2  Green
    0x008080, // 3  Cyan (teal)
    0x800000, // 4  Red (maroon)
    0x800080, // 5  Magenta (purple)
    0x808000, // 6  Brown (olive)
    0xC0C0C0, // 7  Light Gray (silver)
    0x808080, // 8  Dark Gray
    0x0000FF, // 9  Light Blue
    0x00FF00, // 10 Light Green (lime)
    0x00FFFF, // 11 Light Cyan (aqua)
    0xFF0000, // 12 Light Red
    0xFF00FF, // 13 Light Magenta (fuchsia)
    0xFFFF00, // 14 Yellow
    0xFFFFFF, // 15 White
};

// CGA palette in RGB565 format for DispHSTX
static u16 cga_palette_rgb565[16];

// Framebuffer: 640x480x4bpp = 153,600 bytes
static alignas(4) uint8_t framebuffer[FB_STRIDE * FB_HEIGHT];

uint8_t *display_draw_buffer_ptr = framebuffer;
uint8_t *display_show_buffer_ptr = framebuffer;

// Convert RGB888 to RGB565
static inline u16 rgb888_to_rgb565(uint32_t rgb888) {
    uint8_t r = (rgb888 >> 16) & 0xFF;
    uint8_t g = (rgb888 >> 8) & 0xFF;
    uint8_t b = rgb888 & 0xFF;
    return (u16)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

static void start_mode_640x480x16(void) {
    sDispHstxVModeState *vmode = &DispHstxVMode;
    DispHstxVModeInitTime(vmode, &DispHstxVModeTimeList[vmodetime_640x480_fast]);

    DispHstxVModeAddStrip(vmode, -1);

    int err = DispHstxVModeAddSlot(vmode,
        1,                          // hdbl: 1 = full resolution
        1,                          // vdbl: 1 = no vertical doubling
        -1,                         // w: -1 = full width (640 pixels)
        DISPHSTX_FORMAT_4_PAL,     // 4-bit paletted
        display_show_buffer_ptr,    // framebuffer
        -1,                         // pitch: -1 = auto (320 bytes)
        cga_palette_rgb565,         // CGA palette
        NULL,                       // palvga: not used (DVI only)
        NULL,                       // font: not used
        -1,                         // fonth: auto
        0,                          // gap_col: no separator
        0);                         // gap_len: no separator

    if (err != DISPHSTX_ERR_OK) {
        printf("DispHSTX 640x480 slot error: %d\n", err);
    }

    DispHstxSelDispMode(DISPHSTX_DISPMODE_DVI, vmode);
}

void display_init(void) {
    memset(framebuffer, 0, sizeof(framebuffer));
    display_draw_buffer_ptr = framebuffer;
    display_show_buffer_ptr = framebuffer;

    // Convert palette to RGB565
    for (int i = 0; i < 16; i++) {
        cga_palette_rgb565[i] = rgb888_to_rgb565(display_palette_rgb888[i]);
    }

    start_mode_640x480x16();
}

void display_set_palette_entry(uint8_t index, uint32_t rgb888) {
    if (index < 16)
        cga_palette_rgb565[index] = rgb888_to_rgb565(rgb888);
}

void display_set_pixel(int x, int y, uint8_t color) {
    if ((unsigned)x >= DISPLAY_WIDTH || (unsigned)y >= FB_HEIGHT) return;
    color &= 0x0F;
    uint8_t *p = &display_draw_buffer_ptr[y * FB_STRIDE + (x >> 1)];
    if (x & 1)
        *p = (*p & 0xF0) | color;         // right pixel = low nibble
    else
        *p = (*p & 0x0F) | (color << 4);  // left pixel = high nibble
}

void display_clear(uint8_t color) {
    uint8_t fill = (color << 4) | (color & 0x0F);
    memset(display_draw_buffer_ptr, fill, FB_STRIDE * FB_HEIGHT);
}

void display_wait_vsync(void) {
    DispHstxWaitVSync();
}

void display_hline_fast(int x0, int y, int w, uint8_t color) {
    if (w <= 0) return;

    uint8_t *row = &display_draw_buffer_ptr[y * FB_STRIDE];
    int x_end = x0 + w;
    uint8_t fill = (color << 4) | color;

    if (x0 & 1) {
        uint8_t *p = &row[x0 >> 1];
        *p = (*p & 0xF0) | color;
        x0++;
    }

    if (x_end & 1) {
        x_end--;
        uint8_t *p = &row[x_end >> 1];
        *p = (*p & 0x0F) | (color << 4);
    }

    int byte0 = x0 >> 1;
    int byte1 = x_end >> 1;
    if (byte1 > byte0)
        memset(&row[byte0], fill, byte1 - byte0);
}

void display_blit_glyph_8wide(int x, int y, const uint8_t *glyph,
                               int h, uint8_t fg, uint8_t bg) {
    /* 4bpp: pair-encoded LUT path */
    uint8_t lut[4];
    lut[0] = (bg << 4) | bg;
    lut[1] = (fg << 4) | bg;
    lut[2] = (bg << 4) | fg;
    lut[3] = (fg << 4) | fg;

    int byte_x = x >> 1;

    for (int r = 0; r < h; r++) {
        int py = y + r;
        if ((unsigned)py >= (unsigned)DISPLAY_HEIGHT) continue;
        uint8_t bits = glyph[r];
        uint8_t *dst = &display_draw_buffer_ptr[py * FB_STRIDE + byte_x];
        dst[0] = lut[(bits >> 0) & 3];
        dst[1] = lut[(bits >> 2) & 3];
        dst[2] = lut[(bits >> 4) & 3];
        dst[3] = lut[(bits >> 6) & 3];
    }
}
