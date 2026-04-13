/*
 * Manul - Text Layout / Render Engine
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Renders HTML tokens into a grid of render_cell_t, following the Lynx
 * browser's text-mode rendering conventions:
 *
 *   - Normal text:  fg=7 (light gray), bg=0 (black)
 *   - Bold text:    fg=15 (white), RATTR_BOLD
 *   - Links:        bold text (fg=15), no special color -- just bold
 *   - Selected link: reverse video (caller applies RATTR_REVERSE)
 *   - Headings:     bold, paragraph breaks before/after
 *   - <i>/<em>:     bold (no italic in text mode, same as Lynx)
 *   - <u>:          underline attribute
 *   - <pre>:        whitespace preserved, tabs expand to 8-col stops
 *   - <hr>:         full-width line of '-' characters, fg=7
 *   - Lists:        bullet chars rotate by depth (* + o # @ - =)
 *   - Form elements: bracket notation
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "render.h"
#include "html.h"
#include "psram_allocator.h"
#include "hardware/xip_cache.h"

/* ── Color constants (Lynx defaults) ──────────────────────────────── */

#define FG_NORMAL   7       /* light gray */
#define FG_BOLD     15      /* white */
#define BG_DEFAULT  0       /* black */

/* Bullet characters for unordered lists, rotating by nesting depth */
static const char ul_bullets[] = { '*', '+', 'o', '#', '@', '-', '=' };
#define NUM_UL_BULLETS  ((int)(sizeof(ul_bullets) / sizeof(ul_bullets[0])))

/* ── Internal helpers (forward declarations) ──────────────────────── */

static int  strcasecmp_local(const char *a, const char *b);
static const char *find_attr(const html_token_t *tok, const char *name);

static void clear_lines(render_line_t *lines, uint16_t count);
static bool grow_page(render_page_t *page);
static render_line_t *current_line(render_ctx_t *ctx);

static uint8_t build_attr(const render_ctx_t *ctx);
static uint8_t pick_fg(const render_ctx_t *ctx);

static void emit_char(render_ctx_t *ctx, char ch);
static void emit_newline(render_ctx_t *ctx);
static void emit_string(render_ctx_t *ctx, const char *s);
static void ensure_new_line(render_ctx_t *ctx);
static void ensure_paragraph_break(render_ctx_t *ctx);
static void flush_pending(render_ctx_t *ctx);
static void emit_indent(render_ctx_t *ctx);
static void emit_hr(render_ctx_t *ctx);

static void process_text(render_ctx_t *ctx, const char *text, uint16_t len);
static void handle_tag_open(render_ctx_t *ctx, const html_token_t *tok);
static void handle_tag_close(render_ctx_t *ctx, const html_token_t *tok);

/* ── UTF-8 → Win1251 conversion ──────────────────────────────────── */

/*
 * Decode one UTF-8 character from *p, advance *p past it.
 * Returns the Unicode code point, or -1 on invalid sequence.
 */
