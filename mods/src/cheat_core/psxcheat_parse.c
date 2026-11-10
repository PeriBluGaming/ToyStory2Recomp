/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "psxcheat_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

const char *psxcheat_status_name(psxcheat_status status)
{
    switch (status) {
    case PSXCHEAT_OK: return "ok";
    case PSXCHEAT_ERR_ARGUMENT: return "invalid argument";
    case PSXCHEAT_ERR_TEXT_TOO_LONG: return "text too long";
    case PSXCHEAT_ERR_LINE_TOO_LONG: return "line too long";
    case PSXCHEAT_ERR_TOO_MANY_CODES: return "too many codes";
    case PSXCHEAT_ERR_INVALID_CHAR: return "invalid character";
    case PSXCHEAT_ERR_SYNTAX: return "syntax error";
    case PSXCHEAT_ERR_HEX: return "invalid hex digit";
    case PSXCHEAT_ERR_UNSUPPORTED_TYPE: return "unsupported code type";
    case PSXCHEAT_ERR_XPLORER: return "Xplorer code not supported";
    case PSXCHEAT_ERR_VALUE_WIDTH: return "value too wide";
    case PSXCHEAT_ERR_ADDRESS_RANGE: return "address out of range";
    case PSXCHEAT_ERR_ALIGNMENT: return "misaligned address";
    case PSXCHEAT_ERR_INVALID_PROGRAM: return "invalid program";
    case PSXCHEAT_ERR_MEMORY_REJECTED: return "memory rejected address";
    case PSXCHEAT_ERR_MEMORY_WRITE: return "memory write failed";
    }
    return "unknown status";
}

/* Fills `err` (when given) and returns `status`. Shared with the executor. */
psxcheat_status psxcheat_fail(psxcheat_error *err, psxcheat_status status,
                              uint32_t line, uint32_t column,
                              const char *fmt, ...)
{
    if (err) {
        int n = 0;
        va_list ap;
        err->status = status;
        err->line = line;
        err->column = column;
        if (line && column)
            n = snprintf(err->message, sizeof err->message, "line %u, column %u: ",
                         (unsigned)line, (unsigned)column);
        else if (line)
            n = snprintf(err->message, sizeof err->message, "line %u: ", (unsigned)line);
        if (n < 0 || (size_t)n >= sizeof err->message) n = 0;
        va_start(ap, fmt);
        vsnprintf(err->message + n, sizeof err->message - (size_t)n, fmt, ap);
        va_end(ap);
    }
    return status;
}

static void clear_error(psxcheat_error *err)
{
    if (err) {
        err->status = PSXCHEAT_OK;
        err->line = 0;
        err->column = 0;
        err->message[0] = '\0';
    }
}

static int is_blank(char c) { return c == ' ' || c == '\t'; }
static int is_comment(char c) { return c == '#' || c == ';'; }

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parses exactly `digits` hex digits at `s`. On a bad digit returns 0 and
 * sets *bad to its index. */
