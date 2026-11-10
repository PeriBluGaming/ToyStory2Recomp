/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Unit tests for the psxcheat core (mods/src/cheat_core). Fake memory only. */
#include "fake_memory.h"
#include "psxcheat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failures;
static const char *g_test = "";

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

/* Large objects live in static storage. */
static fake_memory g_mem;
static psxcheat_program g_prog;
static psxcheat_error g_err;

static psxcheat_status parse(const char *text)
{
    return psxcheat_parse(text, strlen(text), &g_prog, &g_err);
}

/* Parses `text`, expects `status` at `line` (and `column` when nonzero). */
static void expect_parse_error(const char *text, psxcheat_status status,
                               uint32_t line, uint32_t column)
{
    const psxcheat_status st = parse(text);
    CHECK(st == status);
    CHECK(g_err.status == status);
    CHECK(g_err.line == line);
    if (column) CHECK(g_err.column == column);
    CHECK(g_prog.count == 0);
    CHECK(g_err.message[0] != '\0');
    if (st != status || g_err.line != line || (column && g_err.column != column))
        printf("    text \"%s\" -> %s, line %u col %u: %s\n", text,
               psxcheat_status_name(st), (unsigned)g_err.line,
               (unsigned)g_err.column, g_err.message);
}

static psxcheat_status run(psxcheat_result *res)
{
    const psxcheat_memory b = fake_memory_backend(&g_mem);
    return psxcheat_execute(&g_prog, &b, res, &g_err);
}

/* ------------------------------------------------------------------------- */

static void test_valid_30_80(void)
{
    psxcheat_result res;
    g_test = "valid 30/80";
    fake_memory_reset(&g_mem);
    CHECK(parse("300B21C8 0009\n800B221C 002A\n") == PSXCHEAT_OK);
    CHECK(g_prog.count == 2);
    CHECK(g_prog.lines[0].op == PSXCHEAT_OP_WRITE8);
    CHECK(g_prog.lines[0].code_type == 0x30);
    CHECK(g_prog.lines[0].offset == 0x0B21C8u);
    CHECK(g_prog.lines[0].value == 0x0009u);
    CHECK(g_prog.lines[0].source_line == 1);
    CHECK(g_prog.lines[1].op == PSXCHEAT_OP_WRITE16);
    CHECK(g_prog.lines[1].code_type == 0x80);
    CHECK(g_prog.lines[1].offset == 0x0B221Cu);
    CHECK(g_prog.lines[1].value == 0x002Au);
    CHECK(g_prog.lines[1].source_line == 2);
    CHECK(run(&res) == PSXCHEAT_OK);
    CHECK(res.writes == 2);
    CHECK(fake_memory_u8(&g_mem, 0x0B21C8u) == 0x09);
    CHECK(fake_memory_u16(&g_mem, 0x0B221Cu) == 0x002Au);
    /* 16-bit writes are little-endian. */
    CHECK(parse("80000100 BEEF") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_OK);
    CHECK(fake_memory_u8(&g_mem, 0x100) == 0xEF);
    CHECK(fake_memory_u8(&g_mem, 0x101) == 0xBE);
}

static void test_hex_case(void)
{
    psxcheat_result res;
    g_test = "hex case";
    fake_memory_reset(&g_mem);
    CHECK(parse("300abcde 00ff\n800AbCdE fEdC\n") == PSXCHEAT_OK);
    CHECK(g_prog.count == 2);
    CHECK(g_prog.lines[0].offset == 0x0ABCDEu && g_prog.lines[0].value == 0xFFu);
    CHECK(g_prog.lines[1].offset == 0x0ABCDEu && g_prog.lines[1].value == 0xFEDCu);
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 2);
}

