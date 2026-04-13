/*
 * frank-manul -- text web browser for RP2350 bare metal
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef HTML_H
#define HTML_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    HTML_TOKEN_TEXT,
    HTML_TOKEN_TAG_OPEN,
    HTML_TOKEN_TAG_CLOSE,
    HTML_TOKEN_TAG_SELF_CLOSE,
} html_token_type_t;

typedef struct {
    html_token_type_t type;
    char tag[16];               /* lowercase tag name */
    char text[256];             /* text content for TEXT tokens */
    uint16_t text_len;
    char attr_href[128];        /* href="" for <a> */
    char attr_alt[64];          /* alt="" for <img> */
    char attr_type[16];         /* type="" for <input> */
    char attr_name[32];         /* name="" */
    char attr_value[64];        /* value="" */
    char attr_action[128];      /* action="" for <form> */
    char attr_src[128];         /* src="" for <img> */
} html_token_t;                 /* ~836 bytes */

typedef struct {
    enum {
        HPS_TEXT,
        HPS_TAG_NAME,
        HPS_TAG_ATTRS,
        HPS_TAG_CLOSE,
        HPS_COMMENT,
        HPS_SCRIPT,
        HPS_STYLE,
    } state;
    char buf[512];
    uint16_t buf_len;
    bool is_closing_tag;
    char current_tag[16];
    uint16_t tag_name_len;
    html_token_t token;         /* reusable token — kept off the stack */
} html_parser_t;

typedef void (*html_token_cb_t)(const html_token_t *token, void *ctx);

/* Initialize a parser instance. Must be called before first feed. */
void html_parser_init(html_parser_t *p);

/* Feed raw bytes into the parser. Tokens are emitted via cb as they are
 * recognized. Can be called repeatedly with successive chunks. */
void html_parser_feed(html_parser_t *p, const uint8_t *data, uint16_t len,
                      html_token_cb_t cb, void *ctx);

/* Signal end of input. Flushes any remaining buffered text as a token. */
void html_parser_finish(html_parser_t *p, html_token_cb_t cb, void *ctx);

#endif /* HTML_H */
