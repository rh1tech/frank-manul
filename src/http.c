/*
 * Manul - HTTP/1.0 Client
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Streaming HTTP/1.0 client over netcard sockets.
 * Uses socket 0 for all HTTP connections.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "http.h"
#include "netcard.h"
#include "url.h"

/* Socket used for HTTP connections */
#define HTTP_SOCKET_ID  0

/* Maximum number of redirect hops */
#define HTTP_MAX_REDIRECTS  5

/* Receive buffer for header accumulation */
#define HTTP_RECV_BUF_SIZE  2048

/* Connection timeout (ms) */
#define HTTP_CONNECT_TIMEOUT_MS  10000

/* User-Agent string */
#define HTTP_USER_AGENT  "Manul/1.0 (RP2350)"

extern void run_tasks(bool processInput);

/* ---- Internal state ---- */

static http_state_t     state;
static http_response_t  response;
static url_t            current_url;

static http_body_cb_t   body_cb;
static http_done_cb_t   done_cb;
static void            *user_ctx;

static uint8_t  recv_buf[HTTP_RECV_BUF_SIZE];
static uint16_t recv_len;

static int      redirect_count;

/* Deferred redirect — set inside data callback, executed from main loop */
static bool     redirect_pending;
static url_t    redirect_url;

/* Chunked transfer encoding state */
static enum {
    CHUNK_SIZE_LINE,    /* reading chunk size hex line */
    CHUNK_DATA,         /* reading chunk data */
    CHUNK_DATA_CRLF,    /* consuming CRLF after chunk data */
    CHUNK_DONE,         /* final zero chunk received */
} chunk_state;
static int32_t  chunk_remaining;

/* Saved previous netcard callbacks so we can restore them */
static nc_data_cb_t  prev_data_cb;
static nc_close_cb_t prev_close_cb;

/* ---- Forward declarations ---- */

static void http_on_data(uint8_t socket_id, const uint8_t *data, uint16_t len);
static void http_on_close(uint8_t socket_id);
static bool http_start_request(void);
static void http_finish(http_state_t end_state);
static void http_parse_status_line(const char *line);
static void http_parse_header_line(const char *line);
static void http_process_headers(void);
static void http_deliver_body(const uint8_t *data, uint16_t len);
static void http_deliver_body_chunked(const uint8_t *data, uint16_t len);
static bool http_is_redirect(uint16_t code);
static int  strcasecmp_local(const char *a, const char *b);
static int  strncasecmp_local(const char *a, const char *b, size_t n);

/* ---- Case-insensitive string comparison ---- */

static int strcasecmp_local(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static int strncasecmp_local(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!a[i] && !b[i]) return 0;
        if (!a[i]) return -1;
        if (!b[i]) return 1;
        char ca = (a[i] >= 'A' && a[i] <= 'Z') ? a[i] + 32 : a[i];
        char cb = (b[i] >= 'A' && b[i] <= 'Z') ? b[i] + 32 : b[i];
        if (ca != cb)
            return ca - cb;
    }
    return 0;
}

/* ---- Public API ---- */

void http_init(void) {
    state = HTTP_STATE_IDLE;
    memset(&response, 0, sizeof(response));
    recv_len = 0;
    redirect_count = 0;
    redirect_pending = false;
    body_cb = NULL;
    done_cb = NULL;
    user_ctx = NULL;
}

void http_poll(void) {
    if (!redirect_pending)
        return;
    redirect_pending = false;

    /* Close the current socket (safe here — not inside netcard_poll) */
    netcard_socket_close(HTTP_SOCKET_ID);

    /* Follow the redirect */
    memcpy(&current_url, &redirect_url, sizeof(url_t));
    if (!http_start_request()) {
        http_finish(HTTP_STATE_ERROR);
    }
}