static void test_whitespace_comments(void)
{
    psxcheat_result res;
    g_test = "whitespace/comments";
    fake_memory_reset(&g_mem);
    CHECK(parse("# Infinite something (placeholder addresses)\n"
                "\n"
                "   \t\n"
                "  30000010    00AA   # comment after code\n"
                "\t80000020\t1234\t; another comment\r\n"
                "; full-line comment\r\n"
                "30000030 00BB   ") == PSXCHEAT_OK);
    CHECK(g_prog.count == 3);
    CHECK(g_prog.lines[0].source_line == 4);
    CHECK(g_prog.lines[1].source_line == 5);
    CHECK(g_prog.lines[2].source_line == 7);
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 3);
    CHECK(fake_memory_u8(&g_mem, 0x10) == 0xAA);
    CHECK(fake_memory_u16(&g_mem, 0x20) == 0x1234u);
    CHECK(fake_memory_u8(&g_mem, 0x30) == 0xBB);

    /* UTF-8 is allowed inside comments only. */
    CHECK(parse("30000010 00AA # caf\xC3\xA9\n") == PSXCHEAT_OK);

    /* Empty and comment-only texts parse to an empty program that writes nothing. */
    fake_memory_reset(&g_mem);
    CHECK(parse("") == PSXCHEAT_OK && g_prog.count == 0);
    CHECK(psxcheat_parse(NULL, 0, &g_prog, &g_err) == PSXCHEAT_OK && g_prog.count == 0);
    CHECK(parse("# nothing\n\n;\n") == PSXCHEAT_OK && g_prog.count == 0);
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 0);
    CHECK(g_mem.write_calls == 0 && fake_memory_all_zero(&g_mem));
}

static void test_syntax_errors(void)
{
    g_test = "syntax";
    expect_parse_error("3000000 0000", PSXCHEAT_ERR_SYNTAX, 1, 1);        /* 7 digits */
    expect_parse_error("300000000 0000", PSXCHEAT_ERR_SYNTAX, 1, 1);      /* 9 digits */
    expect_parse_error("30000000", PSXCHEAT_ERR_SYNTAX, 1, 9);            /* no value */
    expect_parse_error("30000000   # c", PSXCHEAT_ERR_SYNTAX, 1, 12);     /* no value */
    expect_parse_error("30000000 000", PSXCHEAT_ERR_SYNTAX, 1, 10);       /* 3 digits */
    expect_parse_error("30000000 00000", PSXCHEAT_ERR_SYNTAX, 1, 10);     /* 5 digits */
    expect_parse_error("30000000 0000 0000", PSXCHEAT_ERR_SYNTAX, 1, 15); /* extra field */
    expect_parse_error("30000000-0000", PSXCHEAT_ERR_SYNTAX, 1, 1);       /* no separator */
    expect_parse_error("30000000:0000", PSXCHEAT_ERR_SYNTAX, 1, 1);
    expect_parse_error("3000000G 0000", PSXCHEAT_ERR_HEX, 1, 8);
    expect_parse_error("  30000000 00G0", PSXCHEAT_ERR_HEX, 1, 14);
    expect_parse_error("0x300000 0000", PSXCHEAT_ERR_HEX, 1, 2);
    /* The error names the right line among several. */
    expect_parse_error("30000000 0001\n# ok\n30000002 00Z1\n", PSXCHEAT_ERR_HEX, 3, 12);
    CHECK(strstr(g_err.message, "line 3") != NULL);
}

static void test_unsupported_types(void)
{
    static const char *const codes[] = {
        "D0000000 0001", "D1000000 0001", "50000A02 0001", "10000000 0001",
        "11000000 0001", "20000000 0001", "C0000000 0001", "E0000000 0001",
        "00000000 0000", "FF000000 0000", "A0000000 0000", "40000000 0000",
    };
    size_t i;
    g_test = "unsupported types";
    for (i = 0; i < sizeof codes / sizeof codes[0]; ++i)
        expect_parse_error(codes[i], PSXCHEAT_ERR_UNSUPPORTED_TYPE, 1, 1);
    expect_parse_error("  d0000000 0001", PSXCHEAT_ERR_UNSUPPORTED_TYPE, 1, 3);
    CHECK(strstr(g_err.message, "D0") != NULL);
}

static void test_xplorer_rejected(void)
{
    static const char *const codes[] = {
        "31000000 00AA", "37000000 00AA", "38000000 00AA", "3F000000 00AA",
        "81000000 1234", "88000000 1234", "8F000000 1234",
    };
    size_t i;
    g_test = "xplorer";
    for (i = 0; i < sizeof codes / sizeof codes[0]; ++i)
        expect_parse_error(codes[i], PSXCHEAT_ERR_XPLORER, 1, 1);
}

