/*
 * Manul - Font System
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * Based on Iris by Mikhail Matveev / VersaTerm by David Hansel
 *
 * Simplified: only VGA, Terminus, and Terminus Bold fonts.
 * No user font upload, no xmodem.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/sync.h"

#include "font.h"
#include "browser_config.h"

#ifndef _IMG_ASSET_SECTION
#define _IMG_ASSET_SECTION ".data"
#endif

#include "font_vga.h"
#include "font_vga_win1251.h"
#include "font_terminus.h"
#include "font_terminus_bold.h"

#define INFLASHFUN __in_flash(".configfun")

static const uint8_t builtin_font_graphics_char_mapping[31] =
  {0x04, 0xB1, 0x0B, 0x0C, 0x0D, 0x0E, 0xF8, 0xF1,
   0x0F, 0x10, 0xD9, 0xBF, 0xDA, 0xC0, 0xC5, 0x11,
   0x12, 0xC4, 0x13, 0x5F, 0xC3, 0xB4, 0xC1, 0xC2,
   0xB3, 0xF3, 0xF2, 0xE3, 0x1C, 0x9C, 0xFA};

static uint8_t cur_font_normal = 0, cur_font_bold = 0, font_char_height = 16;
static uint8_t __attribute__((aligned(4), section(_IMG_ASSET_SECTION ".font"))) font_blinkoff[8*256*16];
static uint8_t __attribute__((aligned(4), section(_IMG_ASSET_SECTION ".font"))) font_blinkon[8*256*16];

uint8_t font_get_current_id(void) {
    return cur_font_normal;
}

bool font_have_boldfont(void) {
    return cur_font_normal != cur_font_bold;
}

const uint8_t *font_get_data_blinkon(void)  { return font_blinkon; }
const uint8_t *font_get_data_blinkoff(void) { return font_blinkoff; }
uint8_t font_get_char_height(void)          { return font_char_height; }

const uint8_t *font_get_bmpdata(uint8_t fontNum) {
    switch (fontNum) {
    case FONT_ID_VGA:      return font_vga_bmp;
    case FONT_ID_WIN1251:  return font_vga_win1251_bmp;
    case FONT_ID_TERM:     return font_terminus_bmp;
    case FONT_ID_TERMBOLD: return font_terminus_bold_bmp;
    default:               return font_vga_win1251_bmp;  /* default: Win1251 for Cyrillic */
    }
}

bool INFLASHFUN font_get_font_info(uint8_t fontNum, uint32_t *bitmapWidth, uint32_t *bitmapHeight, uint8_t *charHeight, uint8_t *underlineRow) {
    uint8_t ch = 0, ur = 0;
    uint32_t bh = 0, bw = 0;

    switch (fontNum) {
    case FONT_ID_VGA:
    case FONT_ID_WIN1251:
    case FONT_ID_TERM:
    case FONT_ID_TERMBOLD:
        bh = 64; bw = 512; ch = 16; ur = 14;
        break;
    default:
        return false;
    }

    if (bitmapWidth)  *bitmapWidth = bw;
    if (bitmapHeight) *bitmapHeight = bh;
    if (charHeight)   *charHeight = ch;
    if (underlineRow) *underlineRow = ur;
    return true;
}

const INFLASHFUN char *font_get_name(uint8_t fontNum) {
    switch (fontNum) {
    case FONT_ID_VGA:      return "VGA";
    case FONT_ID_WIN1251:  return "VGA Win1251";
    case FONT_ID_TERM:     return "Terminus";
    case FONT_ID_TERMBOLD: return "Terminus bold";
    default:               return NULL;
    }
}

const INFLASHFUN uint8_t font_map_graphics_char(uint8_t c, bool boldFont) {
    (void)boldFont;
    if (c >= 96 && c <= 126)
        c = builtin_font_graphics_char_mapping[c - 96];
    return c;
}

/* ---- Font rendering ---- */

static uint8_t INFLASHFUN reverse_bits(uint8_t b) {
    uint8_t res = 0;
    for (int i = 0; i < 8; i++) {
        res <<= 1;
        if (b & 1) res |= 1;
        b >>= 1;
    }
    return res;
}

static bool INFLASHFUN set_font_data(uint32_t font_offset, uint32_t bitmapWidth, uint32_t bitmapHeight, uint8_t charHeight, uint8_t underlineRow, const uint8_t *bitmapData) {
    if ((bitmapWidth * bitmapHeight) == (2048 * charHeight) && bitmapData != NULL && charHeight > 0) {
        for (int br = 0; br < (int)bitmapHeight; br++)
            for (int bc = 0; bc < (int)(bitmapWidth / 8); bc++) {
                int cr = (bitmapHeight - br - 1) % charHeight;
                int cn = ((bitmapHeight - br - 1) / charHeight) * (bitmapWidth / 8) + bc;

                uint32_t offset = cr * 256 * 8 + cn;
                uint8_t d = bitmapData[br * bitmapWidth / 8 + bc];
                d = reverse_bits(d);
                uint8_t du;
                if (cr == underlineRow)
                    du = 255;
                else
                    du = d;
                font_blinkoff[font_offset + offset + 256 * 0] = d;
                font_blinkoff[font_offset + offset + 256 * 1] = d;
                font_blinkoff[font_offset + offset + 256 * 2] = d;
                font_blinkoff[font_offset + offset + 256 * 3] = du;
                font_blinkon[font_offset + offset + 256 * 0]  = d;
                font_blinkon[font_offset + offset + 256 * 1]  = du;
                font_blinkon[font_offset + offset + 256 * 2]  = ~d;
                font_blinkon[font_offset + offset + 256 * 3]  = ~du;
            }
        return true;
    }
    return false;
}

bool INFLASHFUN font_apply_font(uint8_t font, bool bold) {
    bool res = false;
    uint8_t charHeight, underlineRow;
    uint32_t bitmapHeight, bitmapWidth;

    if (bold && font == 0) font = cur_font_normal;
    if (font != (bold ? cur_font_bold : cur_font_normal)) {
        if (font_get_font_info(font, &bitmapWidth, &bitmapHeight, &charHeight, &underlineRow))
            if (set_font_data(bold ? 4 * 256 : 0, bitmapWidth, bitmapHeight, charHeight, underlineRow, font_get_bmpdata(font))) {
                if (bold)
                    cur_font_bold = font;
                else
                    cur_font_normal = font;
                font_char_height = charHeight;
                res = true;
            }
    } else {
        res = true;
    }
    return res;
}

void INFLASHFUN font_apply_settings(void) {
    uint8_t fn = config_get_screen_font_normal();
    uint8_t fb = config_get_screen_font_bold();
    uint8_t chn, chb;

    font_apply_font(fn, false);
    if (!font_get_font_info(fn, NULL, NULL, &chn, NULL) || !font_get_font_info(fb, NULL, NULL, &chb, NULL) || chn != chb)
        fb = 0;
    font_apply_font(fb, true);
}

void INFLASHFUN font_init(void) {
    font_apply_settings();
}