bool http_get(const char *url_str, http_body_cb_t bcb, http_done_cb_t dcb, void *ctx) {
    if (state != HTTP_STATE_IDLE && state != HTTP_STATE_DONE &&
        state != HTTP_STATE_ERROR)
        return false;

    if (!url_parse(url_str, &current_url))
        return false;

    body_cb = bcb;
    done_cb = dcb;
    user_ctx = ctx;
    redirect_count = 0;

    return http_start_request();
}

http_state_t http_get_state(void) {
    return state;
}

const http_response_t *http_get_response(void) {
    return &response;
}

void http_abort(void) {
    if (state != HTTP_STATE_IDLE && state != HTTP_STATE_DONE &&
        state != HTTP_STATE_ERROR) {
        netcard_socket_close(HTTP_SOCKET_ID);
        http_finish(HTTP_STATE_ERROR);
    }
}

/* ---- Internal implementation ---- */

static bool http_start_request(void) {
    /* Reset response and receive buffer */
    memset(&response, 0, sizeof(response));
    response.content_length = -1;
    recv_len = 0;
    chunk_state = CHUNK_SIZE_LINE;
    chunk_remaining = 0;

    /* Install our callbacks */
    netcard_set_data_callback(http_on_data);
    netcard_set_close_callback(http_on_close);

    /* Open socket: TLS for HTTPS, plain TCP for HTTP */
    bool tls = (strcmp(current_url.scheme, "https") == 0);
    state = HTTP_STATE_CONNECTING;

    printf("[HTTP] %s %s://%s:%u%s\n",
           tls ? "HTTPS" : "HTTP",
           current_url.scheme, current_url.host,
           current_url.port, current_url.path);

    if (!netcard_socket_open(HTTP_SOCKET_ID, tls,
                             current_url.host, current_url.port)) {
        printf("[HTTP] socket_open failed\n");
        http_finish(HTTP_STATE_ERROR);
        return false;
    }

    /* AT+SOPEN blocks until TCP/TLS handshake completes.
     * If it returned OK the socket is already connected. */
    printf("[HTTP] connected, sending request\n");

    /* Build and send GET request */
    state = HTTP_STATE_SENDING;

    char request[768];
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Connection: close\r\n"
        "Accept: text/html, text/plain, */*\r\n"
        "\r\n",
        current_url.path,
        current_url.host,
        HTTP_USER_AGENT);

    if (req_len < 0 || req_len >= (int)sizeof(request)) {
        printf("[HTTP] request too long\n");
        netcard_socket_close(HTTP_SOCKET_ID);
        http_finish(HTTP_STATE_ERROR);
        return false;
    }

    if (!netcard_socket_send(HTTP_SOCKET_ID,
                             (const uint8_t *)request, (uint16_t)req_len)) {
        printf("[HTTP] send failed\n");
        netcard_socket_close(HTTP_SOCKET_ID);
        http_finish(HTTP_STATE_ERROR);
        return false;
    }

    state = HTTP_STATE_RECV_STATUS;
    return true;
}

static void http_finish(http_state_t end_state) {
    state = end_state;
    if (done_cb)
        done_cb(user_ctx);
}

static bool http_is_redirect(uint16_t code) {
    return code == 301 || code == 302 || code == 307 || code == 308;
}

/*
 * Netcard data callback: receives raw TCP data from socket 0.
 * Routes to header accumulation or body delivery depending on state.
 */