static void test_8bit_bounds(void)
{
    psxcheat_result res;
    g_test = "8-bit bounds";
    fake_memory_reset(&g_mem);
    CHECK(parse("30000000 0011\n301FFFFF 00AB\n") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 2);
    CHECK(fake_memory_u8(&g_mem, 0) == 0x11);
    CHECK(fake_memory_u8(&g_mem, 0x1FFFFF) == 0xAB);
    expect_parse_error("30200000 0001", PSXCHEAT_ERR_ADDRESS_RANGE, 1, 3);
    expect_parse_error("30FFFFFF 0001", PSXCHEAT_ERR_ADDRESS_RANGE, 1, 3);
    expect_parse_error("30400000 0001", PSXCHEAT_ERR_ADDRESS_RANGE, 1, 3);  /* mirror: not folded */
}

static void test_16bit_bounds_alignment(void)
{
    psxcheat_result res;
    g_test = "16-bit bounds/alignment";
    fake_memory_reset(&g_mem);
    CHECK(parse("80000000 1122\n801FFFFE A1B2\n") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 2);
    CHECK(fake_memory_u16(&g_mem, 0) == 0x1122u);
    CHECK(fake_memory_u8(&g_mem, 0x1FFFFE) == 0xB2);
    CHECK(fake_memory_u8(&g_mem, 0x1FFFFF) == 0xA1);
    expect_parse_error("80000001 0001", PSXCHEAT_ERR_ALIGNMENT, 1, 3);
    expect_parse_error("801FFFFF 0001", PSXCHEAT_ERR_ALIGNMENT, 1, 3);
    expect_parse_error("80200000 0001", PSXCHEAT_ERR_ADDRESS_RANGE, 1, 3);
    expect_parse_error("80FFFFFE 0001", PSXCHEAT_ERR_ADDRESS_RANGE, 1, 3);
}

static void test_value_width(void)
{
    g_test = "value width";
    expect_parse_error("30000000 0100", PSXCHEAT_ERR_VALUE_WIDTH, 1, 10);
    expect_parse_error("30000000 FFFF", PSXCHEAT_ERR_VALUE_WIDTH, 1, 10);
    expect_parse_error("30000000 FF00", PSXCHEAT_ERR_VALUE_WIDTH, 1, 10);
    CHECK(parse("30000000 00FF") == PSXCHEAT_OK && g_prog.count == 1);
    CHECK(parse("80000000 FFFF") == PSXCHEAT_OK && g_prog.count == 1);
}

static void test_order_and_overlap(void)
{
    psxcheat_result res;
    g_test = "order/overlap";
    fake_memory_reset(&g_mem);
    /* 16-bit then 8-bit into its low byte: the later line wins. */
    CHECK(parse("80000010 1234\n30000010 00FF\n80000020 AAAA\n80000020 BBBB\n") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 4);
    CHECK(fake_memory_u16(&g_mem, 0x10) == 0x12FFu);
    CHECK(fake_memory_u16(&g_mem, 0x20) == 0xBBBBu);
    /* The log follows source order exactly. */
    CHECK(g_mem.log_count == 4);
    CHECK(g_mem.log[0].offset == 0x10 && g_mem.log[0].width == 2 && g_mem.log[0].value == 0x1234);
    CHECK(g_mem.log[1].offset == 0x10 && g_mem.log[1].width == 1 && g_mem.log[1].value == 0xFF);
    CHECK(g_mem.log[2].value == 0xAAAA && g_mem.log[3].value == 0xBBBB);

    /* Reversed order gives the other result. */
    fake_memory_reset(&g_mem);
    CHECK(parse("30000010 00FF\n80000010 1234\n") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_OK);
    CHECK(fake_memory_u16(&g_mem, 0x10) == 0x1234u);

    /* Re-running a program (persistent cheats) is idempotent. */
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 2);
    CHECK(fake_memory_u16(&g_mem, 0x10) == 0x1234u);
    CHECK(g_mem.log_count == 4);
}

