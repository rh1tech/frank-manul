/*
 * Manul - ESP-01 Netcard AT Command Client
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * AT command client for the frank-netcard ESP-01 firmware.
 * Communicates over PIO UART using serial.h.
 *
 * Protocol:
 *   Commands:  AT+CMD=args\r\n
 *   Responses: OK\r\n, ERROR:reason\r\n, +TAG:data\r\n
 *   Boot:      +READY\r\n
 *   Binary RX: +SRECV:id,len\r\n followed by len raw bytes
 *   Binary TX: AT+SSEND=id,len\r\n -> >\r\n -> len raw bytes -> SEND OK/FAIL\r\n
 *   Async:     +SCLOSED:id, +WDISCONN, +WCONN:ip
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"

#include "serial.h"
#include "netcard.h"

/* Event loop pump from main.c */
extern void run_tasks(bool processInput);

/* -------------------------------------------------------------------------- */
/* Constants                                                                  */
/* -------------------------------------------------------------------------- */

#define NC_MAX_SOCKETS      4
#define NC_LINE_BUF_SIZE    512
#define NC_SRECV_BUF_SIZE   1100    /* 1024 data + header overhead */

#define NC_TIMEOUT_DEFAULT  5000    /* ms */
#define NC_TIMEOUT_LONG     25000   /* ms — ESP firmware has 15-20s internal timeouts */
#define NC_TIMEOUT_TLS      35000   /* ms — TLS handshake on ESP-01 is slow */

/* -------------------------------------------------------------------------- */
/* Parser state machine                                                       */
/* -------------------------------------------------------------------------- */

typedef enum {
    NCS_READLINE,   /* accumulating a \r\n-terminated line */
    NCS_READDATA    /* reading exactly data_remaining raw bytes */
} nc_state_t;

/* -------------------------------------------------------------------------- */
/* Command response status                                                    */
/* -------------------------------------------------------------------------- */

typedef enum {
    NC_RESP_NONE,
    NC_RESP_OK,
    NC_RESP_ERROR,
    NC_RESP_SEND_OK,
    NC_RESP_SEND_FAIL,
    NC_RESP_PROMPT      /* received ">" for binary send */
} nc_resp_t;

/* -------------------------------------------------------------------------- */
/* Module state                                                               */
/* -------------------------------------------------------------------------- */

static nc_state_t   state;
static char          line_buf[NC_LINE_BUF_SIZE];
static uint16_t      line_pos;

/* Binary receive state (NCS_READDATA) */
static uint8_t       srecv_buf[NC_SRECV_BUF_SIZE];
static uint16_t      data_remaining;
static uint16_t      data_pos;
static uint8_t       data_socket_id;

/* Command serialisation */
static volatile nc_resp_t cmd_response;
static bool          cmd_pending;

/* WiFi state */
static bool          wifi_connected;
static char          wifi_ip[32];

/* Async callbacks */
static nc_data_cb_t  cb_data;
static nc_close_cb_t cb_close;
static nc_wifi_cb_t  cb_wifi;

/* Scan callback (transient, only valid during netcard_wifi_scan) */
static nc_scan_cb_t  cb_scan;
static int           scan_count;

/* Boot flag — set when +READY is received */
static volatile bool got_ready;

/* -------------------------------------------------------------------------- */
/* Low-level serial helpers                                                   */
/* -------------------------------------------------------------------------- */

static void nc_send_cmd(const char *cmd)
{
    serial_send_string(cmd);
    serial_send_string("\r\n");
}

/* -------------------------------------------------------------------------- */
/* Line dispatcher — called for every complete \r\n-terminated line           */
/* -------------------------------------------------------------------------- */