static int parse_hex(const char *s, size_t digits, uint32_t *out, size_t *bad)
{
    uint32_t v = 0;
    size_t i;
    for (i = 0; i < digits; ++i) {
        const int d = hex_value(s[i]);
        if (d < 0) {
            *bad = i;
            return 0;
        }
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return 1;
}

typedef struct parse_ctx {
    psxcheat_program *out;
    psxcheat_error *err;
} parse_ctx;

/* One line without its EOL. Returns PSXCHEAT_OK for a code, blank or comment
 * line (appending codes to the program). */
static psxcheat_status parse_line(parse_ctx *p, const char *s, size_t n, uint32_t line)
{
    size_t i = 0, code_end, tok, len, addr_col, value_col;
    uint32_t word, value;
    size_t bad;
    unsigned type, digit1, digit2;
    uint32_t offset;
    psxcheat_line *dst;

    /* Characters: printable ASCII and tab in code; comments may also carry
     * UTF-8 (bytes >= 0x80). Control characters are rejected anywhere. */
    code_end = n;
    for (i = 0; i < n; ++i) {
        if (is_comment(s[i])) {
            code_end = i;
            break;
        }
    }
    for (i = 0; i < n; ++i) {
        const unsigned char c = (unsigned char)s[i];
        const int in_comment = i >= code_end;
        if (c == '\t') continue;
        if (c < 0x20 || c == 0x7F || (c >= 0x80 && !in_comment))
            return psxcheat_fail(p->err, PSXCHEAT_ERR_INVALID_CHAR, line,
                                 (uint32_t)i + 1, "invalid byte 0x%02X", (unsigned)c);
    }

    i = 0;
    while (i < code_end && is_blank(s[i])) ++i;
    if (i == code_end) return PSXCHEAT_OK;  /* blank or comment-only line */

    /* Address word: exactly 8 non-blank characters. */
    tok = i;
    while (i < code_end && !is_blank(s[i])) ++i;
    len = i - tok;
    if (len != 8)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_SYNTAX, line, (uint32_t)tok + 1,
                             "expected \"TTAAAAAA VVVV\": the first field must have 8 hex digits, found %u characters",
                             (unsigned)len);
    if (!parse_hex(s + tok, 8, &word, &bad))
        return psxcheat_fail(p->err, PSXCHEAT_ERR_HEX, line, (uint32_t)(tok + bad) + 1,
                             "'%c' is not a hex digit", s[tok + bad]);
    addr_col = tok + 1;  /* 1-based column of the type byte */

    /* Separator, then the value: exactly 4 non-blank characters. */
    if (i == code_end)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_SYNTAX, line, (uint32_t)i + 1,
                             "expected \"TTAAAAAA VVVV\": the 4-digit value is missing");
    while (i < code_end && is_blank(s[i])) ++i;
    if (i == code_end)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_SYNTAX, line, (uint32_t)i + 1,
                             "expected \"TTAAAAAA VVVV\": the 4-digit value is missing");
    tok = i;
    while (i < code_end && !is_blank(s[i])) ++i;
    len = i - tok;
    if (len != 4)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_SYNTAX, line, (uint32_t)tok + 1,
                             "expected \"TTAAAAAA VVVV\": the value must have 4 hex digits, found %u characters",
                             (unsigned)len);
    if (!parse_hex(s + tok, 4, &value, &bad))
        return psxcheat_fail(p->err, PSXCHEAT_ERR_HEX, line, (uint32_t)(tok + bad) + 1,
                             "'%c' is not a hex digit", s[tok + bad]);
    value_col = tok + 1;
    while (i < code_end && is_blank(s[i])) ++i;
    if (i != code_end)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_SYNTAX, line, (uint32_t)i + 1,
                             "unexpected text after the code");

    /* Classify. The first field is TT (type byte) + AAAAAA (24-bit offset). */
    type = (unsigned)(word >> 24);
    digit1 = type >> 4;
    digit2 = type & 0xFu;
    offset = word & 0x00FFFFFFu;

    if ((digit1 == 0x3u || digit1 == 0x8u) && digit2 != 0u)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_XPLORER, line, (uint32_t)addr_col,
                             "code type %02X is an Xplorer/Xploder code (second digit carries encryption/flags); only GameShark 30 and 80 codes are supported",
                             type);
    if (type != 0x30u && type != 0x80u)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_UNSUPPORTED_TYPE, line, (uint32_t)addr_col,
                             "code type %02X is not supported (supported: 30, 80)", type);

    if (offset >= PSXCHEAT_RAM_SIZE)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_ADDRESS_RANGE, line, (uint32_t)addr_col + 2,
                             "offset %06X is outside the 2 MiB RAM (000000-1FFFFF)",
                             (unsigned)offset);
    if (type == 0x30u && (value >> 8) != 0u)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_VALUE_WIDTH, line, (uint32_t)value_col,
                             "8-bit code 30 needs a value 00XX, found %04X", (unsigned)value);
    if (type == 0x80u && (offset & 1u) != 0u)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_ALIGNMENT, line, (uint32_t)addr_col + 2,
                             "16-bit code 80 needs an even offset, found %06X", (unsigned)offset);

    if (p->out->count >= PSXCHEAT_MAX_CODES)
        return psxcheat_fail(p->err, PSXCHEAT_ERR_TOO_MANY_CODES, line, (uint32_t)addr_col,
                             "more than %u codes", (unsigned)PSXCHEAT_MAX_CODES);
    dst = &p->out->lines[p->out->count++];
    dst->op = (uint8_t)(type == 0x30u ? PSXCHEAT_OP_WRITE8 : PSXCHEAT_OP_WRITE16);
    dst->code_type = (uint8_t)type;
    dst->value = (uint16_t)value;
    dst->offset = offset;
    dst->source_line = line;
    return PSXCHEAT_OK;
}

psxcheat_status psxcheat_parse(const char *text, size_t len,
                               psxcheat_program *out, psxcheat_error *err)
{
    parse_ctx p;
    size_t pos = 0;
    uint32_t line = 0;
    psxcheat_status st;

    clear_error(err);
    if (!out)
        return psxcheat_fail(err, PSXCHEAT_ERR_ARGUMENT, 0, 0, "no output program");
    out->count = 0;
    if (!text && len != 0)
        return psxcheat_fail(err, PSXCHEAT_ERR_ARGUMENT, 0, 0, "no input text");
    if (len > PSXCHEAT_MAX_TEXT)
        return psxcheat_fail(err, PSXCHEAT_ERR_TEXT_TOO_LONG, 0, 0,
                             "input has %lu bytes, the limit is %u",
                             (unsigned long)len, (unsigned)PSXCHEAT_MAX_TEXT);

    p.out = out;
    p.err = err;
    while (pos < len) {
        const char *s = text + pos;
        const char *nl = (const char *)memchr(s, '\n', len - pos);
        size_t n = nl ? (size_t)(nl - s) : len - pos;
        pos += n + (nl ? 1u : 0u);
        ++line;
        /* Only CRLF ends a line besides LF. A CR not followed by LF, including
         * one at the very end of the input, stays in the line and is rejected
         * as a control character by parse_line. */
        if (nl && n > 0 && s[n - 1] == '\r') --n;
        if (n > PSXCHEAT_MAX_LINE) {
            out->count = 0;
            return psxcheat_fail(err, PSXCHEAT_ERR_LINE_TOO_LONG, line, 0,
                                 "line has %lu bytes, the limit is %u",
                                 (unsigned long)n, (unsigned)PSXCHEAT_MAX_LINE);
        }
        st = parse_line(&p, s, n, line);
        if (st != PSXCHEAT_OK) {
            out->count = 0;  /* a failed parse never yields runnable codes */
            return st;
        }
    }
    return PSXCHEAT_OK;
}