static void test_no_writes_on_failure(void)
{
    psxcheat_result res;
    const psxcheat_memory b = fake_memory_backend(&g_mem);
    g_test = "no writes on failure";

    /* Parse failure after valid lines: nothing is runnable. */
    fake_memory_reset(&g_mem);
    expect_parse_error("30000010 00AA\n80000020 1234\nD0000030 0001\n",
                       PSXCHEAT_ERR_UNSUPPORTED_TYPE, 3, 1);
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 0);
    CHECK(g_mem.write_calls == 0 && fake_memory_all_zero(&g_mem));

    /* Backend validation rejects the last line: no line is written. */
    fake_memory_reset(&g_mem);
    g_mem.reject_offset_set = 1;
    g_mem.reject_offset = 0x31;
    CHECK(parse("30000010 00AA\n80000020 1234\n80000030 5678\n") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_ERR_MEMORY_REJECTED);
    CHECK(g_err.line == 3);
    CHECK(res.writes == 0);
    CHECK(g_mem.write_calls == 0 && fake_memory_all_zero(&g_mem));
    CHECK(psxcheat_validate(&g_prog, &b, &g_err) == PSXCHEAT_ERR_MEMORY_REJECTED);
    CHECK(g_mem.write_calls == 0);

    /* A smaller RAM backend rejects spans beyond its end. */
    fake_memory_reset(&g_mem);
    g_mem.valid_limit = 0x1000;
    CHECK(parse("80000FFE 0001\n80001000 0001\n") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_ERR_MEMORY_REJECTED && g_err.line == 2);
    CHECK(g_mem.write_calls == 0);
}

static void test_injected_write_failure(void)
{
    psxcheat_result res;
    g_test = "write failure";
    fake_memory_reset(&g_mem);
    g_mem.fail_write_index = 1;  /* the second write fails */
    CHECK(parse("30000010 00AA\n# gap\n80000020 1234\n30000030 00BB\n") == PSXCHEAT_OK);
    CHECK(run(&res) == PSXCHEAT_ERR_MEMORY_WRITE);
    CHECK(g_err.status == PSXCHEAT_ERR_MEMORY_WRITE);
    CHECK(g_err.line == 3);
    CHECK(res.writes == 1);
    CHECK(g_mem.write_calls == 2);                 /* the third is never attempted */
    CHECK(fake_memory_u8(&g_mem, 0x10) == 0xAA);   /* earlier write remains */
    CHECK(fake_memory_u16(&g_mem, 0x20) == 0);
    CHECK(fake_memory_u8(&g_mem, 0x30) == 0);
    CHECK(strstr(g_err.message, "1 earlier") != NULL);

    /* Same, without result or error output. */
    {
        const psxcheat_memory b = fake_memory_backend(&g_mem);
        fake_memory_reset(&g_mem);
        g_mem.fail_write_index = 0;
        CHECK(psxcheat_execute(&g_prog, &b, NULL, NULL) == PSXCHEAT_ERR_MEMORY_WRITE);
        CHECK(fake_memory_all_zero(&g_mem));
    }
}

static void test_hand_built_programs(void)
{
    psxcheat_result res;
    g_test = "hand-built programs";

    fake_memory_reset(&g_mem);
    memset(&g_prog, 0, sizeof g_prog);
    g_prog.count = 1;
    g_prog.lines[0].op = 0;  /* unknown */
    g_prog.lines[0].source_line = 1;
    CHECK(run(&res) == PSXCHEAT_ERR_INVALID_PROGRAM);

    g_prog.lines[0].op = PSXCHEAT_OP_WRITE16;
    g_prog.lines[0].code_type = 0x80;
    g_prog.lines[0].offset = 0x1FFFFFu;  /* span leaves RAM and is odd */
    CHECK(run(&res) == PSXCHEAT_ERR_ADDRESS_RANGE);

    g_prog.lines[0].offset = 0xFFFFFFFFu;  /* offset + width would overflow */
    CHECK(run(&res) == PSXCHEAT_ERR_ADDRESS_RANGE);

    g_prog.lines[0].offset = 0x11u;
    CHECK(run(&res) == PSXCHEAT_ERR_ALIGNMENT);

    g_prog.lines[0].op = PSXCHEAT_OP_WRITE8;
    g_prog.lines[0].code_type = 0x30;
    g_prog.lines[0].value = 0x1FF;  /* does not fit 8 bits */
    CHECK(run(&res) == PSXCHEAT_ERR_INVALID_PROGRAM);

    g_prog.lines[0].code_type = 0x80;  /* op/type mismatch */
    g_prog.lines[0].value = 0x01;
    CHECK(run(&res) == PSXCHEAT_ERR_INVALID_PROGRAM);

    g_prog.count = PSXCHEAT_MAX_CODES + 1u;
    CHECK(run(&res) == PSXCHEAT_ERR_INVALID_PROGRAM);

    CHECK(g_mem.write_calls == 0 && fake_memory_all_zero(&g_mem));
}