static void nc_process_line(const char *line, uint16_t len)
{
    /* ---- Final responses ---- */

    if (strcmp(line, "OK") == 0) {
        cmd_response = NC_RESP_OK;
        return;
    }

    if (strncmp(line, "ERROR:", 6) == 0) {
        printf("[NC] %s\n", line);
        cmd_response = NC_RESP_ERROR;
        return;
    }

    if (strcmp(line, "SEND OK") == 0) {
        cmd_response = NC_RESP_SEND_OK;
        return;
    }

    if (strcmp(line, "SEND FAIL") == 0) {
        cmd_response = NC_RESP_SEND_FAIL;
        return;
    }

    if (strcmp(line, ">") == 0) {
        cmd_response = NC_RESP_PROMPT;
        return;
    }

    /* ---- Unsolicited Result Codes (URCs) ---- */

    if (strcmp(line, "+READY") == 0) {
        got_ready = true;
        return;
    }

    /* +SRECV:id,len — switch to binary-read mode */
    if (strncmp(line, "+SRECV:", 7) == 0) {
        unsigned int id = 0, dlen = 0;
        if (sscanf(line + 7, "%u,%u", &id, &dlen) == 2 && id < NC_MAX_SOCKETS) {
            if (dlen > NC_SRECV_BUF_SIZE)
                dlen = NC_SRECV_BUF_SIZE;
            data_socket_id = (uint8_t)id;
            data_remaining = (uint16_t)dlen;
            data_pos = 0;
            state = NCS_READDATA;
        }
        return;
    }

    /* +SCLOSED:id — peer closed socket */
    if (strncmp(line, "+SCLOSED:", 9) == 0) {
        unsigned int id = 0;
        if (sscanf(line + 9, "%u", &id) == 1 && id < NC_MAX_SOCKETS) {
            if (cb_close)
                cb_close((uint8_t)id);
        }
        return;
    }

    /* +WCONN:ip — WiFi connected */
    if (strncmp(line, "+WCONN:", 7) == 0) {
        wifi_connected = true;
        strncpy(wifi_ip, line + 7, sizeof(wifi_ip) - 1);
        wifi_ip[sizeof(wifi_ip) - 1] = '\0';
        if (cb_wifi)
            cb_wifi(true, wifi_ip);
        return;
    }

    /* +WDISCONN — WiFi disconnected */
    if (strcmp(line, "+WDISCONN") == 0) {
        wifi_connected = false;
        wifi_ip[0] = '\0';
        if (cb_wifi)
            cb_wifi(false, NULL);
        return;
    }

    /* +WSCAN:ssid,rssi,enc,ch — scan result line */
    if (strncmp(line, "+WSCAN:", 7) == 0 && cb_scan) {
        /* Parse: +WSCAN:ssid,rssi,enc,ch */
        char ssid[64];
        int rssi = 0, enc = 0, ch = 0;
        const char *p = line + 7;

        /* Extract SSID (up to first comma) */
        const char *comma = strrchr(p, ',');  /* find last comma for ch */
        /* We need to parse from the right: ...,rssi,enc,ch */
        /* Find the three rightmost commas */
        const char *c3 = NULL, *c2 = NULL, *c1 = NULL;
        const char *scan = p;
        const char *commas[32];
        int ncommas = 0;
        while (*scan && ncommas < 32) {
            if (*scan == ',')
                commas[ncommas++] = scan;
            scan++;
        }
        if (ncommas >= 3) {
            c1 = commas[ncommas - 3];  /* before rssi */
            c2 = commas[ncommas - 2];  /* before enc */
            c3 = commas[ncommas - 1];  /* before ch */

            uint16_t ssid_len = (uint16_t)(c1 - p);
            if (ssid_len >= sizeof(ssid))
                ssid_len = sizeof(ssid) - 1;
            memcpy(ssid, p, ssid_len);
            ssid[ssid_len] = '\0';

            rssi = atoi(c1 + 1);
            enc  = atoi(c2 + 1);
            ch   = atoi(c3 + 1);

            cb_scan(ssid, rssi, enc, ch);
            scan_count++;
        }
        return;
    }

    /* Unknown lines are silently ignored */
}