static void http_on_data(uint8_t socket_id, const uint8_t *data, uint16_t len) {
    if (socket_id != HTTP_SOCKET_ID)
        return;

    if (state == HTTP_STATE_CONNECTING) {
        /* First data means connection succeeded */
        state = HTTP_STATE_RECV_STATUS;
    }

    if (state == HTTP_STATE_RECV_STATUS || state == HTTP_STATE_RECV_HEADERS) {
        /* Accumulate into recv_buf for header parsing */
        uint16_t space = HTTP_RECV_BUF_SIZE - recv_len;
        uint16_t copy = (len < space) ? len : space;
        if (copy > 0) {
            memcpy(recv_buf + recv_len, data, copy);
            recv_len += copy;
        }

        /* Look for end of headers: \r\n\r\n */
        char *hdr_end = NULL;
        for (uint16_t i = 3; i < recv_len; i++) {
            if (recv_buf[i-3] == '\r' && recv_buf[i-2] == '\n' &&
                recv_buf[i-1] == '\r' && recv_buf[i]   == '\n') {
                hdr_end = (char *)&recv_buf[i+1];
                break;
            }
        }

        if (!hdr_end)
            return;  /* Need more data */

        /* Null-terminate headers for string parsing */
        uint16_t hdr_len = (uint16_t)(hdr_end - (char *)recv_buf);
        /* Place null terminator at end of header block (overwrite first body byte
         * temporarily — we saved the data and will deliver it below) */
        uint8_t saved_byte = 0;
        if (hdr_len < recv_len)
            saved_byte = recv_buf[hdr_len];
        recv_buf[hdr_len] = '\0';

        /* Parse status line and headers */
        http_process_headers();

        /* Restore byte */
        if (hdr_len < recv_len)
            recv_buf[hdr_len] = saved_byte;

        /* Handle redirects — defer to avoid recursive netcard_poll.
         * The actual close/reopen happens in http_poll(). */
        if (http_is_redirect(response.status_code) && response.location[0]) {
            redirect_count++;
            if (redirect_count > HTTP_MAX_REDIRECTS) {
                printf("[HTTP] too many redirects\n");
                state = HTTP_STATE_ERROR;
                return;
            }

            printf("[HTTP] redirect %d -> %s\n", response.status_code,
                   response.location);

            url_resolve(&current_url, response.location, &redirect_url);
            redirect_pending = true;
            return;
        }

        state = HTTP_STATE_RECV_BODY;

        /* Deliver any body data already in the buffer */
        uint16_t body_in_buf = recv_len - hdr_len;
        if (body_in_buf > 0) {
            http_deliver_body(recv_buf + hdr_len, body_in_buf);
        }

        /* Also deliver the overflow data that didn't fit in recv_buf */
        if (copy < len) {
            http_deliver_body(data + copy, len - copy);
        }
    } else if (state == HTTP_STATE_RECV_BODY) {
        /* Direct body delivery */
        http_deliver_body(data, len);
    }
}

/*
 * Netcard close callback: server closed the connection.
 * For HTTP/1.0 with Connection: close, this signals end of response.
 */
static void http_on_close(uint8_t socket_id) {
    if (socket_id != HTTP_SOCKET_ID)
        return;

    if (state == HTTP_STATE_CONNECTING) {
        /* Connection failed */
        printf("[HTTP] connection refused\n");
        http_finish(HTTP_STATE_ERROR);
    } else if (state == HTTP_STATE_RECV_BODY ||
               state == HTTP_STATE_RECV_STATUS ||
               state == HTTP_STATE_RECV_HEADERS) {
        /* Server closed connection — transfer complete */
        http_finish(HTTP_STATE_DONE);
    }
}

/*
 * Parse the accumulated header block.
 * Format: "HTTP/1.x NNN reason\r\nHeader: Value\r\n..."
 */
static void http_process_headers(void) {
    char *p = (char *)recv_buf;

    /* Parse status line: find first \r\n */
    char *line_end = strstr(p, "\r\n");
    if (!line_end) {
        state = HTTP_STATE_ERROR;
        return;
    }
    *line_end = '\0';
    http_parse_status_line(p);
    p = line_end + 2;

    state = HTTP_STATE_RECV_HEADERS;

    /* Parse each header line */
    while (*p && !(p[0] == '\r' && p[1] == '\n')) {
        line_end = strstr(p, "\r\n");
        if (!line_end)
            break;
        *line_end = '\0';
        http_parse_header_line(p);
        p = line_end + 2;
    }
}

/*
 * Parse "HTTP/1.x NNN reasonphrase"
 */