static void test_limits(void)
{
    static char text[PSXCHEAT_MAX_TEXT + 64];
    size_t i, n;
    g_test = "limits";

    /* Text size. */
    memset(text, '#', sizeof text);
    CHECK(psxcheat_parse(text, PSXCHEAT_MAX_TEXT + 1u, &g_prog, &g_err) == PSXCHEAT_ERR_TEXT_TOO_LONG);
    CHECK(g_prog.count == 0);

    /* Line length: exactly the limit is fine, one more is not. */
    memset(text, ' ', PSXCHEAT_MAX_LINE);
    memcpy(text, "30000000 0001 #", 15);
    CHECK(psxcheat_parse(text, PSXCHEAT_MAX_LINE, &g_prog, &g_err) == PSXCHEAT_OK);
    CHECK(psxcheat_parse(text, PSXCHEAT_MAX_LINE + 1u, &g_prog, &g_err) == PSXCHEAT_ERR_LINE_TOO_LONG);
    CHECK(g_err.line == 1);

    /* Code count: exactly the limit is fine, one more is not. */
    n = 0;
    for (i = 0; i < PSXCHEAT_MAX_CODES; ++i)
        n += (size_t)snprintf(text + n, sizeof text - n, "30%06X 0001\n", (unsigned)i);
    CHECK(psxcheat_parse(text, n, &g_prog, &g_err) == PSXCHEAT_OK);
    CHECK(g_prog.count == PSXCHEAT_MAX_CODES);
    n += (size_t)snprintf(text + n, sizeof text - n, "30000000 0001\n");
    CHECK(psxcheat_parse(text, n, &g_prog, &g_err) == PSXCHEAT_ERR_TOO_MANY_CODES);
    CHECK(g_err.line == PSXCHEAT_MAX_CODES + 1u);
    CHECK(g_prog.count == 0);
}

static void test_invalid_characters(void)
{
    static const char nul_text[] = "30000000 00\0" "1";
    g_test = "invalid characters";
    CHECK(psxcheat_parse(nul_text, sizeof nul_text - 1, &g_prog, &g_err) == PSXCHEAT_ERR_INVALID_CHAR);
    CHECK(g_err.column == 12);
    expect_parse_error("30000000\r0001", PSXCHEAT_ERR_INVALID_CHAR, 1, 9);   /* lone CR */
    expect_parse_error("30000000 0001\x01", PSXCHEAT_ERR_INVALID_CHAR, 1, 14);
    expect_parse_error("3000000\xC3\xA9 0001", PSXCHEAT_ERR_INVALID_CHAR, 1, 8);
    expect_parse_error("30000000 0001 # bell\x07", PSXCHEAT_ERR_INVALID_CHAR, 1, 21);
}

