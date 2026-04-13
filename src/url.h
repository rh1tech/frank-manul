/*
 * Manul - URL Parser
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef URL_H
#define URL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define URL_MAX 512

typedef struct {
    char     scheme[8];      /* "http" or "https" */
    char     host[128];
    uint16_t port;           /* 80 or 443 default */
    char     path[URL_MAX];  /* includes query string, starts with '/' */
} url_t;

/* Parse a URL string into components. Returns true on success. */
bool url_parse(const char *url_str, url_t *out);

/* Resolve a relative URL against a base URL. */
void url_resolve(const url_t *base, const char *relative, url_t *out);

/* Convert url_t back to string. */
void url_to_string(const url_t *url, char *out, size_t out_sz);

/* URL-encode a string (for form data). */
void url_encode(const char *src, char *dst, size_t dst_sz);

/* URL-decode a string. */
void url_decode(const char *src, char *dst, size_t dst_sz);

#endif
