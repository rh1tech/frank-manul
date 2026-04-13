/*
 * Manul - Text Layout / Render Engine
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Renders HTML to a text cell grid matching the Lynx browser's approach:
 *   - Links are bold (not colored differently)
 *   - Selected link uses reverse video
 *   - Headings are bold with paragraph breaks
 *   - <i>/<em> rendered as bold (no italic in text mode)
 *   - Bullet chars rotate by nesting depth
 *   - Word wrap at 80 columns
 */

#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>
#include <stdbool.h>

/* ── Constants ─────────────────────────────────────────────────────── */

#define RENDER_MAX_COLS     80
#define RENDER_MAX_LINKS    256
#define RENDER_MAX_LINES    480     /* SRAM — PSRAM blocked by XIP cache issue */

/* Cell attributes (matches framebuf ATTR_* where possible) */
#define RATTR_NORMAL    0x00
#define RATTR_UNDERLINE 0x01    /* matches ATTR_UNDERLINE in framebuf.h */
#define RATTR_BOLD      0x04    /* matches ATTR_BOLD in framebuf.h */
#define RATTR_REVERSE   0x08    /* matches ATTR_INVERSE in framebuf.h */

/* ── Data structures ───────────────────────────────────────────────── */

typedef struct {
    char    ch;
    uint8_t attr;       /* RATTR_* flags */
    uint8_t color;      /* fg:4 | bg:4 packed (low nibble = fg, high = bg) */
    int8_t  link_id;    /* -1 if no link, 0..126 */
} render_cell_t;        /* 4 bytes per cell */

/* Accessors for packed colour */
#define RCELL_FG(c)  ((c).color & 0x0F)
#define RCELL_BG(c)  (((c).color >> 4) & 0x0F)
#define RCELL_COLOR(fg, bg)  (((bg) << 4) | ((fg) & 0x0F))

typedef struct {
    render_cell_t cells[RENDER_MAX_COLS];
    uint8_t len;        /* number of used cells in this line */
    uint8_t _pad[3];    /* align to 4 bytes for PSRAM XIP access */
} render_line_t;        /* 324 bytes per line, 4-byte aligned */

typedef struct {
    char     url[128];
    uint16_t start_line;
    uint8_t  start_col;
    uint16_t end_line;
    uint8_t  end_col;
} render_link_t;

typedef struct {
    render_line_t *lines;       /* dynamically allocated array */
    uint16_t num_lines;
    uint16_t capacity;
    render_link_t *links;       /* dynamically allocated array */
    uint16_t num_links;
    char title[128];
} render_page_t;

/* Render context (formatting state machine) */
typedef struct {
    render_page_t *page;
    uint8_t col;                /* current column position */
    uint8_t indent;             /* current indentation (chars) */

    /* Formatting state */
    bool bold;
    bool underline;
    bool preformatted;
    bool in_anchor;
    int16_t current_link_id;

    /* Block state */
    uint8_t heading_level;      /* 0 = none, 1-6 = <h1>..<h6> */
    uint8_t list_depth;
    bool ordered_list[8];       /* per nesting level */
    uint16_t list_counter[8];   /* per nesting level */
    uint8_t blockquote_depth;

    /* Whitespace state */
    bool last_was_space;
    bool at_line_start;
    bool pending_paragraph;     /* need blank line before next text */
    bool pending_br;

    /* Head/title handling */
    bool in_head;
    bool in_title;
    bool suppress_output;       /* true while inside <head>, <script>, <style> */
} render_ctx_t;

/* ── API ───────────────────────────────────────────────────────────── */

/* Initialise / free page buffer */
void render_init(render_page_t *page);
void render_clear(render_page_t *page);

/* Initialise render context (call after render_init on the page) */
void render_ctx_init(render_ctx_t *ctx, render_page_t *page);

/*
 * Process a single HTML token.
 * Designed to be used as html_token_cb_t:
 *   void (*html_token_cb_t)(const html_token_t *token, void *user_data);
 * Pass a render_ctx_t* as user_data.
 */
void render_process_token(const void *token, void *ctx);

/* Flush the current line to PSRAM (call after last token processed) */
void render_flush(render_ctx_t *ctx);

/* Read-only access to rendered lines */
const render_line_t *render_get_line(const render_page_t *page, uint16_t idx);

/* Read-only access to link table */
const render_link_t *render_get_link(const render_page_t *page, uint16_t idx);

/*
 * Navigate between links.
 * direction > 0: next link, direction < 0: previous link.
 * current = -1 to start from the beginning (or end).
 * Returns link index or -1 if none found.
 */
int16_t render_find_next_link(const render_page_t *page,
                              int16_t current, int direction);

#endif /* RENDER_H */
