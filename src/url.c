/*
 * Manul - URL Parser
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "url.h"

bool url_parse(const char *url_str, url_t *out) {
    memset(out, 0, sizeof(*out));

    const char *p = url_str;

    /* Parse scheme */
    if (strncmp(p, "https://", 8) == 0) {
        strcpy(out->scheme, "https");
        out->port = 443;
        p += 8;
    } else if (strncmp(p, "http://", 7) == 0) {
        strcpy(out->scheme, "http");
        out->port = 80;
        p += 7;
    } else {
        /* Default to http if no scheme */
        strcpy(out->scheme, "http");
        out->port = 80;
    }

    /* Parse host and optional port */
    const char *host_start = p;

    /* Find end of host: either ':', '/', or end of string */
    while (*p && *p != ':' && *p != '/')
        p++;

    size_t host_len = p - host_start;
    if (host_len == 0 || host_len >= sizeof(out->host))
        return false;

    memcpy(out->host, host_start, host_len);
    out->host[host_len] = '\0';

    /* Parse optional port */
    if (*p == ':') {
        p++;
        uint16_t port = 0;
        while (*p >= '0' && *p <= '9') {
            port = port * 10 + (*p - '0');
            p++;
        }
        if (port > 0)
            out->port = port;
    }

    /* Parse path (everything from '/' onwards) */
    if (*p == '/') {
        strncpy(out->path, p, sizeof(out->path) - 1);
    } else {
        strcpy(out->path, "/");
    }

    return true;
}

void url_resolve(const url_t *base, const char *relative, url_t *out) {
    /* Absolute URL? */
    if (strncmp(relative, "http://", 7) == 0 || strncmp(relative, "https://", 8) == 0) {
        url_parse(relative, out);
        return;
    }

    /* Copy base scheme, host, port */
    memcpy(out, base, sizeof(*out));

    if (relative[0] == '/') {
        /* Absolute path */
        strncpy(out->path, relative, sizeof(out->path) - 1);
        out->path[sizeof(out->path) - 1] = '\0';
    } else {
        /* Relative path: resolve against base path */
        /* Find last '/' in base path */
        char *last_slash = strrchr(out->path, '/');
        if (last_slash) {
            size_t base_dir_len = last_slash - out->path + 1;
            if (base_dir_len + strlen(relative) < sizeof(out->path)) {
                strcpy(out->path + base_dir_len, relative);
            }
        } else {
            snprintf(out->path, sizeof(out->path), "/%s", relative);
        }
    }

    /* Resolve ".." and "." segments */
    char resolved[URL_MAX];
    char *segments[64];
    int seg_count = 0;

    char temp[URL_MAX];
    strncpy(temp, out->path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *tok = strtok(temp, "/");
    while (tok && seg_count < 64) {
        if (strcmp(tok, ".") == 0) {
            /* Skip */
        } else if (strcmp(tok, "..") == 0) {
            if (seg_count > 0) seg_count--;
        } else {
            segments[seg_count++] = tok;
        }
        tok = strtok(NULL, "/");
    }

    resolved[0] = '/';
    resolved[1] = '\0';
    for (int i = 0; i < seg_count; i++) {
        if (i > 0) strncat(resolved, "/", sizeof(resolved) - strlen(resolved) - 1);
        strncat(resolved, segments[i], sizeof(resolved) - strlen(resolved) - 1);
    }

    strncpy(out->path, resolved, sizeof(out->path) - 1);
    out->path[sizeof(out->path) - 1] = '\0';
}

void url_to_string(const url_t *url, char *out, size_t out_sz) {
    bool default_port = (strcmp(url->scheme, "http") == 0 && url->port == 80) ||
                         (strcmp(url->scheme, "https") == 0 && url->port == 443);

    if (default_port)
        snprintf(out, out_sz, "%s://%s%s", url->scheme, url->host, url->path);
    else
        snprintf(out, out_sz, "%s://%s:%u%s", url->scheme, url->host, url->port, url->path);
}

void url_encode(const char *src, char *dst, size_t dst_sz) {
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 3 < dst_sz; si++) {
        uint8_t c = (uint8_t)src[si];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = c;
        } else if (c == ' ') {
            dst[di++] = '+';
        } else {
            dst[di++] = '%';
            dst[di++] = hex[c >> 4];
            dst[di++] = hex[c & 0x0F];
        }
    }
    dst[di] = '\0';
}

void url_decode(const char *src, char *dst, size_t dst_sz) {
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 1 < dst_sz; si++) {
        if (src[si] == '%' && isxdigit(src[si+1]) && isxdigit(src[si+2])) {
            uint8_t hi = src[si+1];
            uint8_t lo = src[si+2];
            hi = (hi >= 'a') ? hi - 'a' + 10 : (hi >= 'A') ? hi - 'A' + 10 : hi - '0';
            lo = (lo >= 'a') ? lo - 'a' + 10 : (lo >= 'A') ? lo - 'A' + 10 : lo - '0';
            dst[di++] = (hi << 4) | lo;
            si += 2;
        } else if (src[si] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[si];
        }
    }
    dst[di] = '\0';
}
