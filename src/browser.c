/*
 * Manul - Browser UI and Navigation
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Main browser UI: manages screen layout (80x30), user input,
 * navigation history, and coordinates HTTP fetching with HTML
 * rendering.  Lynx-inspired keyboard-driven interface.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "browser.h"

#include <string.h>
#include <stdio.h>

#include "framebuf.h"
#include "keyboard.h"
#include "browser_config.h"
#include "netcard.h"
#include "http.h"
#include "html.h"
#include "render.h"
#include "url.h"
#include "wifi_setup.h"
#include "sound.h"

/* ------------------------------------------------------------------ */
/*  Colour constants                                                  */
/* ------------------------------------------------------------------ */
#define TITLE_FG    15  /* white  */
#define TITLE_BG     1  /* blue   */
#define STATUS_FG   14  /* yellow */
#define STATUS_BG    0  /* black  */
#define URL_FG      15  /* white  */
#define URL_BG       8  /* dark gray */
#define CONTENT_FG   7  /* light gray */
#define CONTENT_BG   0  /* black  */
/* Link colors are now computed by the render engine (Lynx style:
 * bold text for links, reverse video for selected link). */

/* ------------------------------------------------------------------ */
/*  Ctrl-key helper: Ctrl+L = 0x0c                                    */
/* ------------------------------------------------------------------ */
#define CTRL_L  0x0c

/* ------------------------------------------------------------------ */
/*  Singleton browser state                                           */
/* ------------------------------------------------------------------ */
static browser_state_t bs;

/* ------------------------------------------------------------------ */
/*  Forward declarations (internal helpers)                            */
/* ------------------------------------------------------------------ */
static void draw_title_bar(void);
static void draw_status_bar(void);
static void draw_url_bar(void);
static void draw_content(void);
static void clear_content(void);

static void handle_browsing_key(uint16_t key);
static void handle_url_input_key(uint16_t key);
static void handle_loading_key(uint16_t key);

static void history_push(void);
static void show_welcome_page(void);
static void select_link(int16_t idx);
static void scroll_to_link(int16_t idx);
static void follow_selected_link(void);

static void browser_on_body_chunk(const uint8_t *data, uint16_t len, void *ctx);
static void browser_on_done(void *ctx);
static void wifi_status_callback(bool connected, const char *ip);

/* ------------------------------------------------------------------ */
/*  Draw helpers                                                      */
/* ------------------------------------------------------------------ */

/* Fill a single row with spaces in a given colour. */
static void fill_row(uint8_t row, uint8_t fg, uint8_t bg)
{
    framebuf_fill_region(0, row, BROWSER_COLS - 1, row, ' ', fg, bg);
}

/* Write a string truncated to the row width. */
static void write_row_string(uint8_t col, uint8_t row,
                             const char *s, uint8_t fg, uint8_t bg,
                             uint8_t attr)
{
    framebuf_write_string(col, row, s, fg, bg, attr);
}

/* ------------------------------------------------------------------ */
/*  Title bar  (row 0)                                                */
/* ------------------------------------------------------------------ */
static void draw_title_bar(void)
{
    char buf[BROWSER_COLS + 1];
    const char *title = "";

    fill_row(BROWSER_TITLE_ROW, TITLE_FG, TITLE_BG);

    if (bs.page.title[0] != '\0')
        title = bs.page.title;

    if (title[0] != '\0')
        snprintf(buf, sizeof(buf), "Manul | %s", title);
    else
        snprintf(buf, sizeof(buf), "Manul");

    /* Truncate to screen width */
    buf[BROWSER_COLS] = '\0';

    write_row_string(0, BROWSER_TITLE_ROW, buf, TITLE_FG, TITLE_BG, 0);
}

/* ------------------------------------------------------------------ */
/*  Status bar (row 28)                                               */
/* ------------------------------------------------------------------ */
static void draw_status_bar(void)
{
    fill_row(BROWSER_STATUS_ROW, STATUS_FG, STATUS_BG);
    write_row_string(0, BROWSER_STATUS_ROW, bs.status_msg,
                     STATUS_FG, STATUS_BG, 0);
}

