/*
 * Manul - Text Framebuffer
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * Based on Iris by Mikhail Matveev / VersaTerm by David Hansel
 *
 * Maintains a character+attribute+color buffer and renders glyphs
 * to the 4bpp HSTX framebuffer via display_blit_glyph_8wide().
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "font.h"
#include "browser_config.h"
#include "sound.h"
#include "keyboard.h"
#include "framebuf.h"
#include "display.h"
#include "pico/time.h"

// defined in main.c
void wait(uint32_t milliseconds);

/*==========================================================================
 * Internal character cell storage
 *
 * Each cell stores: character (8 bit), attribute (8 bit), fg (4 bit), bg (4 bit)
 * Packed as: charbuf[idx] = char | (attr << 8)
 *            colorbuf[idx] = fg | (bg << 4)
 *==========================================================================*/

static uint16_t charbuf[80 * 60];
static uint8_t  colorbuf[80 * 60];

static bool screen_inverted = false;
static uint8_t num_rows = 0, num_cols = 0, xborder = 0, yborder = 0;
static uint16_t scroll_delay = 0;

int16_t framebuf_flash_counter = 0;
uint8_t framebuf_flash_color = 0;

static bool blink_phase = false;
static absolute_time_t blink_next_toggle = 0;

static uint8_t color_map[16];
static uint8_t color_map_inv[16];

#define MKIDX(x, y) (((x) + xborder) + (((y) + yborder) * (FRAME_WIDTH / FONT_CHAR_WIDTH)))

/*==========================================================================
 * Render a single character cell to the HSTX framebuffer
 *==========================================================================*/
static void render_cell(uint32_t idx) {
    uint8_t ch     = charbuf[idx] & 0xFF;
    uint8_t attr   = charbuf[idx] >> 8;
    uint8_t fg     = colorbuf[idx] & 0x0F;
    uint8_t bg     = (colorbuf[idx] >> 4) & 0x0F;

    bool cell_inv = (attr & ATTR_INVERSE) != 0;
    if (cell_inv != screen_inverted) {
        uint8_t t = fg; fg = bg; bg = t;
    }

    uint32_t max_cols = FRAME_WIDTH / FONT_CHAR_WIDTH;
    int col = idx % max_cols;
    int row = idx / max_cols;
    int px = col * FONT_CHAR_WIDTH;
    int py = row * font_get_char_height();

    if (px >= FRAME_WIDTH || py >= FRAME_HEIGHT)
        return;

    /* Use blinkon font when underline is set — in blinkoff, plane 1
     * (underline) is identical to plane 0 (normal), so underlines
     * would be invisible.  blinkon plane 1 has actual underline. */
    const uint8_t *font = (blink_phase || (attr & ATTR_UNDERLINE))
                          ? font_get_data_blinkon() : font_get_data_blinkoff();
    int plane = (attr & (ATTR_UNDERLINE | ATTR_BLINK));
    uint8_t h = font_get_char_height();
    uint8_t glyph_buf[16];
    for (uint8_t r = 0; r < h && r < 16; r++)
        glyph_buf[r] = font[r * 256 * 8 + ch + plane * 256];

    display_blit_glyph_8wide(px, py, glyph_buf, h, fg, bg);
}

/*==========================================================================
 * Render all visible cells to the framebuffer
 *==========================================================================*/
static void render_all(void) {
    uint32_t max_cols = FRAME_WIDTH / FONT_CHAR_WIDTH;
    uint32_t max_rows = FRAME_HEIGHT / font_get_char_height();
    for (uint32_t r = 0; r < max_rows; r++)
        for (uint32_t c = 0; c < max_cols; c++)
            render_cell(r * max_cols + c);
}

/*==========================================================================
 * charmemset: fill a range of cells and re-render
 *==========================================================================*/
static void charmemset(uint32_t idx, uint8_t c, uint8_t a, uint8_t fg, uint8_t bg, size_t n) {
    if (screen_inverted) {
        uint8_t t = fg; fg = bg; bg = t;
    }

    uint16_t cv = c | ((uint16_t)a << 8);
    uint8_t  col = (fg & 0x0F) | ((bg & 0x0F) << 4);

    for (size_t i = 0; i < n; i++) {
        charbuf[idx + i] = cv;
        colorbuf[idx + i] = col;
    }

    for (size_t i = 0; i < n; i++)
        render_cell(idx + i);
}

/*==========================================================================
 * charmemmove: move cells and re-render affected area
 *==========================================================================*/
static void charmemmove(uint32_t toidx, uint32_t fromidx, size_t n) {
    memmove(&charbuf[toidx], &charbuf[fromidx], n * sizeof(uint16_t));
    memmove(&colorbuf[toidx], &colorbuf[fromidx], n);

    for (size_t i = 0; i < n; i++)
        render_cell(toidx + i);
}

/*==========================================================================
 * Public API
 *==========================================================================*/

uint8_t framebuf_get_nrows(void) {
    return num_rows;
}

uint8_t framebuf_get_ncols(int row) {
    (void)row;
    return num_cols;
}