static void test_arguments(void)
{
    psxcheat_result res;
    psxcheat_memory b;
    g_test = "arguments";
    CHECK(psxcheat_parse("30000000 0001", 13, NULL, &g_err) == PSXCHEAT_ERR_ARGUMENT);
    CHECK(psxcheat_parse(NULL, 5, &g_prog, &g_err) == PSXCHEAT_ERR_ARGUMENT);
    CHECK(psxcheat_parse("30000000 0001", 13, &g_prog, NULL) == PSXCHEAT_OK);
    CHECK(psxcheat_parse("bogus", 5, &g_prog, NULL) == PSXCHEAT_ERR_SYNTAX);

    fake_memory_reset(&g_mem);
    CHECK(parse("30000000 0001") == PSXCHEAT_OK);
    b = fake_memory_backend(&g_mem);
    CHECK(psxcheat_execute(NULL, &b, &res, &g_err) == PSXCHEAT_ERR_ARGUMENT);
    CHECK(psxcheat_execute(&g_prog, NULL, &res, &g_err) == PSXCHEAT_ERR_ARGUMENT);
    b.write16 = NULL;
    CHECK(psxcheat_execute(&g_prog, &b, &res, &g_err) == PSXCHEAT_ERR_ARGUMENT);
    b = fake_memory_backend(&g_mem);
    b.valid = NULL;
    CHECK(psxcheat_validate(&g_prog, &b, &g_err) == PSXCHEAT_ERR_ARGUMENT);
    /* The read callbacks are optional for 30/80. */
    b = fake_memory_backend(&g_mem);
    b.read8 = NULL;
    b.read16 = NULL;
    CHECK(psxcheat_execute(&g_prog, &b, &res, &g_err) == PSXCHEAT_OK);
    CHECK(g_mem.write_calls == 1);
}

/* Review follow-up F1: a CR is only accepted as part of CRLF. */
static void test_lone_cr(void)
{
    g_test = "lone CR";
    expect_parse_error("30000000 0001\r", PSXCHEAT_ERR_INVALID_CHAR, 1, 14);   /* at EOF */
    expect_parse_error("30000000 0001\r\n30000002 0002\r", PSXCHEAT_ERR_INVALID_CHAR, 2, 14);
    expect_parse_error("\r", PSXCHEAT_ERR_INVALID_CHAR, 1, 1);
    expect_parse_error("# comment\r", PSXCHEAT_ERR_INVALID_CHAR, 1, 10);
    expect_parse_error("30000000 0001\r\r\n", PSXCHEAT_ERR_INVALID_CHAR, 1, 14); /* CR CR LF */
    /* CRLF is still fine, also on the last line and on empty lines. */
    CHECK(parse("30000000 0001\r\n") == PSXCHEAT_OK && g_prog.count == 1);
    CHECK(parse("\r\n\r\n30000000 0001\r\n\r\n") == PSXCHEAT_OK && g_prog.count == 1);
    CHECK(g_prog.lines[0].source_line == 3);
}

