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

#include "html.h"
#include <string.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  Void elements -- treated as self-closing even without trailing /  */
/* ------------------------------------------------------------------ */

static const char *void_elements[] = {
    "area", "base", "br", "col", "embed", "hr", "img", "input",
    "link", "meta", "source", "track", "wbr", NULL
};

static bool is_void_element(const char *tag)
{
    for (const char **v = void_elements; *v; v++) {
        if (strcmp(tag, *v) == 0)
            return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/*  Lower-case helper                                                 */
/* ------------------------------------------------------------------ */

static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

/* ------------------------------------------------------------------ */
/*  HTML entity decoding                                              */
/* ------------------------------------------------------------------ */

/* Decode a single entity reference (text between '&' and ';' exclusive).
 * Writes decoded character(s) into dst, returns number of bytes written. */
static uint16_t decode_entity(const char *ent, uint16_t ent_len,
                              char *dst, uint16_t dst_max)
{
    if (ent_len == 0 || dst_max == 0)
        return 0;

    /* Named entities */
    if (ent_len == 3 && memcmp(ent, "amp", 3) == 0) {
        dst[0] = '&'; return 1;
    }
    if (ent_len == 2 && memcmp(ent, "lt", 2) == 0) {
        dst[0] = '<'; return 1;
    }
    if (ent_len == 2 && memcmp(ent, "gt", 2) == 0) {
        dst[0] = '>'; return 1;
    }
    if (ent_len == 4 && memcmp(ent, "quot", 4) == 0) {
        dst[0] = '"'; return 1;
    }
    if (ent_len == 4 && memcmp(ent, "apos", 4) == 0) {
        dst[0] = '\''; return 1;
    }
    if (ent_len == 4 && memcmp(ent, "nbsp", 4) == 0) {
        dst[0] = ' '; return 1;
    }

    /* Numeric: &#NNN; or &#xHH; */
    if (ent_len >= 2 && ent[0] == '#') {
        uint32_t cp = 0;
        if (ent[1] == 'x' || ent[1] == 'X') {
            /* Hexadecimal */
            for (uint16_t i = 2; i < ent_len; i++) {
                char c = ent[i];
                uint8_t nibble;
                if (c >= '0' && c <= '9')      nibble = (uint8_t)(c - '0');
                else if (c >= 'a' && c <= 'f') nibble = (uint8_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') nibble = (uint8_t)(c - 'A' + 10);
                else return 0;
                cp = (cp << 4) | nibble;
                if (cp > 0x10FFFF) return 0;
            }
        } else {
            /* Decimal */
            for (uint16_t i = 1; i < ent_len; i++) {
                char c = ent[i];
                if (c < '0' || c > '9') return 0;
                cp = cp * 10 + (uint32_t)(c - '0');
                if (cp > 0x10FFFF) return 0;
            }
        }

        /* Encode code point as UTF-8 */
        if (cp == 0) {
            return 0;
        } else if (cp < 0x80) {
            dst[0] = (char)cp;
            return 1;
        } else if (cp < 0x800 && dst_max >= 2) {
            dst[0] = (char)(0xC0 | (cp >> 6));
            dst[1] = (char)(0x80 | (cp & 0x3F));
            return 2;
        } else if (cp < 0x10000 && dst_max >= 3) {
            dst[0] = (char)(0xE0 | (cp >> 12));
            dst[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[2] = (char)(0x80 | (cp & 0x3F));
            return 3;
        } else if (cp <= 0x10FFFF && dst_max >= 4) {
            dst[0] = (char)(0xF0 | (cp >> 18));
            dst[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[3] = (char)(0x80 | (cp & 0x3F));
            return 4;
        }
    }

    /* Unknown entity -- emit the raw text including & and ; */
    return 0;
}

/* Decode all entities in src[0..src_len) into dst, returning length.
 * dst must have room for at least dst_max bytes. */
static uint16_t decode_entities(const char *src, uint16_t src_len,
                                char *dst, uint16_t dst_max)
{
    uint16_t out = 0;
    uint16_t i = 0;

    while (i < src_len && out < dst_max) {
        if (src[i] == '&') {
            /* Look for ';' within a reasonable range */
            uint16_t end = i + 1;
            uint16_t limit = i + 12; /* entities are short */
            if (limit > src_len)
                limit = src_len;
            while (end < limit && src[end] != ';')
                end++;

            if (end < limit && src[end] == ';') {
                uint16_t ent_len = end - i - 1;
                uint16_t wrote = decode_entity(src + i + 1, ent_len,
                                               dst + out, dst_max - out);
                if (wrote > 0) {
                    out += wrote;
                    i = end + 1;
                    continue;
                }
            }
            /* Not a valid entity -- pass '&' through literally */
            dst[out++] = '&';
            i++;
        } else {
            dst[out++] = src[i++];
        }
    }
    return out;
}

/* ------------------------------------------------------------------ */
/*  Token helpers                                                     */
/* ------------------------------------------------------------------ */

static void token_clear(html_token_t *tok)
{
    memset(tok, 0, sizeof(*tok));
}

/* Emit accumulated text in the parser buffer as an HTML_TOKEN_TEXT.
 * Decodes entities and collapses the buffer. */
static void flush_text(html_parser_t *p, html_token_cb_t cb, void *ctx)
{
    if (p->buf_len == 0)
        return;

    html_token_t *tok = &p->token;
    token_clear(tok);
    tok->type = HTML_TOKEN_TEXT;

    tok->text_len = decode_entities(p->buf, p->buf_len,
                                    tok->text, sizeof(tok->text) - 1);
    tok->text[tok->text_len] = '\0';

    p->buf_len = 0;

    if (tok->text_len > 0)
        cb(tok, ctx);
}

/* Append a byte to the parser text buffer, flushing if full. */
static void buf_append(html_parser_t *p, char c,
                       html_token_cb_t cb, void *ctx)
{
    if (p->buf_len >= sizeof(p->buf) - 1)
        flush_text(p, cb, ctx);
    p->buf[p->buf_len++] = c;
}

/* ------------------------------------------------------------------ */
/*  Attribute parsing                                                 */
/* ------------------------------------------------------------------ */

/* Case-insensitive comparison of n bytes. */
static bool ci_eq(const char *a, const char *b, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        if (to_lower(a[i]) != to_lower(b[i]))
            return false;
    }
    return true;
}

/* Copy a value into dst, decoding entities, respecting dst_max. */
static void copy_attr_value(const char *src, uint16_t len,
                            char *dst, uint16_t dst_max)
{
    uint16_t n = decode_entities(src, len, dst, dst_max - 1);
    dst[n] = '\0';
}

/* Parse the attribute region (everything between tag-name and '>') and
 * populate the relevant fields in tok. */
static void parse_attributes(const char *attrs, uint16_t attrs_len,
                             html_token_t *tok)
{
    uint16_t i = 0;

    while (i < attrs_len) {
        /* Skip whitespace */
        while (i < attrs_len && ((unsigned char)attrs[i] <= ' '))
            i++;
        if (i >= attrs_len)
            break;

        /* Read attribute name */
        uint16_t name_start = i;
        while (i < attrs_len && attrs[i] != '=' &&
               (unsigned char)attrs[i] > ' ' && attrs[i] != '>')
            i++;
        uint16_t name_len = i - name_start;
        if (name_len == 0)
            break;

        /* Skip whitespace around '=' */
        while (i < attrs_len && (unsigned char)attrs[i] <= ' ')
            i++;

        if (i >= attrs_len || attrs[i] != '=') {
            /* Boolean attribute -- no value, skip it */
            continue;
        }
        i++; /* skip '=' */

        while (i < attrs_len && (unsigned char)attrs[i] <= ' ')
            i++;

        if (i >= attrs_len)
            break;

        /* Read value */
        uint16_t val_start, val_len;
        if (attrs[i] == '"' || attrs[i] == '\'') {
            char quote = attrs[i++];
            val_start = i;
            while (i < attrs_len && attrs[i] != quote)
                i++;
            val_len = i - val_start;
            if (i < attrs_len)
                i++; /* skip closing quote */
        } else {
            /* Unquoted value */
            val_start = i;
            while (i < attrs_len && (unsigned char)attrs[i] > ' ' &&
                   attrs[i] != '>')
                i++;
            val_len = i - val_start;
        }

        /* Match known attribute names (case-insensitive) */
        if (name_len == 4 && ci_eq(attrs + name_start, "href", 4)) {
            copy_attr_value(attrs + val_start, val_len,
                            tok->attr_href, sizeof(tok->attr_href));
        } else if (name_len == 3 && ci_eq(attrs + name_start, "alt", 3)) {
            copy_attr_value(attrs + val_start, val_len,
                            tok->attr_alt, sizeof(tok->attr_alt));
        } else if (name_len == 4 && ci_eq(attrs + name_start, "type", 4)) {
            copy_attr_value(attrs + val_start, val_len,
                            tok->attr_type, sizeof(tok->attr_type));
        } else if (name_len == 4 && ci_eq(attrs + name_start, "name", 4)) {
            copy_attr_value(attrs + val_start, val_len,
                            tok->attr_name, sizeof(tok->attr_name));
        } else if (name_len == 5 && ci_eq(attrs + name_start, "value", 5)) {
            copy_attr_value(attrs + val_start, val_len,
                            tok->attr_value, sizeof(tok->attr_value));
        } else if (name_len == 6 && ci_eq(attrs + name_start, "action", 6)) {
            copy_attr_value(attrs + val_start, val_len,
                            tok->attr_action, sizeof(tok->attr_action));
        } else if (name_len == 3 && ci_eq(attrs + name_start, "src", 3)) {
            copy_attr_value(attrs + val_start, val_len,
                            tok->attr_src, sizeof(tok->attr_src));
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Tag emission                                                      */
/* ------------------------------------------------------------------ */

/* Emit a tag token from the accumulated buf (tag name + attributes).
 * is_closing is true when the tag started with '</'. */
static void emit_tag(html_parser_t *p, bool self_close,
                     html_token_cb_t cb, void *ctx)
{
    html_token_t *tok = &p->token;
    token_clear(tok);

    /* Copy lowercased tag name */
    uint16_t tlen = p->tag_name_len;
    if (tlen >= sizeof(tok->tag))
        tlen = sizeof(tok->tag) - 1;
    for (uint16_t i = 0; i < tlen; i++)
        tok->tag[i] = to_lower(p->current_tag[i]);
    tok->tag[tlen] = '\0';

    if (p->is_closing_tag) {
        tok->type = HTML_TOKEN_TAG_CLOSE;
    } else if (self_close || is_void_element(tok->tag)) {
        tok->type = HTML_TOKEN_TAG_SELF_CLOSE;
    } else {
        tok->type = HTML_TOKEN_TAG_OPEN;
    }

    /* Parse attributes from buf (buf holds everything after the tag name) */
    if (!p->is_closing_tag && p->buf_len > 0) {
        parse_attributes(p->buf, p->buf_len, tok);
    }

    p->buf_len = 0;

    /* Detect <script> and <style> opening tags */
    if (tok->type == HTML_TOKEN_TAG_OPEN) {
        if (strcmp(tok->tag, "script") == 0) {
            cb(tok, ctx);
            p->state = HPS_SCRIPT;
            return;
        }
        if (strcmp(tok->tag, "style") == 0) {
            cb(tok, ctx);
            p->state = HPS_STYLE;
            return;
        }
    }

    cb(tok, ctx);
}

/* ------------------------------------------------------------------ */
/*  Core state machine                                                */
/* ------------------------------------------------------------------ */

void html_parser_init(html_parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state = HPS_TEXT;
}

void html_parser_feed(html_parser_t *p, const uint8_t *data, uint16_t len,
                      html_token_cb_t cb, void *ctx)
{
    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];

        switch (p->state) {

        /* ---------------------------------------------------------- */
        case HPS_TEXT:
            if (c == '<') {
                flush_text(p, cb, ctx);
                p->state = HPS_TAG_NAME;
                p->is_closing_tag = false;
                p->tag_name_len = 0;
                p->current_tag[0] = '\0';
                p->buf_len = 0;
            } else {
                buf_append(p, c, cb, ctx);
            }
            break;

        /* ---------------------------------------------------------- */
        case HPS_TAG_NAME:
            if (c == '/' && p->tag_name_len == 0 && !p->is_closing_tag) {
                /* This is a closing tag: </ */
                p->is_closing_tag = true;
            } else if (c == '!' && p->tag_name_len == 0) {
                /* Could be a comment or doctype.  Stash '!' and keep
                 * reading to decide. */
                if (p->tag_name_len < sizeof(p->current_tag) - 1)
                    p->current_tag[p->tag_name_len++] = c;
            } else if (c == '-' && p->tag_name_len == 1 &&
                       p->current_tag[0] == '!') {
                /* "<!-" -- need one more '-' */
                if (p->tag_name_len < sizeof(p->current_tag) - 1)
                    p->current_tag[p->tag_name_len++] = c;
            } else if (c == '-' && p->tag_name_len == 2 &&
                       p->current_tag[0] == '!' &&
                       p->current_tag[1] == '-') {
                /* "<!--" confirmed -- switch to comment mode */
                p->state = HPS_COMMENT;
                p->buf_len = 0;
            } else if ((unsigned char)c <= ' ' && p->tag_name_len > 0) {
                /* End of tag name, start of attributes */
                p->current_tag[p->tag_name_len] = '\0';
                /* Skip <!DOCTYPE ...> */
                if (p->current_tag[0] == '!') {
                    /* Treat as comment-like: skip until '>' */
                    p->state = HPS_TAG_ATTRS;
                    p->buf_len = 0;
                    /* Mark as closing so we don't emit */
                    p->is_closing_tag = true;
                    /* Actually just skip everything, use a flag:
                     * we store '!' tag name so emit_tag will produce
                     * a tag with name starting with '!' which we can
                     * special-case. Instead, let's just eat until '>'. */
                } else {
                    p->state = HPS_TAG_ATTRS;
                    p->buf_len = 0;
                }
            } else if (c == '>' && p->tag_name_len > 0) {
                /* Tag ended immediately, e.g. <br> */
                p->current_tag[p->tag_name_len] = '\0';
                if (p->current_tag[0] == '!') {
                    /* <!DOCTYPE> or similar -- ignore */
                    p->state = HPS_TEXT;
                    p->buf_len = 0;
                } else {
                    p->buf_len = 0;
                    emit_tag(p, false, cb, ctx);
                    if (p->state != HPS_SCRIPT && p->state != HPS_STYLE)
                        p->state = HPS_TEXT;
                }
            } else if (c == '>') {
                /* Empty tag "<>" -- ignore */
                p->state = HPS_TEXT;
                p->buf_len = 0;
            } else {
                /* Accumulate tag name character */
                if (p->tag_name_len < sizeof(p->current_tag) - 1)
                    p->current_tag[p->tag_name_len++] = c;
            }
            break;

        /* ---------------------------------------------------------- */
        case HPS_TAG_ATTRS:
            if (c == '>') {
                /* Check for self-closing '/>' */
                bool self_close = false;
                if (p->buf_len > 0 && p->buf[p->buf_len - 1] == '/') {
                    self_close = true;
                    p->buf_len--; /* strip trailing '/' from attrs */
                }

                p->current_tag[p->tag_name_len] = '\0';

                /* Skip <!...> directives */
                if (p->current_tag[0] == '!') {
                    p->state = HPS_TEXT;
                    p->buf_len = 0;
                } else {
                    emit_tag(p, self_close, cb, ctx);
                    if (p->state != HPS_SCRIPT && p->state != HPS_STYLE)
                        p->state = HPS_TEXT;
                }
            } else {
                /* Accumulate attribute bytes */
                if (p->buf_len < sizeof(p->buf) - 1)
                    p->buf[p->buf_len++] = c;
            }
            break;

        /* ---------------------------------------------------------- */
        case HPS_COMMENT:
            /* Accumulate last 3 chars to detect "-->" */
            if (p->buf_len < sizeof(p->buf) - 1)
                p->buf[p->buf_len++] = c;

            if (p->buf_len >= 3 &&
                p->buf[p->buf_len - 3] == '-' &&
                p->buf[p->buf_len - 2] == '-' &&
                p->buf[p->buf_len - 1] == '>') {
                p->state = HPS_TEXT;
                p->buf_len = 0;
            }

            /* Prevent buffer overflow: keep only last 3 bytes */
            if (p->buf_len >= sizeof(p->buf) - 1) {
                p->buf[0] = p->buf[p->buf_len - 2];
                p->buf[1] = p->buf[p->buf_len - 1];
                p->buf_len = 2;
            }
            break;

        /* ---------------------------------------------------------- */
        case HPS_SCRIPT:
        case HPS_STYLE: {
            /* Look for </script> or </style> (case-insensitive).
             * We accumulate into buf and scan for the closing tag. */
            if (p->buf_len < sizeof(p->buf) - 1)
                p->buf[p->buf_len++] = c;

            /* Only check when we see '>' */
            if (c == '>') {
                const char *target = (p->state == HPS_SCRIPT)
                                     ? "</script>" : "</style>";
                uint16_t tgt_len = (p->state == HPS_SCRIPT) ? 9 : 8;

                if (p->buf_len >= tgt_len) {
                    bool match = true;
                    uint16_t start = p->buf_len - tgt_len;
                    for (uint16_t j = 0; j < tgt_len; j++) {
                        if (to_lower(p->buf[start + j]) != target[j]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        /* Emit the closing tag token */
                        html_token_t *tok = &p->token;
                        token_clear(tok);
                        tok->type = HTML_TOKEN_TAG_CLOSE;
                        if (p->state == HPS_SCRIPT)
                            memcpy(tok->tag, "script", 7);
                        else
                            memcpy(tok->tag, "style", 6);
                        cb(tok, ctx);
                        p->state = HPS_TEXT;
                        p->buf_len = 0;
                    }
                }
            }

            /* Prevent buffer from overflowing: keep only the tail we
             * need for matching (last tgt_len - 1 bytes). */
            if (p->buf_len >= sizeof(p->buf) - 1) {
                uint16_t keep = 16; /* enough for "</script>" */
                if (p->buf_len > keep) {
                    memmove(p->buf, p->buf + p->buf_len - keep, keep);
                    p->buf_len = keep;
                }
            }
            break;
        }

        /* ---------------------------------------------------------- */
        case HPS_TAG_CLOSE:
            /* Not used in current implementation -- closing tags go
             * through HPS_TAG_NAME with is_closing_tag set. */
            if (c == '>') {
                p->state = HPS_TEXT;
                p->buf_len = 0;
            }
            break;
        }
    }
}

void html_parser_finish(html_parser_t *p, html_token_cb_t cb, void *ctx)
{
    if (p->state == HPS_TEXT && p->buf_len > 0) {
        flush_text(p, cb, ctx);
    }
    /* Reset state for potential reuse */
    p->state = HPS_TEXT;
    p->buf_len = 0;
}
