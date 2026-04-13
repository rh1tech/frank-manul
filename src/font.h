/*
 * Manul - Font System
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * Based on Iris by Mikhail Matveev / VersaTerm by David Hansel
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef FONT_H
#define FONT_H

#include <stdint.h>
#include <stdbool.h>

#define FONT_CHAR_WIDTH  8

#define FONT_ID_NONE     0
#define FONT_ID_CGA      1
#define FONT_ID_EGA      2
#define FONT_ID_VGA      3
#define FONT_ID_TERM     4
#define FONT_ID_TERMBOLD 5
#define FONT_ID_PETSCII  6
#define FONT_ID_CP866    7
#define FONT_ID_WIN1251  8
#define FONT_ID_KOI8R    9
#define FONT_ID_USER1    10
#define FONT_ID_USER2    11
#define FONT_ID_USER3    12
#define FONT_ID_USER4    13

uint8_t        font_get_current_id(void);
bool           font_have_boldfont(void);
const uint8_t *font_get_data_blinkon(void);
const uint8_t *font_get_data_blinkoff(void);

uint8_t        font_get_char_height(void);
bool           font_get_font_info(uint8_t fontNum, uint32_t *bitmapWidth, uint32_t *bitmapHeight, uint8_t *charHeight, uint8_t *underlineRow);
const char    *font_get_name(uint8_t fontNum);
const uint8_t *font_get_bmpdata(uint8_t fontNum);
const uint8_t  font_map_graphics_char(uint8_t c, bool boldFont);

bool font_apply_font(uint8_t font, bool bold);

void font_apply_settings(void);
void font_init(void);

#endif