/* Review follow-up F2: boundaries not covered before. */
static void test_more_boundaries(void)
{
    static char text[PSXCHEAT_MAX_TEXT + 64];
    psxcheat_result res;
    psxcheat_memory b;
    size_t i, n;
    g_test = "more boundaries";

    /* Exactly PSXCHEAT_MAX_TEXT bytes: 256 lines of 255 bytes + LF, codes in
     * the first and the very last line. */
    memset(text, '#', PSXCHEAT_MAX_TEXT);
    for (i = 0; i < 256u; ++i) text[i * 256u + 255u] = '\n';
    memset(text, ' ', 255);
    memcpy(text, "30000000 0001", 13);
    memset(text + 255u * 256u, ' ', 255);
    memcpy(text + 255u * 256u, "30000002 0002", 13);
    CHECK(psxcheat_parse(text, PSXCHEAT_MAX_TEXT, &g_prog, &g_err) == PSXCHEAT_OK);
    CHECK(g_prog.count == 2);
    CHECK(g_prog.lines[1].source_line == 256);
    CHECK(g_prog.lines[1].offset == 2);

    /* A 256-byte line (the limit, excluding EOL) followed by CRLF. */
    memset(text, ' ', PSXCHEAT_MAX_LINE);
    memcpy(text, "30000000 0001", 13);
    text[PSXCHEAT_MAX_LINE] = '\r';
    text[PSXCHEAT_MAX_LINE + 1u] = '\n';
    memcpy(text + PSXCHEAT_MAX_LINE + 2u, "30000002 0002", 13);
    CHECK(psxcheat_parse(text, PSXCHEAT_MAX_LINE + 2u + 13u, &g_prog, &g_err) == PSXCHEAT_OK);
    CHECK(g_prog.count == 2 && g_prog.lines[1].source_line == 2);
    /* 257 bytes before CRLF is too long; so is 256 bytes plus a lone CR at
     * the end, because that CR belongs to the line. */
    memset(text, ' ', PSXCHEAT_MAX_LINE + 1u);
    memcpy(text, "30000000 0001", 13);
    text[PSXCHEAT_MAX_LINE + 1u] = '\r';
    text[PSXCHEAT_MAX_LINE + 2u] = '\n';
    CHECK(psxcheat_parse(text, PSXCHEAT_MAX_LINE + 3u, &g_prog, &g_err) == PSXCHEAT_ERR_LINE_TOO_LONG);
    memset(text, ' ', PSXCHEAT_MAX_LINE);
    memcpy(text, "30000000 0001", 13);
    text[PSXCHEAT_MAX_LINE] = '\r';
    CHECK(psxcheat_parse(text, PSXCHEAT_MAX_LINE + 1u, &g_prog, &g_err) == PSXCHEAT_ERR_LINE_TOO_LONG);

    /* The error structure is reset by a successful call. */
    CHECK(parse("D0000000 0001") == PSXCHEAT_ERR_UNSUPPORTED_TYPE && g_err.line == 1);
    CHECK(parse("30000000 0001") == PSXCHEAT_OK);
    CHECK(g_err.status == PSXCHEAT_OK && g_err.line == 0 && g_err.column == 0);
    CHECK(g_err.message[0] == '\0');
    fake_memory_reset(&g_mem);
    g_mem.fail_write_index = 0;
    CHECK(run(&res) == PSXCHEAT_ERR_MEMORY_WRITE && g_err.line == 1);
    g_mem.fail_write_index = -1;
    CHECK(run(&res) == PSXCHEAT_OK && res.writes == 1);
    CHECK(g_err.status == PSXCHEAT_OK && g_err.line == 0 && g_err.message[0] == '\0');
    b = fake_memory_backend(&g_mem);
    CHECK(psxcheat_validate(&g_prog, &b, &g_err) == PSXCHEAT_OK && g_err.status == PSXCHEAT_OK);

    /* TOO_MANY_CODES points at the first field of the extra code. */
    n = 0;
    for (i = 0; i < PSXCHEAT_MAX_CODES; ++i)
        n += (size_t)snprintf(text + n, sizeof text - n, "30%06X 0001\n", (unsigned)i);
    n += (size_t)snprintf(text + n, sizeof text - n, "  30000000 0001\n");
    CHECK(psxcheat_parse(text, n, &g_prog, &g_err) == PSXCHEAT_ERR_TOO_MANY_CODES);
    CHECK(g_err.line == PSXCHEAT_MAX_CODES + 1u && g_err.column == 3);

    /* Successful execution with result == NULL. */
    fake_memory_reset(&g_mem);
    CHECK(parse("30000010 00AA\n80000020 1234\n") == PSXCHEAT_OK);
    b = fake_memory_backend(&g_mem);
    CHECK(psxcheat_execute(&g_prog, &b, NULL, &g_err) == PSXCHEAT_OK);
    CHECK(g_err.status == PSXCHEAT_OK);
    CHECK(g_mem.log_count == 2);
    CHECK(fake_memory_u8(&g_mem, 0x10) == 0xAA && fake_memory_u16(&g_mem, 0x20) == 0x1234u);
}

static void test_status_names(void)
{
    int s;
    g_test = "status names";
    for (s = PSXCHEAT_OK; s <= PSXCHEAT_ERR_MEMORY_WRITE; ++s) {
        const char *name = psxcheat_status_name((psxcheat_status)s);
        CHECK(name != NULL && name[0] != '\0' && strcmp(name, "unknown status") != 0);
    }
}

int main(void)
{
    test_valid_30_80();
    test_hex_case();
    test_whitespace_comments();
    test_syntax_errors();
    test_unsupported_types();
    test_xplorer_rejected();
    test_8bit_bounds();
    test_16bit_bounds_alignment();
    test_value_width();
    test_order_and_overlap();
    test_no_writes_on_failure();
    test_injected_write_failure();
    test_hand_built_programs();
    test_limits();
    test_invalid_characters();
    test_arguments();
    test_lone_cr();
    test_more_boundaries();
    test_status_names();
    printf("psxcheat: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