static void http_parse_status_line(const char *line) {
    /* Skip "HTTP/1.x " */
    const char *p = line;
    while (*p && *p != ' ')
        p++;
    while (*p == ' ')
        p++;

    /* Parse status code */
    response.status_code = 0;
    while (*p >= '0' && *p <= '9') {
        response.status_code = response.status_code * 10 + (*p - '0');
        p++;
    }

    printf("[HTTP] status %u\n", response.status_code);
}

/*
 * Parse a single "Header-Name: value" line (case-insensitive).
 */
static void http_parse_header_line(const char *line) {
    const char *colon = strchr(line, ':');
    if (!colon)
        return;

    size_t name_len = colon - line;

    /* Skip colon and leading whitespace in value */
    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t')
        value++;

    if (name_len == 14 && strncasecmp_local(line, "content-length", 14) == 0) {
        response.content_length = (int32_t)strtol(value, NULL, 10);
    } else if (name_len == 12 && strncasecmp_local(line, "content-type", 12) == 0) {
        strncpy(response.content_type, value, sizeof(response.content_type) - 1);
        response.content_type[sizeof(response.content_type) - 1] = '\0';
    } else if (name_len == 8 && strncasecmp_local(line, "location", 8) == 0) {
        strncpy(response.location, value, sizeof(response.location) - 1);
        response.location[sizeof(response.location) - 1] = '\0';
    } else if (name_len == 17 && strncasecmp_local(line, "transfer-encoding", 17) == 0) {
        if (strncasecmp_local(value, "chunked", 7) == 0) {
            response.chunked = true;
            chunk_state = CHUNK_SIZE_LINE;
            chunk_remaining = 0;
        }
    }
}

/*
 * Deliver body data to the user callback, either directly or
 * through chunked decoding.
 */
static void http_deliver_body(const uint8_t *data, uint16_t len) {
    if (len == 0)
        return;

    if (response.chunked) {
        http_deliver_body_chunked(data, len);
    } else {
        /* Plain body: deliver directly */
        if (body_cb)
            body_cb(data, len, user_ctx);
        response.body_received += len;
    }
}

/*
 * Chunked transfer encoding decoder.
 * Format: hex-size\r\n...data...\r\n  (repeated; final chunk has size 0)
 */
static void http_deliver_body_chunked(const uint8_t *data, uint16_t len) {
    uint16_t pos = 0;

    while (pos < len && chunk_state != CHUNK_DONE) {
        switch (chunk_state) {
        case CHUNK_SIZE_LINE: {
            /* Parse hex chunk size, terminated by \r\n */
            while (pos < len) {
                uint8_t c = data[pos++];
                if (c == '\r') {
                    /* Expect \n next */
                    continue;
                }
                if (c == '\n') {
                    if (chunk_remaining == 0) {
                        chunk_state = CHUNK_DONE;
                    } else {
                        chunk_state = CHUNK_DATA;
                    }
                    break;
                }
                /* Parse hex digit */
                int digit = -1;
                if (c >= '0' && c <= '9')      digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
                if (digit >= 0)
                    chunk_remaining = chunk_remaining * 16 + digit;
                /* Ignore chunk extensions (;...) */
            }
            break;
        }

        case CHUNK_DATA: {
            /* Deliver up to chunk_remaining bytes */
            uint16_t avail = len - pos;
            uint16_t deliver = (avail < (uint16_t)chunk_remaining)
                                ? avail : (uint16_t)chunk_remaining;
            if (deliver > 0) {
                if (body_cb)
                    body_cb(data + pos, deliver, user_ctx);
                response.body_received += deliver;
                chunk_remaining -= deliver;
                pos += deliver;
            }
            if (chunk_remaining == 0)
                chunk_state = CHUNK_DATA_CRLF;
            break;
        }

        case CHUNK_DATA_CRLF: {
            /* Consume trailing \r\n after chunk data */
            uint8_t c = data[pos++];
            if (c == '\n') {
                /* End of CRLF — start next chunk */
                chunk_state = CHUNK_SIZE_LINE;
                chunk_remaining = 0;
            }
            /* Skip \r silently */
            break;
        }

        case CHUNK_DONE:
            return;
        }
    }
}
