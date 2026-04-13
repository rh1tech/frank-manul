/*
 * Manul - WiFi Configuration UI
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Full-screen WiFi setup: scan for networks, select, enter password, save.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include "wifi_setup.h"
#include "framebuf.h"
#include "keyboard.h"
#include "browser_config.h"
#include "netcard.h"
#include "sound.h"

// defined in main.c
void run_tasks(bool processInput);

#define MAX_SCAN_RESULTS 20
#define PASS_MAX_LEN     64

typedef enum {
    WS_INACTIVE,
    WS_SCANNING,
    WS_LIST,
    WS_PASSWORD,
    WS_CONNECTING,
    WS_DONE,
} ws_state_t;

typedef struct {
    char ssid[33];
    int  rssi;
    int  enc;
    int  channel;
} ws_network_t;

static ws_state_t ws_state = WS_INACTIVE;
static ws_network_t networks[MAX_SCAN_RESULTS];
static int network_count = 0;
static int selected_network = 0;
static int scroll_offset = 0;

static char password[PASS_MAX_LEN + 1];
static int  pass_cursor = 0;
static bool pass_show = false;

/* Callback called by netcard during scan */
static nc_scan_cb_t scan_cb_active = NULL;

static void scan_result_cb(const char *ssid, int rssi, int enc, int ch) {
    if (network_count < MAX_SCAN_RESULTS) {
        strncpy(networks[network_count].ssid, ssid, 32);
        networks[network_count].ssid[32] = '\0';
        networks[network_count].rssi = rssi;
        networks[network_count].enc = enc;
        networks[network_count].channel = ch;
        network_count++;
    }
}

static void draw_header(void) {
    framebuf_fill_region(0, 0, 79, 0, ' ', 15, 1);
    framebuf_write_string(0, 0, " WiFi Setup", 15, 1, 0);
}

static void draw_status(const char *msg) {
    framebuf_fill_region(0, 29, 79, 29, ' ', 14, 0);
    framebuf_write_string(1, 29, msg, 14, 0, 0);
}

static void draw_network_list(void) {
    /* Clear content area */
    framebuf_fill_region(0, 1, 79, 28, ' ', 7, 0);

    framebuf_write_string(2, 2, "Available Networks:", 15, 0, 0);
    framebuf_write_string(2, 3, "-------------------", 8, 0, 0);

    int visible_rows = 20;
    for (int i = 0; i < visible_rows && (i + scroll_offset) < network_count; i++) {
        int ni = i + scroll_offset;
        uint8_t fg = (ni == selected_network) ? 0 : 7;
        uint8_t bg = (ni == selected_network) ? 11 : 0;

        framebuf_fill_region(2, 5 + i, 77, 5 + i, ' ', fg, bg);

        char line[76];
        const char *enc_str = "?";
        switch (networks[ni].enc) {
        case 2: enc_str = "WPA";  break;
        case 4: enc_str = "WPA2"; break;
        case 5: enc_str = "WEP";  break;
        case 7: enc_str = "Open"; break;
        case 8: enc_str = "Auto"; break;
        }
        snprintf(line, sizeof(line), "  %-32s  %4ddBm  %-4s  ch%d",
                 networks[ni].ssid, networks[ni].rssi, enc_str, networks[ni].channel);
        framebuf_write_string(2, 5 + i, line, fg, bg, 0);
    }

    if (network_count == 0) {
        framebuf_write_string(2, 5, "No networks found. Press R to rescan.", 7, 0, 0);
    }

    draw_status("Up/Down=Select  Enter=Connect  R=Rescan  Esc=Cancel");
}

static void draw_password_screen(void) {
    framebuf_fill_region(0, 1, 79, 28, ' ', 7, 0);

    char title[80];
    snprintf(title, sizeof(title), "Connect to: %s", networks[selected_network].ssid);
    framebuf_write_string(2, 4, title, 15, 0, 0);

    framebuf_write_string(2, 7, "Password:", 7, 0, 0);

    /* Draw password field */
    framebuf_fill_region(2, 8, 66, 8, ' ', 15, 8);
    if (pass_show) {
        framebuf_write_string(2, 8, password, 15, 8, 0);
    } else {
        char masked[PASS_MAX_LEN + 1];
        int len = strlen(password);
        for (int i = 0; i < len && i < PASS_MAX_LEN; i++)
            masked[i] = '*';
        masked[len] = '\0';
        framebuf_write_string(2, 8, masked, 15, 8, 0);
    }

    /* Cursor */
    if (pass_cursor < 65) {
        framebuf_set_char(2 + pass_cursor, 8, pass_show ? (pass_cursor < (int)strlen(password) ? password[pass_cursor] : ' ') : (pass_cursor < (int)strlen(password) ? '*' : ' '));
        framebuf_set_color(2 + pass_cursor, 8, 0, 15);
    }

    framebuf_write_string(2, 11, "Tab=Show/Hide password", 8, 0, 0);
    draw_status("Enter=Connect  Esc=Back");
}