/* -------------------------------------------------------------------------- */
/* netcard_poll — drain RX FIFO, feed through state machine                   */
/* -------------------------------------------------------------------------- */

void netcard_poll(void)
{
    while (serial_readable()) {
        uint8_t c = serial_read_byte();

        switch (state) {

        case NCS_READLINE:
            if (c == '\n') {
                /* Strip trailing \r if present */
                if (line_pos > 0 && line_buf[line_pos - 1] == '\r')
                    line_pos--;
                line_buf[line_pos] = '\0';

                if (line_pos > 0)
                    nc_process_line(line_buf, line_pos);

                line_pos = 0;
            } else {
                if (line_pos < NC_LINE_BUF_SIZE - 1)
                    line_buf[line_pos++] = (char)c;
                /* else: overflow — discard character, keep parsing */
            }
            break;

        case NCS_READDATA:
            srecv_buf[data_pos++] = c;
            data_remaining--;
            if (data_remaining == 0) {
                /* Deliver received data — no printf here, blocks USB
                 * and causes PIO UART FIFO overflow */
                if (cb_data)
                    cb_data(data_socket_id, srecv_buf, data_pos);
                state = NCS_READLINE;
                line_pos = 0;  /* reset line buffer for next line */
            }
            break;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Blocking helpers — wait for a specific command response                     */
/* -------------------------------------------------------------------------- */

static nc_resp_t nc_wait_response(uint32_t timeout_ms)
{
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

    while (cmd_response == NC_RESP_NONE) {
        if (get_absolute_time() >= deadline)
            return NC_RESP_NONE;   /* timeout */

        /* Drain UART in a tight loop — do NOT call run_tasks() here
         * because framebuf_blink_task() takes too long and causes
         * PIO UART FIFO overflows (only 4-8 byte deep). */
        netcard_poll();
        tight_loop_contents();
    }

    nc_resp_t r = cmd_response;
    cmd_response = NC_RESP_NONE;
    return r;
}

static bool nc_send_and_wait(const char *cmd, uint32_t timeout_ms)
{
    /* Drain any pending RX data before sending a new command */
    netcard_poll();

    cmd_response = NC_RESP_NONE;
    cmd_pending = true;

    printf("[NC] >> %s\n", cmd);
    nc_send_cmd(cmd);
    nc_resp_t r = nc_wait_response(timeout_ms);

    cmd_pending = false;
    if (r != NC_RESP_OK)
        printf("[NC] << %s\n", r == NC_RESP_ERROR ? "ERROR" :
                                r == NC_RESP_NONE ? "TIMEOUT" : "OTHER");
    return (r == NC_RESP_OK);
}

/* -------------------------------------------------------------------------- */
/* Public API — Initialisation                                                */
/* -------------------------------------------------------------------------- */

void netcard_init(void)
{
    /* Reset parser state */
    state = NCS_READLINE;
    line_pos = 0;
    data_remaining = 0;
    cmd_response = NC_RESP_NONE;
    cmd_pending = false;
    wifi_connected = false;
    wifi_ip[0] = '\0';
    cb_data = NULL;
    cb_close = NULL;
    cb_wifi = NULL;
    cb_scan = NULL;
    got_ready = false;

    /* Drain any stale data, then verify modem with AT */
    absolute_time_t settle = make_timeout_time_ms(500);
    while (get_absolute_time() < settle)
        netcard_poll();

    for (int attempt = 0; attempt < 5; attempt++) {
        if (nc_send_and_wait("AT", NC_TIMEOUT_DEFAULT))
            return;

        absolute_time_t retry_delay = make_timeout_time_ms(200);
        while (get_absolute_time() < retry_delay)
            netcard_poll();
    }
}

/* -------------------------------------------------------------------------- */
/* Public API — Polling                                                       */
/* -------------------------------------------------------------------------- */

/* netcard_poll is defined above in the parser section */

/* -------------------------------------------------------------------------- */
/* Public API — WiFi                                                          */
/* -------------------------------------------------------------------------- */

bool netcard_wifi_join(const char *ssid, const char *pass)
{
    char cmd[NC_LINE_BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+WJOIN=%s,%s", ssid, pass);
    return nc_send_and_wait(cmd, NC_TIMEOUT_LONG);
}

void netcard_wifi_quit(void)
{
    nc_send_and_wait("AT+WQUIT", NC_TIMEOUT_DEFAULT);
    wifi_connected = false;
    wifi_ip[0] = '\0';
}

bool netcard_wifi_connected(void)
{
    return wifi_connected;
}

/* -------------------------------------------------------------------------- */
/* Public API — WiFi Scan                                                     */
/* -------------------------------------------------------------------------- */

int netcard_wifi_scan(nc_scan_cb_t cb)
{
    cb_scan = cb;
    scan_count = 0;

    bool ok = nc_send_and_wait("AT+WSCAN", NC_TIMEOUT_LONG);

    cb_scan = NULL;

    if (!ok)
        return -1;

    return scan_count;
}

/* -------------------------------------------------------------------------- */
/* Public API — Sockets                                                       */
/* -------------------------------------------------------------------------- */

bool netcard_socket_open(uint8_t id, bool tls, const char *host, uint16_t port)
{
    if (id >= NC_MAX_SOCKETS)
        return false;

    char cmd[NC_LINE_BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "AT+SOPEN=%u,%s,%s,%u",
             (unsigned)id, tls ? "TLS" : "TCP", host, (unsigned)port);

    uint32_t timeout = tls ? NC_TIMEOUT_TLS : NC_TIMEOUT_LONG;

    /* Retry up to 3 times — WiFi may briefly drop during reconnect */
    for (int attempt = 0; attempt < 3; attempt++) {
        if (nc_send_and_wait(cmd, timeout))
            return true;

        printf("[NC] SOPEN failed, retry %d/3...\n", attempt + 1);
        /* Brief delay for WiFi to stabilize */
        absolute_time_t wait = make_timeout_time_ms(1000);
        while (get_absolute_time() < wait)
            netcard_poll();
    }
    return false;
}

bool netcard_socket_send(uint8_t id, const uint8_t *data, uint16_t len)
{
    if (id >= NC_MAX_SOCKETS || len == 0)
        return false;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+SSEND=%u,%u", (unsigned)id, (unsigned)len);

    /* Send command and wait for ">" prompt */
    cmd_response = NC_RESP_NONE;
    cmd_pending = true;

    nc_send_cmd(cmd);
    nc_resp_t r = nc_wait_response(NC_TIMEOUT_DEFAULT);

    if (r != NC_RESP_PROMPT) {
        cmd_pending = false;
        return false;
    }

    /* Send raw binary data */
    serial_send_data(data, len);

    /* Wait for SEND OK / SEND FAIL */
    cmd_response = NC_RESP_NONE;
    r = nc_wait_response(NC_TIMEOUT_DEFAULT);

    cmd_pending = false;
    return (r == NC_RESP_SEND_OK);
}

void netcard_socket_close(uint8_t id)
{
    if (id >= NC_MAX_SOCKETS)
        return;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "AT+SCLOSE=%u", (unsigned)id);
    nc_send_and_wait(cmd, NC_TIMEOUT_DEFAULT);
}

/* -------------------------------------------------------------------------- */
/* Public API — Async callbacks                                               */
/* -------------------------------------------------------------------------- */

void netcard_set_data_callback(nc_data_cb_t cb)
{
    cb_data = cb;
}

void netcard_set_close_callback(nc_close_cb_t cb)
{
    cb_close = cb;
}

void netcard_set_wifi_callback(nc_wifi_cb_t cb)
{
    cb_wifi = cb;
}