static int32_t utf8_decode(const char **p, const char *end) {
    const uint8_t *s = (const uint8_t *)*p;
    if (s >= (const uint8_t *)end) return -1;

    uint8_t b0 = *s;
    if (b0 < 0x80) {
        (*p)++;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && s + 1 < (const uint8_t *)end) {
        int32_t cp = ((b0 & 0x1F) << 6) | (s[1] & 0x3F);
        *p += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && s + 2 < (const uint8_t *)end) {
        int32_t cp = ((b0 & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *p += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && s + 3 < (const uint8_t *)end) {
        int32_t cp = ((b0 & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
                     ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *p += 4;
        return cp;
    }
    /* Invalid: skip one byte */
    (*p)++;
    return -1;
}

/*
 * Map a Unicode code point to a Win1251 byte (0-255).
 * Returns the byte, or '?' if unmappable.
 */
static uint8_t unicode_to_win1251(int32_t cp) {
    /* ASCII pass-through */
    if (cp >= 0 && cp < 0x80)
        return (uint8_t)cp;

    /* Cyrillic block U+0400–U+04FF → Win1251 */
    if (cp >= 0x0410 && cp <= 0x042F) return (uint8_t)(cp - 0x0410 + 0xC0);  /* А-Я */
    if (cp >= 0x0430 && cp <= 0x044F) return (uint8_t)(cp - 0x0430 + 0xE0);  /* а-я */
    if (cp == 0x0401) return 0xA8;  /* Ё */
    if (cp == 0x0451) return 0xB8;  /* ё */
    if (cp == 0x0402) return 0x80;  /* Ђ */
    if (cp == 0x0403) return 0x81;  /* Ѓ */
    if (cp == 0x0404) return 0xAA;  /* Є */
    if (cp == 0x0405) return 0xBD;  /* Ѕ */
    if (cp == 0x0406) return 0xB2;  /* І */
    if (cp == 0x0407) return 0xAF;  /* Ї */
    if (cp == 0x0408) return 0xA3;  /* Ј */
    if (cp == 0x0409) return 0x8A;  /* Љ */
    if (cp == 0x040A) return 0x8C;  /* Њ */
    if (cp == 0x040B) return 0x8E;  /* Ћ */
    if (cp == 0x040C) return 0x8D;  /* Ќ */
    if (cp == 0x040E) return 0xA1;  /* Ў */
    if (cp == 0x040F) return 0x8F;  /* Џ */
    if (cp == 0x0452) return 0x90;  /* ђ */
    if (cp == 0x0453) return 0x83;  /* ѓ */
    if (cp == 0x0454) return 0xBA;  /* є */
    if (cp == 0x0455) return 0xBE;  /* ѕ */
    if (cp == 0x0456) return 0xB3;  /* і */
    if (cp == 0x0457) return 0xBF;  /* ї */
    if (cp == 0x0458) return 0xBC;  /* ј */
    if (cp == 0x0459) return 0x9A;  /* љ */
    if (cp == 0x045A) return 0x9C;  /* њ */
    if (cp == 0x045B) return 0x9E;  /* ћ */
    if (cp == 0x045C) return 0x9D;  /* ќ */
    if (cp == 0x045E) return 0xA2;  /* ў */
    if (cp == 0x045F) return 0x9F;  /* џ */

    /* Common symbols */
    if (cp == 0x2013) return '-';   /* en dash → hyphen */
    if (cp == 0x2014) return '-';   /* em dash → hyphen */
    if (cp == 0x2018 || cp == 0x2019) return '\''; /* smart quotes */
    if (cp == 0x201C || cp == 0x201D) return '"';   /* smart double quotes */
    if (cp == 0x2022) return '*';   /* bullet → asterisk */
    if (cp == 0x2026) return '.';   /* ellipsis → period */
    if (cp == 0x00A0) return ' ';   /* non-breaking space */
    if (cp == 0x00A9) return '(';   /* © → ( approx */
    if (cp == 0x00AB) return '<';   /* « */
    if (cp == 0x00BB) return '>';   /* » */
    if (cp == 0x2116) return 0xB9;  /* № Win1251 */

    return '?';
}

/* ── Page management ──────────────────────────────────────────────── */

void render_init(render_page_t *page) {
    /* Only allocate on first call — bump allocator can't free,
     * so we keep the buffers forever and reuse them. */
    if (page->lines == NULL) {
        page->capacity = RENDER_MAX_LINES;
        page->lines = (render_line_t *)malloc(
            page->capacity * sizeof(render_line_t));
        if (!page->lines) {
            page->capacity = 256;
            page->lines = (render_line_t *)malloc(page->capacity * sizeof(render_line_t));
        }
    }
    if (page->links == NULL) {
        page->links = (render_link_t *)malloc(RENDER_MAX_LINKS * sizeof(render_link_t));
        if (page->links)
            memset(page->links, 0, RENDER_MAX_LINKS * sizeof(render_link_t));
    }

    page->num_lines = 1;
    page->num_links = 0;
    page->title[0] = '\0';
    if (page->lines)
        clear_lines(&page->lines[0], 1);
}

void render_clear(render_page_t *page) {
    /* Keep the PSRAM buffers allocated — bump allocator can't free.
     * Just reset the counters so the page is reused. */
    page->num_lines = 1;
    page->num_links = 0;
    page->title[0] = '\0';
    if (page->lines)
        clear_lines(&page->lines[0], 1);
}

void render_ctx_init(render_ctx_t *ctx, render_page_t *page) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->page = page;
    ctx->current_link_id = -1;
    ctx->at_line_start = true;
}

/* ── Accessors ────────────────────────────────────────────────────── */

const render_line_t *render_get_line(const render_page_t *page, uint16_t idx) {
    if (idx >= page->num_lines)
        return NULL;
    return &page->lines[idx];
}

const render_link_t *render_get_link(const render_page_t *page, uint16_t idx) {
    if (idx >= page->num_links)
        return NULL;
    return &page->links[idx];
}

int16_t render_find_next_link(const render_page_t *page,
                              int16_t current, int direction) {
    if (page->num_links == 0)
        return -1;

    if (direction > 0) {
        int16_t next = current + 1;
        if (next < 0) next = 0;
        if (next < (int16_t)page->num_links)
            return next;
        return -1;
    } else {
        int16_t prev = current - 1;
        if (current < 0)
            prev = (int16_t)page->num_links - 1;
        if (prev >= 0 && prev < (int16_t)page->num_links)
            return prev;
        return -1;
    }
}

/* ── Internal helpers ─────────────────────────────────────────────── */

/* Case-insensitive string compare (ASCII only, no locale dependency) */
static int strcasecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Look up a token attribute by name, return pointer or NULL */
static const char *find_attr(const html_token_t *tok, const char *name) {
    if (strcasecmp_local(name, "href") == 0)
        return tok->attr_href[0] ? tok->attr_href : NULL;
    if (strcasecmp_local(name, "alt") == 0)
        return tok->attr_alt[0] ? tok->attr_alt : NULL;
    if (strcasecmp_local(name, "type") == 0)
        return tok->attr_type[0] ? tok->attr_type : NULL;
    if (strcasecmp_local(name, "name") == 0)
        return tok->attr_name[0] ? tok->attr_name : NULL;
    if (strcasecmp_local(name, "value") == 0)
        return tok->attr_value[0] ? tok->attr_value : NULL;
    if (strcasecmp_local(name, "action") == 0)
        return tok->attr_action[0] ? tok->attr_action : NULL;
    if (strcasecmp_local(name, "src") == 0)
        return tok->attr_src[0] ? tok->attr_src : NULL;
    return NULL;
}

static inline void flush_psram(void) {
    /* No-op: using SRAM — no cache flush needed */
}

/* Initialise a range of lines to default empty cells */
static void clear_lines(render_line_t *lines, uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        lines[i].len = 0;
        for (uint8_t c = 0; c < RENDER_MAX_COLS; c++) {
            lines[i].cells[c].ch = ' ';
            lines[i].cells[c].attr = RATTR_NORMAL;
            lines[i].cells[c].color = RCELL_COLOR(FG_NORMAL, BG_DEFAULT);
            lines[i].cells[c].link_id = -1;
        }
        /* cache flushed in bulk by render_flush() */
    }
}

/* Grow the line array if needed, up to RENDER_MAX_LINES */
static bool grow_page(render_page_t *page) {
    /* Buffer is pre-allocated to full capacity from PSRAM */
    return (page->lines != NULL && page->num_lines < page->capacity);
}

/* Get pointer to the current (last) line */
static render_line_t *current_line(render_ctx_t *ctx) {
    if (ctx->page->num_lines == 0) {
        ctx->page->num_lines = 1;
        grow_page(ctx->page);
    }
    return &ctx->page->lines[ctx->page->num_lines - 1];
}

/*
 * Build the attribute byte for the current formatting state.
 *
 * Lynx rules:
 *   - bold, <b>, <strong>, <i>, <em>, headings, links => RATTR_BOLD
 *   - <u> => RATTR_UNDERLINE
 *   - Links are just bold (no underline, no special LINK flag)
 */
static uint8_t build_attr(const render_ctx_t *ctx) {
    uint8_t a = RATTR_NORMAL;

    if (ctx->bold || ctx->heading_level)
        a |= RATTR_BOLD;
    if (ctx->underline || ctx->in_anchor)
        a |= RATTR_UNDERLINE;

    return a;
}

/*
 * Pick foreground color for current state.
 *
 * Color scheme:
 *   - fg=11 (cyan) for links
 *   - fg=15 (white) for bold text, headings
 *   - fg=7  (light gray) for everything else
 */
#define FG_LINK  11     /* cyan */

static uint8_t pick_fg(const render_ctx_t *ctx) {
    if (ctx->in_anchor)
        return FG_LINK;
    if (ctx->bold || ctx->heading_level)
        return FG_BOLD;
    return FG_NORMAL;
}

/* ── Emission helpers ─────────────────────────────────────────────── */

/* Start a new line */
static void emit_newline(render_ctx_t *ctx) {
    render_page_t *page = ctx->page;

    /* Flush the line we're leaving to PSRAM */
    if (page->num_lines > 0)
        /* cache flushed in bulk by render_flush() */

    if (!grow_page(page))
        return;

    page->num_lines++;
    clear_lines(&page->lines[page->num_lines - 1], 1);
    ctx->col = 0;
    ctx->last_was_space = false;
    ctx->at_line_start = true;
}

/* Emit a single character at the current position */
static void emit_char(render_ctx_t *ctx, char ch) {
    if (ctx->suppress_output)
        return;

    /* Flush any pending paragraph/br breaks before emitting content */
    flush_pending(ctx);

    /* Word wrap: if at column limit, break to new line */
    if (!ctx->preformatted && ctx->col >= RENDER_MAX_COLS) {
        /* Try to find a word break: scan backwards for a space */
        render_line_t *line = current_line(ctx);
        int wrap_col = -1;
        for (int i = (int)line->len - 1; i > (int)ctx->indent; i--) {
            if (line->cells[i].ch == ' ') {
                wrap_col = i;
                break;
            }
        }

        if (wrap_col > 0) {
            /* Move text after the space to a new line */
            emit_newline(ctx);

            /* Emit indent on wrapped line */
            for (uint8_t i = 0; i < ctx->indent; i++)
                emit_char(ctx, ' ');

            /* Copy the characters after the break point */
            render_line_t *prev = &ctx->page->lines[ctx->page->num_lines - 2];
            render_line_t *cur = current_line(ctx);
            for (int i = wrap_col + 1; i < (int)prev->len; i++) {
                if (ctx->col < RENDER_MAX_COLS) {
                    cur->cells[ctx->col] = prev->cells[i];
                    ctx->col++;
                    if (ctx->col > cur->len)
                        cur->len = ctx->col;
                }
            }
            /* Trim the previous line at the break point */
            prev->len = (uint8_t)wrap_col;
        } else {
            /* No space found; hard break at column limit */
            emit_newline(ctx);
            /* Skip leading space after hard break */
            if (ch == ' ')
                return;
        }
    }

    render_line_t *line = current_line(ctx);
    if (ctx->col >= RENDER_MAX_COLS)
        return; /* safety: don't write past end */

    render_cell_t *cell = &line->cells[ctx->col];
    cell->ch = ch;
    cell->attr = build_attr(ctx);
    cell->color = RCELL_COLOR(pick_fg(ctx), BG_DEFAULT);
    cell->link_id = ctx->in_anchor ? (int8_t)ctx->current_link_id : -1;

    ctx->col++;
    if (ctx->col > line->len)
        line->len = ctx->col;

    ctx->last_was_space = (ch == ' ');
    ctx->at_line_start = false;
}

/* Emit a null-terminated string */
static void emit_string(render_ctx_t *ctx, const char *s) {
    while (*s)
        emit_char(ctx, *s++);
}

/* Move to a new line if not already at column 0 */
static void ensure_new_line(render_ctx_t *ctx) {
    if (ctx->col > 0)
        emit_newline(ctx);
}

/* Ensure there is a blank line (paragraph break) */
static void ensure_paragraph_break(render_ctx_t *ctx) {
    ensure_new_line(ctx);

    /* Add a blank line if the last line is not already blank */
    if (ctx->page->num_lines >= 2) {
        render_line_t *prev = &ctx->page->lines[ctx->page->num_lines - 2];
        if (prev->len > 0)
            emit_newline(ctx);
    }
}

/* Flush pending paragraph breaks and line breaks before emitting text */
static void flush_pending(render_ctx_t *ctx) {
    if (ctx->pending_paragraph) {
        ctx->pending_paragraph = false;
        ensure_paragraph_break(ctx);
    }
    if (ctx->pending_br) {
        ctx->pending_br = false;
        ensure_new_line(ctx);
    }
}

/* Emit indentation spaces for the current indent level */
static void emit_indent(render_ctx_t *ctx) {
    for (uint8_t i = 0; i < ctx->indent && ctx->col < RENDER_MAX_COLS; i++)
        emit_char(ctx, ' ');
}

/* Emit a horizontal rule: full-width line of '-' chars, fg=7 */
static void emit_hr(render_ctx_t *ctx) {
    ensure_new_line(ctx);

    render_line_t *line = current_line(ctx);
    for (int i = 0; i < RENDER_MAX_COLS; i++) {
        line->cells[i].ch = '-';
        line->cells[i].attr = RATTR_NORMAL;
        line->cells[i].color = RCELL_COLOR(FG_NORMAL, BG_DEFAULT);
        line->cells[i].link_id = -1;
    }
    line->len = RENDER_MAX_COLS;
    ctx->col = RENDER_MAX_COLS;
    emit_newline(ctx);
}

/* ── Compute current effective indent ─────────────────────────────── */

/*
 * Recompute ctx->indent from the block nesting state.
 * Called when list depth or blockquote depth changes.
 *
 * Lynx indent rules:
 *   - <blockquote>: 2 chars per level
 *   - <ul>: 2 chars per level
 *   - <ol>: 3 chars per level
 */
static void recompute_indent(render_ctx_t *ctx) {
    uint8_t ind = 0;

    /* Blockquote indentation */
    ind += ctx->blockquote_depth * 2;

    /* List indentation: walk through each nesting level */
    for (uint8_t i = 0; i < ctx->list_depth && i < 8; i++) {
        if (ctx->ordered_list[i])
            ind += 3;
        else
            ind += 2;
    }

    ctx->indent = ind;
}

/* ── Process text content ─────────────────────────────────────────── */

static void process_text(render_ctx_t *ctx, const char *text, uint16_t len) {
    const char *p = text;
    const char *end = text + len;

    /* Capture title text — convert UTF-8 to Win1251 for display */
    if (ctx->in_title) {
        size_t cur = strlen(ctx->page->title);
        const char *tp = text;
        const char *te = text + len;
        while (tp < te && cur + 1 < sizeof(ctx->page->title)) {
            int32_t cp = utf8_decode(&tp, te);
            if (cp < 0) continue;
            ctx->page->title[cur++] = (char)unicode_to_win1251(cp);
        }
        ctx->page->title[cur] = '\0';
    }

    if (ctx->suppress_output)
        return;

    /* Preformatted mode: preserve all whitespace */
    if (ctx->preformatted) {
        while (p < end) {
            int32_t cp = utf8_decode(&p, end);
            if (cp < 0) continue;
            if (cp == '\n') {
                emit_newline(ctx);
            } else if (cp == '\t') {
                int stop = ((ctx->col / 8) + 1) * 8;
                while (ctx->col < stop && ctx->col < RENDER_MAX_COLS)
                    emit_char(ctx, ' ');
            } else if (cp == '\r') {
                /* skip */
            } else {
                emit_char(ctx, (char)unicode_to_win1251(cp));
            }
        }
        return;
    }

    /* Normal mode: collapse whitespace, word wrap, UTF-8 decode */
    while (p < end) {
        int32_t cp = utf8_decode(&p, end);
        if (cp < 0) continue;

        if (cp == '\n' || cp == '\r' || cp == '\t' || cp == ' ') {
            if (!ctx->last_was_space && !ctx->at_line_start)
                emit_char(ctx, ' ');
            ctx->last_was_space = true;
        } else {
            emit_char(ctx, (char)unicode_to_win1251(cp));
        }
    }
}

/* ── Handle opening tags ──────────────────────────────────────────── */

static void handle_tag_open(render_ctx_t *ctx, const html_token_t *tok) {
    const char *tag = tok->tag;

    /* ── Suppressed elements ────────────────────────────────────── */

    /* <head> - suppress body output */
    if (strcasecmp_local(tag, "head") == 0) {
        ctx->in_head = true;
        ctx->suppress_output = true;
        return;
    }

    /* <title> - capture text for title bar */
    if (strcasecmp_local(tag, "title") == 0) {
        ctx->in_title = true;
        ctx->page->title[0] = '\0';
        return;
    }

    /* <script>, <style> - suppress content */
    if (strcasecmp_local(tag, "script") == 0 ||
        strcasecmp_local(tag, "style") == 0) {
        ctx->suppress_output = true;
        return;
    }

    /* Skip everything else while suppressed */
    if (ctx->suppress_output)
        return;

    /* ── Block-level elements ───────────────────────────────────── */

    /* <p> */
    if (strcasecmp_local(tag, "p") == 0) {
        ctx->pending_paragraph = true;
        return;
    }

    /* <div> */
    if (strcasecmp_local(tag, "div") == 0) {
        ctx->pending_paragraph = true;
        return;
    }

    /* <br> - self-closing usually, but handle as open too */
    if (strcasecmp_local(tag, "br") == 0) {
        ctx->pending_br = true;
        return;
    }

    /* <hr> - full-width line of dashes */
    if (strcasecmp_local(tag, "hr") == 0) {
        emit_hr(ctx);
        return;
    }

    /* ── Headings <h1>..<h6> ────────────────────────────────────── */
    if ((tag[0] == 'h' || tag[0] == 'H') &&
        tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0') {
        ctx->heading_level = tag[1] - '0';
        ctx->bold = true;
        ctx->pending_paragraph = true;
        return;
    }

    /* ── Inline formatting ──────────────────────────────────────── */

    /* <a href="..."> - link: bold text, tracked in link table */
    if (strcasecmp_local(tag, "a") == 0) {
        const char *href = find_attr(tok, "href");
        ctx->in_anchor = true;

        if (href && ctx->page->num_links < RENDER_MAX_LINKS) {
            render_link_t *link = &ctx->page->links[ctx->page->num_links];
            strncpy(link->url, href, sizeof(link->url) - 1);
            link->url[sizeof(link->url) - 1] = '\0';
            link->start_line = ctx->page->num_lines - 1;
            link->start_col = ctx->col;
            ctx->current_link_id = (int16_t)ctx->page->num_links;
            ctx->page->num_links++;
        } else {
            ctx->current_link_id = -1;
        }
        return;
    }

    /* <b>, <strong> - bold */
    if (strcasecmp_local(tag, "b") == 0 ||
        strcasecmp_local(tag, "strong") == 0) {
        ctx->bold = true;
        return;
    }

    /* <i>, <em> - also bold in Lynx (no italic in text mode) */
    if (strcasecmp_local(tag, "i") == 0 ||
        strcasecmp_local(tag, "em") == 0) {
        ctx->bold = true;
        return;
    }

    /* <u> - underline */
    if (strcasecmp_local(tag, "u") == 0) {
        ctx->underline = true;
        return;
    }

    /* ── Preformatted ───────────────────────────────────────────── */

    if (strcasecmp_local(tag, "pre") == 0) {
        ctx->preformatted = true;
        ensure_new_line(ctx);
        return;
    }

    /* ── Lists ──────────────────────────────────────────────────── */

    /* <ul> - unordered list */
    if (strcasecmp_local(tag, "ul") == 0) {
        if (ctx->list_depth < 8) {
            ctx->ordered_list[ctx->list_depth] = false;
            ctx->list_counter[ctx->list_depth] = 0;
            ctx->list_depth++;
        }
        recompute_indent(ctx);
        ensure_new_line(ctx);
        return;
    }

    /* <ol> - ordered list */
    if (strcasecmp_local(tag, "ol") == 0) {
        if (ctx->list_depth < 8) {
            ctx->ordered_list[ctx->list_depth] = true;
            ctx->list_counter[ctx->list_depth] = 0;
            ctx->list_depth++;
        }
        recompute_indent(ctx);
        ensure_new_line(ctx);
        return;
    }

    /* <li> - list item */
    if (strcasecmp_local(tag, "li") == 0) {
        ensure_new_line(ctx);

        uint8_t depth = ctx->list_depth;
        if (depth == 0) {
            /* <li> outside a list -- treat as unordered depth 1 */
            emit_string(ctx, "* ");
            return;
        }

        uint8_t level = depth - 1;
        bool ordered = (level < 8) ? ctx->ordered_list[level] : false;

        /* Emit indentation (indent minus the bullet/number space) */
        /* For ul: indent is 2*depth total, bullet takes 2 chars */
        /* For ol: indent is 3*depth total, number takes ~3 chars */
        uint8_t base_indent = 0;
        for (uint8_t i = 0; i < level && i < 8; i++) {
            if (ctx->ordered_list[i])
                base_indent += 3;
            else
                base_indent += 2;
        }
        base_indent += ctx->blockquote_depth * 2;

        for (uint8_t i = 0; i < base_indent && ctx->col < RENDER_MAX_COLS; i++)
            emit_char(ctx, ' ');

        if (ordered) {
            if (level < 8)
                ctx->list_counter[level]++;
            uint16_t num = (level < 8) ? ctx->list_counter[level] : 1;
            char buf[12];
            snprintf(buf, sizeof(buf), "%u.", num);
            emit_string(ctx, buf);
        } else {
            /* Rotating bullet characters by depth */
            char bullet = ul_bullets[level % NUM_UL_BULLETS];
            emit_char(ctx, bullet);
            emit_char(ctx, ' ');
        }
        return;
    }

    /* ── Blockquote ─────────────────────────────────────────────── */

    if (strcasecmp_local(tag, "blockquote") == 0) {
        ctx->blockquote_depth++;
        recompute_indent(ctx);
        ctx->pending_paragraph = true;
        return;
    }

    /* ── Table support (simplified) ─────────────────────────────── */

    if (strcasecmp_local(tag, "table") == 0) {
        ctx->pending_paragraph = true;
        return;
    }
    if (strcasecmp_local(tag, "tr") == 0) {
        ensure_new_line(ctx);
        emit_indent(ctx);
        return;
    }
    if (strcasecmp_local(tag, "td") == 0 ||
        strcasecmp_local(tag, "th") == 0) {
        /* Separate cells with spaces */
        if (ctx->col > ctx->indent)
            emit_string(ctx, "  ");
        return;
    }

    /* ── Image ──────────────────────────────────────────────────── */

    if (strcasecmp_local(tag, "img") == 0) {
        const char *alt = find_attr(tok, "alt");
        emit_char(ctx, '[');
        if (alt && alt[0] != '\0')
            emit_string(ctx, alt);
        else
            emit_string(ctx, "IMAGE");
        emit_char(ctx, ']');
        return;
    }

    /* ── Form elements ──────────────────────────────────────────── */

    if (strcasecmp_local(tag, "form") == 0) {
        ctx->pending_paragraph = true;
        return;
    }

    /* <input> */
    if (strcasecmp_local(tag, "input") == 0) {
        const char *type = find_attr(tok, "type");
        const char *value = find_attr(tok, "value");

        if (!type || strcasecmp_local(type, "text") == 0 ||
            strcasecmp_local(type, "password") == 0) {
            /* Text field: [___________________] (20 chars default) */
            emit_char(ctx, '[');
            for (int i = 0; i < 20; i++)
                emit_char(ctx, '_');
            emit_char(ctx, ']');
        } else if (strcasecmp_local(type, "checkbox") == 0) {
            /* Checkbox: [_] */
            emit_string(ctx, "[_]");
        } else if (strcasecmp_local(type, "radio") == 0) {
            /* Radio: (o) */
            emit_string(ctx, "(o)");
        } else if (strcasecmp_local(type, "submit") == 0 ||
                   strcasecmp_local(type, "button") == 0) {
            /* Submit/button: [Submit] or [value] */
            emit_char(ctx, '[');
            emit_string(ctx, value ? value : "Submit");
            emit_char(ctx, ']');
        } else if (strcasecmp_local(type, "hidden") == 0) {
            /* Hidden inputs produce no output */
        } else {
            /* Other input types: show as text field */
            emit_char(ctx, '[');
            for (int i = 0; i < 20; i++)
                emit_char(ctx, '_');
            emit_char(ctx, ']');
        }
        return;
    }

    /* <select> - show first option text (will appear in text content) */
    if (strcasecmp_local(tag, "select") == 0) {
        emit_char(ctx, '[');
        return;
    }

    /* <textarea> */
    if (strcasecmp_local(tag, "textarea") == 0) {
        ensure_new_line(ctx);
        emit_indent(ctx);
        emit_char(ctx, '[');
        for (int i = 0; i < 30; i++)
            emit_char(ctx, '_');
        emit_char(ctx, ']');
        emit_newline(ctx);
        return;
    }

    /* <html>, <body>, <span>, and other unhandled tags: no special action */
}

/* ── Handle closing tags ──────────────────────────────────────────── */

static void handle_tag_close(render_ctx_t *ctx, const html_token_t *tok) {
    const char *tag = tok->tag;

    /* </head> */
    if (strcasecmp_local(tag, "head") == 0) {
        ctx->in_head = false;
        ctx->suppress_output = false;
        return;
    }

    /* </title> */
    if (strcasecmp_local(tag, "title") == 0) {
        ctx->in_title = false;
        return;
    }

    /* </script>, </style> */
    if (strcasecmp_local(tag, "script") == 0 ||
        strcasecmp_local(tag, "style") == 0) {
        /* Only unsuppress if not still in <head> */
        if (!ctx->in_head)
            ctx->suppress_output = false;
        return;
    }

    if (ctx->suppress_output)
        return;

    /* </p> */
    if (strcasecmp_local(tag, "p") == 0) {
        ctx->pending_paragraph = true;
        return;
    }

    /* </div> */
    if (strcasecmp_local(tag, "div") == 0) {
        ctx->pending_paragraph = true;
        return;
    }

    /* </h1>..</h6> */
    if ((tag[0] == 'h' || tag[0] == 'H') &&
        tag[1] >= '1' && tag[1] <= '6' && tag[2] == '\0') {
        ctx->heading_level = 0;
        ctx->bold = false;
        ctx->pending_paragraph = true;
        return;
    }

    /* </a> */
    if (strcasecmp_local(tag, "a") == 0) {
        if (ctx->current_link_id >= 0 &&
            ctx->current_link_id < (int16_t)ctx->page->num_links) {
            render_link_t *link = &ctx->page->links[ctx->current_link_id];
            link->end_line = ctx->page->num_lines - 1;
            link->end_col = ctx->col;
        }
        ctx->in_anchor = false;
        ctx->current_link_id = -1;
        /* Mark that a space is needed before the next inline content
         * so adjacent links don't run together */
        ctx->last_was_space = false;
        if (ctx->col > 0 && !ctx->at_line_start)
            emit_char(ctx, ' ');
        return;
    }

    /* </b>, </strong> */
    if (strcasecmp_local(tag, "b") == 0 ||
        strcasecmp_local(tag, "strong") == 0) {
        ctx->bold = false;
        return;
    }

    /* </i>, </em> - were rendered as bold */
    if (strcasecmp_local(tag, "i") == 0 ||
        strcasecmp_local(tag, "em") == 0) {
        ctx->bold = false;
        return;
    }

    /* </u> */
    if (strcasecmp_local(tag, "u") == 0) {
        ctx->underline = false;
        return;
    }

    /* </pre> */
    if (strcasecmp_local(tag, "pre") == 0) {
        ctx->preformatted = false;
        ensure_new_line(ctx);
        return;
    }

    /* </ul>, </ol> */
    if (strcasecmp_local(tag, "ul") == 0 ||
        strcasecmp_local(tag, "ol") == 0) {
        if (ctx->list_depth > 0)
            ctx->list_depth--;
        recompute_indent(ctx);
        ensure_new_line(ctx);
        return;
    }

    /* </blockquote> */
    if (strcasecmp_local(tag, "blockquote") == 0) {
        if (ctx->blockquote_depth > 0)
            ctx->blockquote_depth--;
        recompute_indent(ctx);
        ctx->pending_paragraph = true;
        return;
    }

    /* </table> */
    if (strcasecmp_local(tag, "table") == 0) {
        ctx->pending_paragraph = true;
        return;
    }

    /* </select> */
    if (strcasecmp_local(tag, "select") == 0) {
        emit_char(ctx, ']');
        return;
    }

    /* </form> */
    if (strcasecmp_local(tag, "form") == 0) {
        ctx->pending_paragraph = true;
        return;
    }
}

/* ── Main entry point ─────────────────────────────────────────────── */

void render_process_token(const void *token, void *user) {
    const html_token_t *tok = (const html_token_t *)token;
    render_ctx_t *ctx = (render_ctx_t *)user;

    switch (tok->type) {
    case HTML_TOKEN_TEXT:
        process_text(ctx, tok->text, tok->text_len);
        break;

    case HTML_TOKEN_TAG_OPEN:
        handle_tag_open(ctx, tok);
        break;

    case HTML_TOKEN_TAG_CLOSE:
        handle_tag_close(ctx, tok);
        break;

    case HTML_TOKEN_TAG_SELF_CLOSE:
        /* Self-closing tags are treated the same as open tags.
         * Elements like <br/>, <hr/>, <img/>, <input/> are handled
         * in handle_tag_open and don't need a close. */
        handle_tag_open(ctx, tok);
        break;

    default:
        /* Ignore comments, doctypes, etc. */
        break;
    }
}

void render_flush(render_ctx_t *ctx) {
    /* Flush all dirty cache lines to PSRAM */
    flush_psram();
}
