/*
 * Manul - HTTP/1.0 Client
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * Streaming HTTP/1.0 client using netcard for network I/O.
 * Supports HTTP and HTTPS (TLS via netcard), redirects, and
 * chunked transfer encoding.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <stdbool.h>

#define HTTP_MAX_URL 512

typedef enum {
    HTTP_STATE_IDLE,
    HTTP_STATE_CONNECTING,
    HTTP_STATE_SENDING,
    HTTP_STATE_RECV_STATUS,
    HTTP_STATE_RECV_HEADERS,
    HTTP_STATE_RECV_BODY,
    HTTP_STATE_DONE,
    HTTP_STATE_ERROR,
} http_state_t;

typedef struct {
    uint16_t status_code;
    int32_t  content_length;    /* -1 if unknown */
    int32_t  body_received;
    bool     chunked;
    char     content_type[64];
    char     location[HTTP_MAX_URL];  /* for redirects */
} http_response_t;

/* Body data callback: called as chunks arrive */
typedef void (*http_body_cb_t)(const uint8_t *data, uint16_t len, void *ctx);

/* Done callback: called when transfer completes or errors out */
typedef void (*http_done_cb_t)(void *ctx);

/* Initialize the HTTP client (registers netcard callbacks) */
void http_init(void);

/* Call from main loop — handles deferred redirects safely outside
 * netcard_poll callback context to avoid recursive netcard_poll. */
void http_poll(void);

/* Start an HTTP GET request. Returns false if already busy or URL invalid. */
bool http_get(const char *url_str, http_body_cb_t body_cb,
              http_done_cb_t done_cb, void *ctx);

/* Current state of the HTTP client */
http_state_t http_get_state(void);

/* Access response metadata (valid after headers received) */
const http_response_t *http_get_response(void);

/* Abort an in-progress request */
void http_abort(void);

#endif /* HTTP_H */