/* ------------------------------------------------------------------ */
/*  URL bar (row 29)                                                  */
/* ------------------------------------------------------------------ */
static void draw_url_bar(void)
{
    char buf[BROWSER_COLS + 1];
    const char *url;
    uint16_t url_len, visible_chars, offset;

    fill_row(BROWSER_URL_ROW, URL_FG, URL_BG);

    if (bs.mode == BMODE_URL_INPUT)
        url = bs.url_input;
    else
        url = bs.current_url;

    /* "URL: " prefix = 5 characters, leaving 75 for the URL */
    write_row_string(0, BROWSER_URL_ROW, "URL: ", URL_FG, URL_BG, 0);

    url_len = (uint16_t)strlen(url);
    visible_chars = BROWSER_COLS - 5;

    if (bs.mode == BMODE_URL_INPUT) {
        offset = bs.url_scroll;
    } else {
        offset = 0;
    }

    /* Copy visible portion */
    uint16_t copy_len = url_len > offset ? url_len - offset : 0;
    if (copy_len > visible_chars)
        copy_len = visible_chars;

    memset(buf, 0, sizeof(buf));
    if (copy_len > 0)
        memcpy(buf, url + offset, copy_len);
    buf[copy_len] = '\0';

    write_row_string(5, BROWSER_URL_ROW, buf, URL_FG, URL_BG, 0);

    /* Draw cursor when editing */
    if (bs.mode == BMODE_URL_INPUT) {
        uint8_t cursor_col = 5 + (uint8_t)(bs.url_cursor - offset);
        if (cursor_col < BROWSER_COLS) {
            char ch = (bs.url_cursor < url_len) ? url[bs.url_cursor] : ' ';
            char cursor_str[2] = { ch, '\0' };
            write_row_string(cursor_col, BROWSER_URL_ROW, cursor_str,
                             URL_BG, URL_FG, 0);  /* inverse */
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Content area (rows 1..27)                                         */
/* ------------------------------------------------------------------ */
static void clear_content(void)
{
    framebuf_fill_region(0, BROWSER_CONTENT_ROW0,
                         BROWSER_COLS - 1,
                         BROWSER_CONTENT_ROW0 + BROWSER_CONTENT_ROWS - 1,
                         ' ', CONTENT_FG, CONTENT_BG);
}

static void draw_content(void)
{
    uint16_t total_lines = bs.page.num_lines;

    for (uint8_t row = 0; row < BROWSER_CONTENT_ROWS; row++) {
        uint16_t line_idx = bs.scroll_pos + row;
        uint8_t screen_row = BROWSER_CONTENT_ROW0 + row;

        if (line_idx >= total_lines) {
            fill_row(screen_row, CONTENT_FG, CONTENT_BG);
            continue;
        }

        const render_line_t *line = &bs.page.lines[line_idx];

        for (uint8_t col = 0; col < BROWSER_COLS; col++) {
            const render_cell_t *cell = &line->cells[col];
            uint8_t fg = RCELL_FG(*cell);
            uint8_t bg = RCELL_BG(*cell);
            uint8_t attr = cell->attr;

            /* Highlight selected link with reverse video (Lynx style) */
            if (cell->link_id >= 0 &&
                cell->link_id == bs.selected_link) {
                uint8_t tmp = fg;
                fg = bg;
                bg = tmp;
                attr &= ~RATTR_UNDERLINE;
            }

            framebuf_set_char(col, screen_row, cell->ch);
            framebuf_set_color(col, screen_row, fg, bg);
            framebuf_set_attr(col, screen_row, attr);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Full redraw                                                       */
/* ------------------------------------------------------------------ */
void browser_redraw(void)
{
    draw_title_bar();
    draw_content();
    draw_status_bar();
    draw_url_bar();
}

/* ------------------------------------------------------------------ */
/*  Status message                                                    */
/* ------------------------------------------------------------------ */
void browser_set_status(const char *msg)
{
    strncpy(bs.status_msg, msg, sizeof(bs.status_msg) - 1);
    bs.status_msg[sizeof(bs.status_msg) - 1] = '\0';
    draw_status_bar();
}

/* ------------------------------------------------------------------ */
/*  History                                                           */
/* ------------------------------------------------------------------ */
static void history_push(void)
{
    /* Don't push empty URLs */
    if (bs.current_url[0] == '\0')
        return;

    browser_history_entry_t *entry =
        &bs.history[bs.history_pos % BROWSER_HISTORY_SIZE];

    strncpy(entry->url, bs.current_url, sizeof(entry->url) - 1);
    entry->url[sizeof(entry->url) - 1] = '\0';
    entry->scroll_pos    = bs.scroll_pos;
    entry->selected_link = bs.selected_link;

    bs.history_pos = (bs.history_pos + 1) % BROWSER_HISTORY_SIZE;
    if (bs.history_count < BROWSER_HISTORY_SIZE)
        bs.history_count++;
}

void browser_go_back(void)
{
    if (bs.history_count == 0) {
        browser_set_status("No history");
        sound_play_tone(220, 100, 30, false);
        return;
    }

    /* Step back one entry */
    if (bs.history_pos == 0)
        bs.history_pos = BROWSER_HISTORY_SIZE - 1;
    else
        bs.history_pos--;
    bs.history_count--;

    browser_history_entry_t *entry =
        &bs.history[bs.history_pos % BROWSER_HISTORY_SIZE];

    /* Navigate without pushing history again */
    strncpy(bs.current_url, entry->url, sizeof(bs.current_url) - 1);
    bs.current_url[sizeof(bs.current_url) - 1] = '\0';

    uint16_t saved_scroll = entry->scroll_pos;
    int16_t  saved_link   = entry->selected_link;

    /* Start loading */
    bs.mode = BMODE_LOADING;
    render_clear(&bs.page);
    render_init(&bs.page);
    render_ctx_init(&bs.render_ctx, &bs.page);
    html_parser_init(&bs.html_parser);
    bs.scroll_pos     = 0;
    bs.selected_link  = -1;

    browser_set_status("Loading...");
    clear_content();
    draw_url_bar();

    http_get(bs.current_url, browser_on_body_chunk, browser_on_done, NULL);

    /* Restore scroll/link after load completes (handled in on_done) */
    /* We store them temporarily in the history entry - on_done will pick up */
    entry->scroll_pos    = saved_scroll;
    entry->selected_link = saved_link;
}

/* ------------------------------------------------------------------ */
/*  Navigation                                                        */
/* ------------------------------------------------------------------ */
void browser_navigate(const char *url)
{
    /* Push current page to history */
    history_push();

    /* Store the new URL */
    strncpy(bs.current_url, url, sizeof(bs.current_url) - 1);
    bs.current_url[sizeof(bs.current_url) - 1] = '\0';

    /* Reset page state */
    bs.mode          = BMODE_LOADING;
    bs.scroll_pos    = 0;
    bs.selected_link = -1;

    render_clear(&bs.page);
    render_init(&bs.page);
    render_ctx_init(&bs.render_ctx, &bs.page);
    html_parser_init(&bs.html_parser);

    browser_set_status("Loading...");
    clear_content();
    draw_title_bar();
    draw_url_bar();

    /* Start async HTTP fetch */
    http_get(bs.current_url, browser_on_body_chunk, browser_on_done, NULL);
}

/* ------------------------------------------------------------------ */
/*  HTTP callbacks                                                    */
/* ------------------------------------------------------------------ */
static void browser_on_body_chunk(const uint8_t *data, uint16_t len, void *ctx)
{
    (void)ctx;
    /* Feed raw bytes to the HTML parser, which emits tokens to the
       render engine via render_process_token callback. */
    html_parser_feed(&bs.html_parser, data, len,
                     (html_token_cb_t)render_process_token, &bs.render_ctx);

    /* Do NOT redraw here — screen updates are slow and cause UART
     * FIFO overflow.  The screen will be drawn on completion. */
}

static void browser_on_done(void *ctx)
{
    (void)ctx;
    bs.mode = BMODE_BROWSING;

    const http_response_t *resp = http_get_response();
    int status_code = resp ? resp->status_code : 0;

    if (status_code >= 200 && status_code < 400) {
        /* Flush any remaining tokens from the parser */
        html_parser_finish(&bs.html_parser,
                           (html_token_cb_t)render_process_token, &bs.render_ctx);
        render_flush(&bs.render_ctx);

        /* Auto-select first link */
        if (bs.page.num_links > 0)
            select_link(0);

        char msg[80];
        snprintf(msg, sizeof(msg), "Done (%d) - %u lines",
                 status_code, bs.page.num_lines);
        browser_set_status(msg);
    } else if (status_code == 0) {
        browser_set_status("Error: connection failed");
    } else {
        char msg[80];
        snprintf(msg, sizeof(msg), "HTTP error %d", status_code);
        browser_set_status(msg);
    }

    browser_redraw();
}

/* ------------------------------------------------------------------ */
/*  WiFi callback                                                     */
/* ------------------------------------------------------------------ */
static void wifi_status_callback(bool connected, const char *ip)
{
    bs.wifi_connected = connected;
    if (connected) {
        char msg[80];
        snprintf(msg, sizeof(msg), "WiFi connected: %s", ip);
        browser_set_status(msg);
    } else {
        browser_set_status("WiFi disconnected");
    }
}

/* ------------------------------------------------------------------ */
/*  Welcome page                                                      */
/* ------------------------------------------------------------------ */
static void show_welcome_page(void)
{
    render_clear(&bs.page);
    render_init(&bs.page);
    render_ctx_init(&bs.render_ctx, &bs.page);

    /* Build a simple welcome page via the render engine */
    static const char *welcome_html =
        "<h1>Welcome to Manul</h1>"
        "<p>A text web browser for the RP2350.</p>"
        "<p></p>"
        "<p><b>Navigation:</b></p>"
        "<p>  Ctrl+L   - Enter a URL</p>"
        "<p>  Enter    - Follow selected link</p>"
        "<p>  Tab      - Next link</p>"
        "<p>  Bksp     - Go back</p>"
        "<p>  Up/Down  - Scroll</p>"
        "<p>  PgUp/Dn  - Scroll one page</p>"
        "<p>  Home/End - Top / bottom</p>"
        "<p>  F5       - Reload</p>"
        "<p></p>"
        "<p><b>Setup:</b></p>"
        "<p>  F1 - Help</p>"
        "<p>  F2 - WiFi setup</p>"
        "<p></p>"
        "<p>Press <b>Ctrl+L</b> to enter a URL and start browsing.</p>";

    html_parser_init(&bs.html_parser);
    html_parser_feed(&bs.html_parser, (const uint8_t *)welcome_html, strlen(welcome_html),
                     (html_token_cb_t)render_process_token, &bs.render_ctx);
    html_parser_finish(&bs.html_parser,
                       (html_token_cb_t)render_process_token, &bs.render_ctx);
    render_flush(&bs.render_ctx);

    strncpy(bs.page.title, "Welcome", sizeof(bs.page.title) - 1);
    bs.page.title[sizeof(bs.page.title) - 1] = '\0';

    bs.current_url[0] = '\0';
    bs.scroll_pos      = 0;
    bs.selected_link   = -1;

    if (bs.page.num_links > 0)
        select_link(0);

    browser_redraw();
}

/* ------------------------------------------------------------------ */
/*  Help page                                                         */
/* ------------------------------------------------------------------ */
static void show_help_page(void)
{
    render_clear(&bs.page);
    render_init(&bs.page);
    render_ctx_init(&bs.render_ctx, &bs.page);

    static const char *help_html =
        "<h1>Manul Help</h1>"
        "<p></p>"
        "<p><b>Browsing Keys:</b></p>"
        "<p>  Up / Down     Scroll one line</p>"
        "<p>  PgUp / PgDn   Scroll one page</p>"
        "<p>  Home / End    Jump to top / bottom</p>"
        "<p>  Tab           Select next link</p>"
        "<p>  Shift+Tab     Select previous link</p>"
        "<p>  Enter         Follow selected link</p>"
        "<p>  Backspace     Go back in history</p>"
        "<p>  F5            Reload current page</p>"
        "<p></p>"
        "<p><b>URL Bar:</b></p>"
        "<p>  Ctrl+L        Focus URL bar</p>"
        "<p>  Enter         Navigate to URL</p>"
        "<p>  Escape        Cancel / return to page</p>"
        "<p></p>"
        "<p><b>Other:</b></p>"
        "<p>  F1            This help page</p>"
        "<p>  F2            WiFi setup</p>"
        "<p></p>"
        "<p>Manul is a text web browser for the RP2350 M2</p>"
        "<p>platform.  It uses an ESP-01 WiFi module running the</p>"
        "<p>frank-netcard AT command firmware.</p>";

    html_parser_init(&bs.html_parser);
    html_parser_feed(&bs.html_parser, (const uint8_t *)help_html, strlen(help_html),
                     (html_token_cb_t)render_process_token, &bs.render_ctx);
    html_parser_finish(&bs.html_parser,
                       (html_token_cb_t)render_process_token, &bs.render_ctx);
    render_flush(&bs.render_ctx);

    strncpy(bs.page.title, "Help", sizeof(bs.page.title) - 1);
    bs.page.title[sizeof(bs.page.title) - 1] = '\0';

    bs.current_url[0] = '\0';
    bs.scroll_pos      = 0;
    bs.selected_link   = -1;

    browser_redraw();
}

/* ------------------------------------------------------------------ */
/*  Link selection                                                    */
/* ------------------------------------------------------------------ */
static void select_link(int16_t idx)
{
    if (bs.page.num_links == 0) {
        bs.selected_link = -1;
        return;
    }

    /* Clamp index */
    if (idx < 0)
        idx = 0;
    if (idx >= bs.page.num_links)
        idx = bs.page.num_links - 1;

    bs.selected_link = idx;

    /* Show link URL in status bar */
    if (idx >= 0 && idx < bs.page.num_links) {
        const render_link_t *link = &bs.page.links[idx];
        char msg[80];
        snprintf(msg, sizeof(msg), "-> %s", link->url);
        browser_set_status(msg);
    }

    /* Scroll viewport to make the selected link visible */
    scroll_to_link(idx);
}

static void scroll_to_link(int16_t idx)
{
    if (idx < 0 || idx >= bs.page.num_links)
        return;

    const render_link_t *link = &bs.page.links[idx];
    uint16_t link_row = link->start_line;

    if (link_row < bs.scroll_pos) {
        bs.scroll_pos = link_row;
    } else if (link_row >= bs.scroll_pos + BROWSER_CONTENT_ROWS) {
        bs.scroll_pos = link_row - BROWSER_CONTENT_ROWS + 1;
    }
}

static void follow_selected_link(void)
{
    if (bs.selected_link < 0 || bs.selected_link >= bs.page.num_links) {
        browser_set_status("No link selected");
        return;
    }

    const render_link_t *link = &bs.page.links[bs.selected_link];

    /* Resolve relative URLs */
    if (bs.current_url[0] != '\0') {
        url_t base, resolved;
        char resolved_str[512];

        if (url_parse(bs.current_url, &base)) {
            url_resolve(&base, link->url, &resolved);
            url_to_string(&resolved, resolved_str, sizeof(resolved_str));
            browser_navigate(resolved_str);
            return;
        }
    }

    /* If no base URL or parse failed, use href directly */
    browser_navigate(link->url);
}

/* ------------------------------------------------------------------ */
/*  Scroll helpers                                                    */
/* ------------------------------------------------------------------ */
static void scroll_clamp(void)
{
    uint16_t max_scroll = 0;
    if (bs.page.num_lines > BROWSER_CONTENT_ROWS)
        max_scroll = bs.page.num_lines - BROWSER_CONTENT_ROWS;

    if (bs.scroll_pos > max_scroll)
        bs.scroll_pos = max_scroll;
}

/* ------------------------------------------------------------------ */
/*  Key handling: BMODE_BROWSING                                      */
/* ------------------------------------------------------------------ */
static void handle_browsing_key(uint16_t key)
{
    switch (key & 0xFF) {
    /* ---- Scrolling ---- */
    case KEY_UP:
        if (bs.scroll_pos > 0) {
            bs.scroll_pos--;
            draw_content();
        }
        break;

    case KEY_DOWN:
        if (bs.page.num_lines > BROWSER_CONTENT_ROWS &&
            bs.scroll_pos < bs.page.num_lines - BROWSER_CONTENT_ROWS) {
            bs.scroll_pos++;
            draw_content();
        }
        break;

    case KEY_PUP:
        if (bs.scroll_pos >= BROWSER_CONTENT_ROWS)
            bs.scroll_pos -= BROWSER_CONTENT_ROWS;
        else
            bs.scroll_pos = 0;
        draw_content();
        break;

    case KEY_PDOWN:
        bs.scroll_pos += BROWSER_CONTENT_ROWS;
        scroll_clamp();
        draw_content();
        break;

    case KEY_HOME:
        bs.scroll_pos = 0;
        draw_content();
        break;

    case KEY_END:
        if (bs.page.num_lines > BROWSER_CONTENT_ROWS)
            bs.scroll_pos = bs.page.num_lines - BROWSER_CONTENT_ROWS;
        else
            bs.scroll_pos = 0;
        draw_content();
        break;

    /* ---- Link navigation ---- */
    case KEY_TAB:
        if (bs.page.num_links > 0) {
            if (keyboard_shift_pressed(key)) {
                /* Shift+Tab: previous link, wrap to last */
                int16_t prev = bs.selected_link - 1;
                if (prev < 0) prev = bs.page.num_links - 1;
                select_link(prev);
            } else {
                /* Tab: next link, wrap to first */
                int16_t next = bs.selected_link + 1;
                if (next >= bs.page.num_links) next = 0;
                select_link(next);
            }
            draw_content();
        }
        break;

    case KEY_ENTER:
        follow_selected_link();
        break;

    /* ---- History ---- */
    case KEY_BACKSPACE:
        browser_go_back();
        break;

    /* ---- URL bar ---- */
    default:
        if ((key & 0xFF) == CTRL_L) {
            /* Ctrl+L: enter URL input mode */
            bs.mode = BMODE_URL_INPUT;
            strncpy(bs.url_input, bs.current_url,
                    sizeof(bs.url_input) - 1);
            bs.url_input[sizeof(bs.url_input) - 1] = '\0';
            bs.url_cursor = (uint16_t)strlen(bs.url_input);
            bs.url_scroll = 0;

            /* Adjust scroll if cursor is off-screen */
            uint16_t visible = BROWSER_COLS - 5;
            if (bs.url_cursor > visible)
                bs.url_scroll = bs.url_cursor - visible + 1;

            browser_set_status("Enter URL and press Enter");
            draw_url_bar();
            return;
        }

        /* Function keys */
        if (key == KEY_F1) {
            show_help_page();
        } else if (key == KEY_F2) {
            wifi_setup_enter();
        } else if (key == KEY_F5) {
            /* Reload */
            if (bs.current_url[0] != '\0')
                browser_navigate(bs.current_url);
            else
                browser_set_status("Nothing to reload");
        }
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Key handling: BMODE_URL_INPUT                                     */
/* ------------------------------------------------------------------ */
static void handle_url_input_key(uint16_t key)
{
    uint16_t len = (uint16_t)strlen(bs.url_input);
    uint16_t visible = BROWSER_COLS - 5;
    uint8_t ch = key & 0xFF;

    switch (ch) {
    case KEY_ESC:
        /* Cancel URL input, return to browsing */
        bs.mode = BMODE_BROWSING;
        browser_set_status("Cancelled");
        draw_url_bar();
        return;

    case KEY_ENTER:
        /* Navigate to the entered URL */
        if (bs.url_input[0] != '\0') {
            bs.mode = BMODE_BROWSING;

            /* Add http:// if no scheme */
            if (strncmp(bs.url_input, "http://", 7) != 0 &&
                strncmp(bs.url_input, "https://", 8) != 0) {
                char tmp[512];
                snprintf(tmp, sizeof(tmp), "http://%s", bs.url_input);
                strncpy(bs.url_input, tmp, sizeof(bs.url_input) - 1);
                bs.url_input[sizeof(bs.url_input) - 1] = '\0';
            }

            browser_navigate(bs.url_input);
        } else {
            browser_set_status("No URL entered");
        }
        return;

    case KEY_BACKSPACE:
        if (bs.url_cursor > 0 && len > 0) {
            memmove(&bs.url_input[bs.url_cursor - 1],
                    &bs.url_input[bs.url_cursor],
                    len - bs.url_cursor + 1);
            bs.url_cursor--;
        }
        break;

    case KEY_DELETE:
        if (bs.url_cursor < len) {
            memmove(&bs.url_input[bs.url_cursor],
                    &bs.url_input[bs.url_cursor + 1],
                    len - bs.url_cursor);
        }
        break;

    case KEY_LEFT:
        if (bs.url_cursor > 0)
            bs.url_cursor--;
        break;

    case KEY_RIGHT:
        if (bs.url_cursor < len)
            bs.url_cursor++;
        break;

    case KEY_HOME:
        bs.url_cursor = 0;
        break;

    case KEY_END:
        bs.url_cursor = len;
        break;

    default:
        /* Printable ASCII: insert character */
        if (ch >= 0x20 && ch < 0x7f && len < sizeof(bs.url_input) - 1) {
            memmove(&bs.url_input[bs.url_cursor + 1],
                    &bs.url_input[bs.url_cursor],
                    len - bs.url_cursor + 1);
            bs.url_input[bs.url_cursor] = (char)ch;
            bs.url_cursor++;
        }
        break;
    }

    /* Adjust horizontal scroll so cursor stays visible */
    if (bs.url_cursor < bs.url_scroll)
        bs.url_scroll = bs.url_cursor;
    else if (bs.url_cursor >= bs.url_scroll + visible)
        bs.url_scroll = bs.url_cursor - visible + 1;

    draw_url_bar();
}

/* ------------------------------------------------------------------ */
/*  Key handling: BMODE_LOADING                                       */
/* ------------------------------------------------------------------ */
static void handle_loading_key(uint16_t key)
{
    if ((key & 0xFF) == KEY_ESC) {
        /* Cancel loading */
        http_abort();
        bs.mode = BMODE_BROWSING;
        browser_set_status("Cancelled");
        browser_redraw();
    }
    /* All other keys are ignored while loading */
}

/* ------------------------------------------------------------------ */
/*  Public: process key                                               */
/* ------------------------------------------------------------------ */
void browser_process_key(uint16_t key)
{
    /* WiFi setup takes priority when active */
    if (wifi_setup_active()) {
        wifi_setup_process_key(key);
        if (!wifi_setup_active()) {
            /* WiFi setup finished: redraw browser */
            browser_redraw();
        }
        return;
    }

    switch (bs.mode) {
    case BMODE_BROWSING:
        handle_browsing_key(key);
        break;
    case BMODE_URL_INPUT:
        handle_url_input_key(key);
        break;
    case BMODE_LOADING:
        handle_loading_key(key);
        break;
    }
}

/* ------------------------------------------------------------------ */
/*  Public: init                                                      */
/* ------------------------------------------------------------------ */
void browser_init(void)
{
    memset(&bs, 0, sizeof(bs));
    bs.mode          = BMODE_BROWSING;
    bs.selected_link = -1;

    render_init(&bs.page);

    /* Register WiFi status callback */
    netcard_set_wifi_callback(wifi_status_callback);

    /* Auto-connect WiFi if credentials are saved */
    if (browser_config_has_wifi()) {
        const browser_config_t *cfg = browser_config_get();
        browser_set_status("Connecting to WiFi...");
        draw_status_bar();
        netcard_wifi_join(cfg->wifi_ssid, cfg->wifi_pass);
    } else {
        browser_set_status("Press F2 to configure WiFi");
    }

    /* Show the welcome page */
    show_welcome_page();

    /* If a homepage is configured and WiFi is available, navigate to it */
    const browser_config_t *cfg = browser_config_get();
    if (cfg->homepage[0] != '\0' && browser_config_has_wifi()) {
        browser_navigate(cfg->homepage);
    }
}