void framebuf_set_char(uint8_t x, uint8_t y, uint8_t c) {
    if (y < num_rows && x < num_cols) {
        uint32_t idx = MKIDX(x, y);
        charbuf[idx] = (charbuf[idx] & 0xFF00) | c;
        render_cell(idx);
    }
}

uint8_t framebuf_get_char(uint8_t x, uint8_t y) {
    if (y < num_rows && x < num_cols)
        return charbuf[MKIDX(x, y)] & 0xFF;
    return 0;
}

void framebuf_set_attr(uint8_t x, uint8_t y, uint8_t attr) {
    if (y < num_rows && x < num_cols) {
        uint32_t idx = MKIDX(x, y);
        uint8_t prev_attr = charbuf[idx] >> 8;

        if (!font_have_boldfont() && (attr & ATTR_BOLD) != (prev_attr & ATTR_BOLD)) {
            uint8_t fg = colorbuf[idx] & 0x0F;
            if (attr & ATTR_BOLD)
                fg = color_map[(color_map_inv[fg] & 0x0F) | 8];
            else
                fg = color_map[(color_map_inv[fg] & 0x0F) & 7];
            colorbuf[idx] = (colorbuf[idx] & 0xF0) | (fg & 0x0F);
        }

        charbuf[idx] = (charbuf[idx] & 0x00FF) | ((uint16_t)attr << 8);
        render_cell(idx);
    }
}

uint8_t framebuf_get_attr(uint8_t x, uint8_t y) {
    if (y < num_rows && x < num_cols)
        return charbuf[MKIDX(x, y)] >> 8;
    return 0;
}

void framebuf_set_color(uint8_t x, uint8_t y, uint8_t fg, uint8_t bg) {
    if (y < num_rows && x < num_cols) {
        fg = color_map[fg & 0x0F];
        bg = color_map[bg & 0x0F];
        if (screen_inverted) { uint8_t t = fg; fg = bg; bg = t; }
        uint32_t idx = MKIDX(x, y);
        colorbuf[idx] = (fg & 0x0F) | ((bg & 0x0F) << 4);
        render_cell(idx);
    }
}

void framebuf_set_fullcolor(uint8_t x, uint8_t y, uint8_t fg, uint8_t bg) {
    if (y < num_rows && x < num_cols) {
        if (screen_inverted) { uint8_t t = fg; fg = bg; bg = t; }
        uint32_t idx = MKIDX(x, y);
        colorbuf[idx] = (fg & 0x0F) | ((bg & 0x0F) << 4);
        render_cell(idx);
    }
}

void framebuf_fill_screen(char character, uint8_t fg, uint8_t bg) {
    framebuf_fill_region(0, 0, num_cols - 1, num_rows - 1, character, fg, bg);
}

void framebuf_fill_region(uint8_t xs, uint8_t ys, uint8_t xe, uint8_t ye,
                           char c, uint8_t fg, uint8_t bg) {
    if (ys < num_rows && xs < num_cols && ye < num_rows && xe < num_cols) {
        fg = color_map[fg & 0x0F];
        bg = color_map[bg & 0x0F];
        uint8_t def_attr = config_get_terminal_default_attr();

        if (xs > 0) {
            charmemset(MKIDX(xs, ys), c, def_attr, fg, bg, num_cols - xs);
            ys++;
        }
        if (xe < num_cols - 1) {
            charmemset(MKIDX(0, ye), c, def_attr, fg, bg, xe + 1);
            if (ye > 0) ye--;
            else return;
        }
        while (ys <= ye) {
            charmemset(MKIDX(0, ys), c, def_attr, fg, bg, num_cols);
            ys++;
        }
    }
}

void framebuf_scroll_screen(int8_t n, uint8_t fg, uint8_t bg) {
    framebuf_scroll_region(0, num_rows - 1, n, fg, bg);
}

void framebuf_scroll_region(uint8_t start, uint8_t end, int8_t n, uint8_t fg, uint8_t bg) {
    if (n != 0 && start < num_rows && end < num_rows) {
        fg = color_map[fg & 0x0F];
        bg = color_map[bg & 0x0F];

        if (scroll_delay > 0)
            wait(scroll_delay);

        uint32_t max_cols = FRAME_WIDTH / FONT_CHAR_WIDTH;
        uint8_t def_attr = config_get_terminal_default_attr();

        if (n > 0) {
            if (n <= end - start) {
                for (int y = start; y <= end - n; y++)
                    charmemmove(MKIDX(0, y), MKIDX(0, y + n), max_cols - xborder * 2);
            }
            if (n > end - start + 1) n = end - start + 1;
            for (int y = 0; y < n; y++)
                charmemset(MKIDX(0, end + y + 1 - n), ' ', def_attr, fg, bg, num_cols);
        } else {
            n = -n;
            if (n <= end - start) {
                for (int y = end - n; y >= start; y--)
                    charmemmove(MKIDX(0, y + n), MKIDX(0, y), max_cols - xborder * 2);
            }
            if (n > end - start + 1) n = end - start + 1;
            for (int i = 0; i < n; i++)
                charmemset(MKIDX(0, start + i), ' ', def_attr, fg, bg, num_cols);
        }
    }
}

