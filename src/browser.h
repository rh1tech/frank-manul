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

#ifndef BROWSER_H
#define BROWSER_H

#include <stdint.h>
#include <stdbool.h>

#include "render.h"
#include "html.h"

/* ---------- Screen geometry ---------- */
#define BROWSER_COLS            80
#define BROWSER_ROWS            30
#define BROWSER_TITLE_ROW       0
#define BROWSER_CONTENT_ROW0    1
#define BROWSER_CONTENT_ROWS    27      /* rows 1 .. 27 */
#define BROWSER_STATUS_ROW      28
#define BROWSER_URL_ROW         29

/* ---------- History ---------- */
#define BROWSER_HISTORY_SIZE    16

/* ---------- Mode ---------- */
typedef enum {
    BMODE_BROWSING,     /* viewing page, scrolling, link selection     */
    BMODE_URL_INPUT,    /* typing URL in the URL bar                   */
    BMODE_LOADING,      /* fetching page (HTTP in progress)            */
} browser_mode_t;

/* ---------- History entry ---------- */
typedef struct {
    char     url[512];
    uint16_t scroll_pos;
    int16_t  selected_link;
} browser_history_entry_t;

/* ---------- Browser state ---------- */
typedef struct {
    browser_mode_t  mode;

    /* Current page */
    render_page_t   page;
    render_ctx_t    render_ctx;
    html_parser_t   html_parser;
    char            current_url[512];
    uint16_t        scroll_pos;
    int16_t         selected_link;      /* -1 = none */

    /* URL input */
    char            url_input[512];
    uint16_t        url_cursor;
    uint16_t        url_scroll;         /* horizontal scroll for long URLs */

    /* History (circular buffer) */
    browser_history_entry_t history[BROWSER_HISTORY_SIZE];
    uint8_t         history_pos;
    uint8_t         history_count;

    /* Status */
    char            status_msg[80];
    bool            wifi_connected;
} browser_state_t;

/* ---------- Public API ---------- */

/* Initialize browser: clear screen, draw chrome, auto-connect WiFi if
   credentials are saved, show welcome page.  Call once after all HW init. */
void browser_init(void);

/* Process a single key from the keyboard.  Called from main loop. */
void browser_process_key(uint16_t key);

/* Navigate to a URL: push history, start HTTP fetch, render result. */
void browser_navigate(const char *url);

/* Go back one page in history. */
void browser_go_back(void);

/* Set the status bar message (row 28). */
void browser_set_status(const char *msg);

/* Redraw the entire browser UI (title bar, content, status, URL bar). */
void browser_redraw(void);

#endif /* BROWSER_H */