static void do_scan(void) {
    ws_state = WS_SCANNING;
    draw_header();
    framebuf_fill_region(0, 1, 79, 28, ' ', 7, 0);
    framebuf_write_string(2, 14, "Scanning for networks...", 14, 0, 0);
    draw_status("Please wait...");

    network_count = 0;
    selected_network = 0;
    scroll_offset = 0;

    netcard_wifi_scan(scan_result_cb);

    ws_state = WS_LIST;
    draw_header();
    draw_network_list();
}

static void do_connect(void) {
    ws_state = WS_CONNECTING;
    draw_header();
    framebuf_fill_region(0, 1, 79, 28, ' ', 7, 0);

    char msg[80];
    snprintf(msg, sizeof(msg), "Connecting to %s...", networks[selected_network].ssid);
    framebuf_write_string(2, 14, msg, 14, 0, 0);
    draw_status("Please wait...");

    if (netcard_wifi_join(networks[selected_network].ssid, password)) {
        /* Save credentials */
        browser_config_set_wifi(networks[selected_network].ssid, password);

        sound_play_tone(1000, 100, 50, false);
        framebuf_write_string(2, 16, "Connected!", 10, 0, 0);
        draw_status("Press any key to continue...");
        ws_state = WS_DONE;
    } else {
        sound_play_tone(440, 200, 50, false);
        framebuf_write_string(2, 16, "Connection failed!", 12, 0, 0);
        draw_status("Press any key to go back...");
        ws_state = WS_DONE;
    }
}

void wifi_setup_enter(void) {
    ws_state = WS_SCANNING;
    password[0] = '\0';
    pass_cursor = 0;
    pass_show = false;
    do_scan();
}

bool wifi_setup_active(void) {
    return ws_state != WS_INACTIVE;
}

void wifi_setup_process_key(uint16_t key) {
    uint8_t ascii = key & 0xFF;  /* already mapped by main.c */

    switch (ws_state) {
    case WS_LIST:
        if (ascii == KEY_UP && selected_network > 0) {
            selected_network--;
            if (selected_network < scroll_offset) scroll_offset = selected_network;
            draw_network_list();
        } else if (ascii == KEY_DOWN && selected_network < network_count - 1) {
            selected_network++;
            if (selected_network >= scroll_offset + 20) scroll_offset = selected_network - 19;
            draw_network_list();
        } else if (ascii == KEY_ENTER && network_count > 0) {
            if (networks[selected_network].enc == 7) {
                /* Open network: connect without password */
                password[0] = '\0';
                do_connect();
            } else {
                /* Need password */
                password[0] = '\0';
                pass_cursor = 0;
                ws_state = WS_PASSWORD;
                draw_header();
                draw_password_screen();
            }
        } else if (ascii == 'r' || ascii == 'R') {
            do_scan();
        } else if (ascii == KEY_ESC) {
            ws_state = WS_INACTIVE;
        }
        break;

    case WS_PASSWORD:
        if (ascii == KEY_ENTER) {
            do_connect();
        } else if (ascii == KEY_ESC) {
            ws_state = WS_LIST;
            draw_header();
            draw_network_list();
        } else if (ascii == KEY_TAB) {
            pass_show = !pass_show;
            draw_password_screen();
        } else if (ascii == KEY_BACKSPACE) {
            if (pass_cursor > 0) {
                int len = strlen(password);
                memmove(password + pass_cursor - 1, password + pass_cursor, len - pass_cursor + 1);
                pass_cursor--;
                draw_password_screen();
            }
        } else if (ascii == KEY_LEFT && pass_cursor > 0) {
            pass_cursor--;
            draw_password_screen();
        } else if (ascii == KEY_RIGHT && pass_cursor < (int)strlen(password)) {
            pass_cursor++;
            draw_password_screen();
        } else if (ascii >= 32 && ascii < 127 && (int)strlen(password) < PASS_MAX_LEN) {
            int len = strlen(password);
            memmove(password + pass_cursor + 1, password + pass_cursor, len - pass_cursor + 1);
            password[pass_cursor] = ascii;
            pass_cursor++;
            draw_password_screen();
        }
        break;

    case WS_DONE:
        ws_state = WS_INACTIVE;
        break;

    default:
        break;
    }
}