void framebuf_insert(uint8_t x, uint8_t y, uint8_t n, uint8_t fg, uint8_t bg) {
    if (y < num_rows && x < num_cols) {
        fg = color_map[fg & 0x0F];
        bg = color_map[bg & 0x0F];
        for (int i = 0; i < ((int)num_cols) - (x + n); i++) {
            int col = num_cols - i - 1;
            uint32_t dst = MKIDX(col, y);
            uint32_t src = MKIDX(col - n, y);
            charbuf[dst] = charbuf[src];
            colorbuf[dst] = colorbuf[src];
            render_cell(dst);
        }
        for (int i = 0; i < n && x + i < num_cols; i++) {
            uint32_t idx = MKIDX(x + i, y);
            charbuf[idx] = ' ';
            colorbuf[idx] = (fg & 0x0F) | ((bg & 0x0F) << 4);
            render_cell(idx);
        }
    }
}

void framebuf_delete(uint8_t x, uint8_t y, uint8_t n, uint8_t fg, uint8_t bg) {
    if (y < num_rows && x < num_cols) {
        fg = color_map[fg & 0x0F];
        bg = color_map[bg & 0x0F];
        for (int i = 0; i < ((int)num_cols) - (x + n); i++) {
            uint32_t dst = MKIDX(x + i, y);
            uint32_t src = MKIDX(x + n + i, y);
            charbuf[dst] = charbuf[src];
            colorbuf[dst] = colorbuf[src];
            render_cell(dst);
        }
        for (int i = 0; i < n && i < num_cols - x; i++) {
            uint32_t idx = MKIDX(num_cols - 1 - i, y);
            charbuf[idx] = ' ';
            colorbuf[idx] = (fg & 0x0F) | ((bg & 0x0F) << 4);
            render_cell(idx);
        }
    }
}

void framebuf_set_screen_size(uint8_t ncols, uint8_t nrows) {
    uint32_t max_cols = FRAME_WIDTH / FONT_CHAR_WIDTH;
    uint32_t max_rows_val = FRAME_HEIGHT / font_get_char_height();
    if (nrows > max_rows_val) nrows = max_rows_val;
    if (ncols > max_cols) ncols = max_cols;

    if (num_rows != nrows || num_cols != ncols) {
        screen_inverted = false;

        for (int i = 0; i < 16; i++) {
            color_map[i] = i;
            color_map_inv[i] = i;
        }

        num_rows = nrows;
        num_cols = ncols;
        xborder = (max_cols - ncols) / 2;
        yborder = (max_rows_val - nrows) / 2;

        memset(charbuf, 0, sizeof(charbuf));
        memset(colorbuf, 0, sizeof(colorbuf));
        display_clear(0);

        uint8_t dfg = config_get_terminal_default_fg();
        uint8_t dbg = config_get_terminal_default_bg();
        charmemset(0, ' ', 0, color_map[dfg], color_map[dbg], max_cols * max_rows_val);
    }
}

void framebuf_set_screen_inverted(bool invert) {
    if (invert != screen_inverted) {
        screen_inverted = invert;
        render_all();
    }
}

void framebuf_set_scroll_delay(uint16_t ms) {
    scroll_delay = ms;
}

void framebuf_flash_screen(uint8_t color, uint8_t nframes) {
    display_clear(color & 0x0F);
    wait(nframes * 16);
    render_all();
}

void framebuf_apply_settings(void) {
    font_apply_settings();
    memset(charbuf, 0, sizeof(charbuf));
    memset(colorbuf, 0, sizeof(colorbuf));
    for (int i = 0; i < 16; i++) {
        color_map[i] = i;
        color_map_inv[i] = i;
    }
    framebuf_set_screen_size(config_get_screen_cols(), config_get_screen_rows());
    scroll_delay = 0;
}

void framebuf_blink_task(void) {
    uint8_t period_cfg = config_get_screen_blink_period();
    if (period_cfg == 0)
        return;  /* blink disabled */

    if (!time_reached(blink_next_toggle))
        return;

    uint16_t period_ms = period_cfg * 16;
    if (period_ms < 100) period_ms = 500;
    blink_next_toggle = make_timeout_time_ms(period_ms / 2);
    blink_phase = !blink_phase;

    render_all();
}

void framebuf_init(void) {
    font_init();
    memset(charbuf, 0, sizeof(charbuf));
    memset(colorbuf, 0, sizeof(colorbuf));
    screen_inverted = false;
    blink_phase = false;
    blink_next_toggle = 0;

    display_init();
    framebuf_apply_settings();
}

void framebuf_write_string(uint8_t col, uint8_t row, const char *s, uint8_t fg, uint8_t bg, uint8_t attr) {
    while (*s && col < num_cols) {
        uint32_t idx = MKIDX(col, row);
        charbuf[idx] = (uint8_t)*s | ((uint16_t)attr << 8);
        colorbuf[idx] = (color_map[fg & 0x0F]) | (color_map[bg & 0x0F] << 4);
        render_cell(idx);
        s++;
        col++;
    }
}
