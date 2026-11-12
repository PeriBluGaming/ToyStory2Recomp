/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3C.2B: the external cheat file loader (ts2_cheat_file.cpp) with the
 * real validator, production policies, table lifecycle and engine. Every
 * test file is created at run time inside the build tree
 * (TS2_CHEAT_FILE_TEST_DIR); nothing outside it is touched. psxrecomp
 * services are stubs; no guest memory. POSIX only (FIFO, symlink, chmod). */
#define _POSIX_C_SOURCE 200809L
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
#include "ts2_cheat_engine.h"
#include "ts2_cheat_external.h"
#include "ts2_cheat_file.h"
#include "ts2_cheat_memory.h"
#include "ts2_cheat_table.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

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

#define HEADER "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00893\"\n"
#define HEALTH_ENTRY "[[cheat]]\nid = \"health-14\"\nlabel = \"Full Health\"\n" \
                     "description = \"Keeps health at 14\"\ncode = [\"800B221E 000E\"]\n"
#define S(p) ((p) ? (p) : "")

static char g_dir[512];
static char g_path[640];
static ts2_cheat_file_report g_r;

static const char *path_of(const char *name)
{
    snprintf(g_path, sizeof g_path, "%s/%s", g_dir, name);
    return g_path;
}

static const char *write_bytes(const char *name, const void *data, size_t n)
{
    const char *p = path_of(name);
    FILE *f;
    unlink(p);
    f = fopen(p, "wb");
    if (!f) { printf("cannot create %s\n", p); exit(2); }
    if (n && fwrite(data, 1, n, f) != n) { printf("cannot write %s\n", p); exit(2); }
    fclose(f);
    return p;
}

static const char *write_text(const char *name, const char *text)
{
    return write_bytes(name, text, strlen(text));
}

/* A fresh, unsealed session window. */
static void fresh(void)
{
    ts2_cheat_combined_reset();
    CHECK(ts2_cheat_session_begin() == 1);
}

static ts2_cheat_file_status load_text(const char *text)
{
    fresh();
    return ts2_cheat_file_load(write_text("doc.toml", text), &g_r);
}

static size_t active_count(void)
{
    size_t n = 0;
    (void)ts2_cheat_active_table(&n);
    return n;
}

static const ts2_cheat_def *entry(size_t k)
{
    static const psxcheat_psx_range none = { 0u, 0u };
    static const ts2_cheat_def empty = { "", "", "", &none, 0u, "", NULL, NULL };
    size_t n = 0;
    const ts2_cheat_def *t = ts2_cheat_active_table(&n);
    CHECK(t != NULL && k < n);
    return t && k < n ? &t[k] : &empty;
}

/* Built-ins only and intact. */
static int builtins_only(void)
{
    size_t n = 0;
    const ts2_cheat_def *t = ts2_cheat_active_table(&n);
    return t && n == 2u && strcmp(S(t[0].id), "max-coins") == 0 &&
           strcmp(S(t[1].id), "infinite-lives") == 0 &&
           strcmp(S(t[0].code), "800B2226 0063") == 0 && ts2_cheat_active_external_count() == 0u;
}

/* ------------------------------------------------------------------------- */

static void test_valid(void)
{
    g_test = "valid";
    CHECK(load_text(HEADER HEALTH_ENTRY "enabled = false\n") == TS2_CHEAT_FILE_OK);
    CHECK(g_r.file == TS2_CHEAT_FILE_OK && g_r.build == TS2_CHEAT_BUILD_OK);
    CHECK(g_r.entries == 1u && g_r.validated == 1u && g_r.accepted == 1u);
    CHECK(g_r.shape[0] == TS2_CHEAT_ENTRY_OK && g_r.verdict[0] == TS2_CHEAT_EXT_OK);
    CHECK(active_count() == 3u);
    CHECK(strcmp(S(entry(2)->id), "health-14") == 0 && strcmp(S(entry(2)->label), "Full Health") == 0);
    CHECK(strcmp(S(entry(2)->off_hint), "Keeps health at 14") == 0);
    CHECK(strcmp(S(entry(2)->menu_key), "cheat_menu.ext.health-14") == 0);
    CHECK(entry(2)->range_count == 1u && entry(2)->ranges[0].start == 0x800B221Eu &&
          entry(2)->ranges[0].end == 0x800B2220u);
    CHECK(ts2_cheat_active_external_enabled(2) == 0);
    CHECK(g_r.bytes == strlen(HEADER HEALTH_ENTRY "enabled = false\n"));
    /* enabled = true; comments, CRLF, inline table form. */
    CHECK(load_text(HEADER HEALTH_ENTRY "enabled = true # on at start\n") == TS2_CHEAT_FILE_OK);
    CHECK(ts2_cheat_active_external_enabled(2) == 1);
    CHECK(load_text("# my cheats\r\nschema = \"toystory2recomp.cheats\"\r\nversion = 1\r\n"
                    "game = \"SLUS-00893\"\r\n[[cheat]]\r\nid = \"h\"\r\nlabel = \"H\"\r\n"
                    "code = [\r\n  \"800B221E 000E\", # health\r\n]\r\n") == TS2_CHEAT_FILE_OK);
    CHECK(g_r.accepted == 1u);
    CHECK(load_text(HEADER "cheat = [ { id = \"h\", label = \"H\", code = [\"800B221E 000E\"] } ]\n")
          == TS2_CHEAT_FILE_OK);
    CHECK(g_r.accepted == 1u && active_count() == 3u);
    /* Optional fields missing: no description, no enabled. */
    CHECK(load_text(HEADER "[[cheat]]\nid = \"h\"\nlabel = \"H\"\ncode = [\"800B221E 000E\"]\n")
          == TS2_CHEAT_FILE_OK);
    CHECK(entry(2)->off_hint == NULL && ts2_cheat_active_external_enabled(2) == 0);
    /* Header only, and a header with an empty cheat array. */
    CHECK(load_text(HEADER) == TS2_CHEAT_FILE_OK && g_r.entries == 0u && builtins_only());
    CHECK(load_text(HEADER "cheat = []\n") == TS2_CHEAT_FILE_OK && g_r.entries == 0u && builtins_only());
}

static void test_document_errors(void)
{
    static const struct { const char *text; ts2_cheat_file_status want; } cases[] = {
        { "", TS2_CHEAT_FILE_ERR_SCHEMA },
        { "version = 1\ngame = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_SCHEMA },
        { "schema = \"toystory2recomp.cheat\"\nversion = 1\ngame = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_SCHEMA },
        { "schema = 1\nversion = 1\ngame = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_SCHEMA },
        { "schema = \"toystory2recomp.cheats\"\ngame = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_VERSION },
        { "schema = \"toystory2recomp.cheats\"\nversion = 2\ngame = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_VERSION },
        { "schema = \"toystory2recomp.cheats\"\nversion = \"1\"\ngame = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_VERSION },
        { "schema = \"toystory2recomp.cheats\"\nversion = 1.0\ngame = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_VERSION },
        { "schema = \"toystory2recomp.cheats\"\nversion = 1\n", TS2_CHEAT_FILE_ERR_GAME },
        { "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLES-02505\"\n", TS2_CHEAT_FILE_ERR_GAME },
        { "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"slus-00893\"\n", TS2_CHEAT_FILE_ERR_GAME },
        { HEADER "author = \"me\"\n", TS2_CHEAT_FILE_ERR_STRUCTURE },
        { HEADER "[cheat]\nid = \"h\"\nlabel = \"H\"\ncode = [\"800B221E 000E\"]\n", TS2_CHEAT_FILE_ERR_STRUCTURE },
        { HEADER "cheat = \"x\"\n", TS2_CHEAT_FILE_ERR_STRUCTURE },
        { HEADER "[meta]\nx = 1\n", TS2_CHEAT_FILE_ERR_STRUCTURE },
        { "schema = \"toystory2recomp.cheats\"\nschema = \"toystory2recomp.cheats\"\nversion = 1\n"
          "game = \"SLUS-00893\"\n", TS2_CHEAT_FILE_ERR_SYNTAX },               /* duplicate key */
        { HEADER "[[cheat]]\nid = \"a\"\nid = \"b\"\nlabel = \"L\"\ncode = []\n", TS2_CHEAT_FILE_ERR_SYNTAX },
        { HEADER "[[cheat]\n", TS2_CHEAT_FILE_ERR_SYNTAX },
        { HEADER "x = = 1\n", TS2_CHEAT_FILE_ERR_SYNTAX },
        { "schema = \"toystory2recomp.cheats\nversion = 1\n", TS2_CHEAT_FILE_ERR_SYNTAX },
    };
    size_t i;
    g_test = "document errors";
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const ts2_cheat_file_status s = load_text(cases[i].text);
        CHECK(s == cases[i].want);
        if (s != cases[i].want) printf("    case %u: got %s\n", (unsigned)i, ts2_cheat_file_status_name(s));
        /* Built-ins only, built normally. */
        CHECK(g_r.build == TS2_CHEAT_BUILD_OK && builtins_only());
        CHECK(g_r.accepted == 0u && g_r.validated == 0u);
    }
    /* A syntax error reports a line, never content. */
    {
        char line[160];
        CHECK(load_text(HEADER "\n\nSECRETMARKER = = 7\n") == TS2_CHEAT_FILE_ERR_SYNTAX);
        CHECK(g_r.error_line == 6u);
        CHECK(ts2_cheat_file_describe(&g_r, line, sizeof line) > 0);
        CHECK(strstr(line, "SECRETMARKER") == NULL && strstr(line, "line 6") != NULL);
    }
}

static void test_entry_shapes(void)
{
    g_test = "entry shapes";
    /* Each broken entry is isolated; the valid one at the end is accepted. */
    CHECK(load_text(HEADER
        "[[cheat]]\nid = \"a\"\nlabel = \"A\"\ncode = [\"800B221E 000E\"]\ncolor = \"red\"\n"   /* 0 */
        "[[cheat]]\nlabel = \"B\"\ncode = [\"800B221E 000E\"]\n"                                 /* 1 */
        "[[cheat]]\nid = \"c\"\nlabel = \"C\"\n"                                                   /* 2 */
        "[[cheat]]\nid = \"d\"\nlabel = 4\ncode = [\"800B221E 000E\"]\n"                         /* 3 */
        "[[cheat]]\nid = \"e\"\nlabel = \"E\"\ncode = \"800B221E 000E\"\n"                       /* 4 */
        "[[cheat]]\nid = \"f\"\nlabel = \"F\"\ncode = [1]\n"                                       /* 5 */
        "[[cheat]]\nid = \"g\"\nlabel = \"G\"\ncode = [\"800B221E 000E\"]\nenabled = \"yes\"\n"  /* 6 */
        "[[cheat]]\nid = \"h\\u0000x\"\nlabel = \"H\"\ncode = [\"800B221E 000E\"]\n"             /* 7 */
        "[[cheat]]\nid = \"i\"\nlabel = \"I\"\ndescription = 5\ncode = [\"800B221E 000E\"]\n"    /* 8 */
        "[[cheat]]\nid = \"j\"\nlabel = \"J\"\ncode = [\"800B221E\\u0000 000E\"]\n"              /* 9 */
        "[[cheat]]\nid = \"ok\"\nlabel = \"Ok\"\ncode = [\"800B221E 000E\"]\n")                  /* 10 */
        == TS2_CHEAT_FILE_OK);
    CHECK(g_r.entries == 11u && g_r.validated == 1u && g_r.accepted == 1u);
    CHECK(g_r.shape[0] == TS2_CHEAT_ENTRY_UNKNOWN_KEY);
    CHECK(g_r.shape[1] == TS2_CHEAT_ENTRY_MISSING_KEY);
    CHECK(g_r.shape[2] == TS2_CHEAT_ENTRY_MISSING_KEY);
    CHECK(g_r.shape[3] == TS2_CHEAT_ENTRY_WRONG_TYPE);
    CHECK(g_r.shape[4] == TS2_CHEAT_ENTRY_WRONG_TYPE);
    CHECK(g_r.shape[5] == TS2_CHEAT_ENTRY_CODE_ELEMENT);
    CHECK(g_r.shape[6] == TS2_CHEAT_ENTRY_WRONG_TYPE);
    CHECK(g_r.shape[7] == TS2_CHEAT_ENTRY_NUL_IN_STRING);
    CHECK(g_r.shape[8] == TS2_CHEAT_ENTRY_WRONG_TYPE);
    CHECK(g_r.shape[9] == TS2_CHEAT_ENTRY_NUL_IN_STRING);
    CHECK(g_r.shape[10] == TS2_CHEAT_ENTRY_OK && g_r.verdict[10] == TS2_CHEAT_EXT_OK);
    CHECK(g_r.verdict[0] == TS2_CHEAT_EXT_ERR_NOT_CHECKED);
    CHECK(active_count() == 3u && strcmp(S(entry(2)->id), "ok") == 0);
    /* Non-table elements next to a valid table. */
    CHECK(load_text(HEADER "cheat = [ \"x\", 3, { id = \"t\", label = \"T\", code = [\"800B221E 000E\"] } ]\n")
          == TS2_CHEAT_FILE_OK);
    CHECK(g_r.shape[0] == TS2_CHEAT_ENTRY_NOT_TABLE && g_r.shape[1] == TS2_CHEAT_ENTRY_NOT_TABLE);
    CHECK(g_r.shape[2] == TS2_CHEAT_ENTRY_OK && g_r.accepted == 1u);
    {
        char line[96];
        CHECK(ts2_cheat_file_describe_entry(&g_r, 0, line, sizeof line) > 0);
        CHECK(strcmp(line, "cheat entry 1: not a table") == 0);
        CHECK(ts2_cheat_file_describe_entry(&g_r, 2, line, sizeof line) > 0);
        CHECK(strcmp(line, "cheat entry 3: ok") == 0);
        CHECK(ts2_cheat_file_describe(&g_r, line, sizeof line) > 0);
        CHECK(strcmp(line, "cheat file: ok, 3 entries, 1 accepted, 2 rejected") == 0);
    }
}

static void test_validator_decisions(void)
{
    g_test = "validator";
    /* Duplicate ids, overlaps, forbidden and malformed codes: all the
     * validator's verdicts, mapped back to file entries. */
    CHECK(load_text(HEADER
        "[[cheat]]\nid = \"dup\"\nlabel = \"A\"\ncode = [\"300B221E 000E\"]\n"   /* 0 dup */
        "[[cheat]]\nid = \"dup\"\nlabel = \"B\"\ncode = [\"300B221F 0000\"]\n"   /* 1 dup */
        "[[cheat]]\nid = \"coins\"\nlabel = \"C\"\ncode = [\"800B2226 0032\"]\n" /* 2 built-in */
        "[[cheat]]\nid = \"timer\"\nlabel = \"T\"\ncode = [\"800B2220 0384\"]\n" /* 3 target */
        "[[cheat]]\nid = \"kernel\"\nlabel = \"K\"\ncode = [\"30000100 0001\"]\n"/* 4 target */
        "[[cheat]]\nid = \"odd\"\nlabel = \"O\"\ncode = [\"800B2221 000F\"]\n"   /* 5 parse */
        "[[cheat]]\nid = \"cond\"\nlabel = \"D\"\ncode = [\"D00B221E 0001\"]\n"  /* 6 parse */
        "[[cheat]]\nid = \"max-coins\"\nlabel = \"M\"\ncode = [\"800B221E 000E\"]\n" /* 7 builtin id */
        "[[cheat]]\nid = \"UPPER\"\nlabel = \"U\"\ncode = [\"800B221E 000E\"]\n" /* 8 id */
        "[[cheat]]\nid = \"utf\"\nlabel = \"Gr\xc3\xbcne\"\ncode = [\"800B221E 000E\"]\n" /* 9 label */
        "[[cheat]]\nid = \"none\"\nlabel = \"N\"\ncode = []\n")                   /* 10 code */
        == TS2_CHEAT_FILE_OK);
    CHECK(g_r.validated == 11u && g_r.accepted == 0u);
    CHECK(g_r.verdict[0] == TS2_CHEAT_EXT_ERR_ID_DUPLICATE && g_r.verdict[1] == TS2_CHEAT_EXT_ERR_ID_DUPLICATE);
    CHECK(g_r.verdict[2] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    CHECK(g_r.verdict[3] == TS2_CHEAT_EXT_ERR_TARGET && g_r.verdict[4] == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(g_r.verdict[5] == TS2_CHEAT_EXT_ERR_PARSE && g_r.verdict[6] == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(g_r.verdict[7] == TS2_CHEAT_EXT_ERR_ID_BUILTIN && g_r.verdict[8] == TS2_CHEAT_EXT_ERR_ID);
    CHECK(g_r.verdict[9] == TS2_CHEAT_EXT_ERR_LABEL && g_r.verdict[10] == TS2_CHEAT_EXT_ERR_CODE);
    CHECK(builtins_only());
    /* Two external health cheats overlap: both rejected. */
    CHECK(load_text(HEADER HEALTH_ENTRY
        "[[cheat]]\nid = \"half\"\nlabel = \"Half\"\ncode = [\"800B221E 0007\"]\n") == TS2_CHEAT_FILE_OK);
    CHECK(g_r.verdict[0] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL &&
          g_r.verdict[1] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL && builtins_only());
    /* 000F: address authorised, no value restriction (documented). */
    CHECK(load_text(HEADER "[[cheat]]\nid = \"f\"\nlabel = \"F\"\ncode = [\"800B221E 000F\"]\n")
          == TS2_CHEAT_FILE_OK && g_r.accepted == 1u);
    /* 33 code lines: too many. */
    {
        char doc[4096];
        size_t k, used;
        used = (size_t)snprintf(doc, sizeof doc, "%s[[cheat]]\nid = \"m\"\nlabel = \"M\"\ncode = [", HEADER);
        for (k = 0; k < 33; ++k)
            used += (size_t)snprintf(doc + used, sizeof doc - used, "\"300B221E 000E\",");
        snprintf(doc + used, sizeof doc - used, "]\n");
        CHECK(load_text(doc) == TS2_CHEAT_FILE_OK && g_r.verdict[0] == TS2_CHEAT_EXT_ERR_CODE);
    }
    /* More than 64 entries: the rest are not examined. */
    {
        static char doc[20000];
        size_t k, used = (size_t)snprintf(doc, sizeof doc, "%s", HEADER);
        char line[96];
        for (k = 0; k < 70; ++k)
            used += (size_t)snprintf(doc + used, sizeof doc - used,
                                     "[[cheat]]\nid = \"e%u\"\nlabel = \"\"\ncode = [\"300B221E 000E\"]\n",
                                     (unsigned)k);
        CHECK(load_text(doc) == TS2_CHEAT_FILE_OK);
        CHECK(g_r.entries == 70u && g_r.validated == 64u);
        CHECK(ts2_cheat_file_describe_entry(&g_r, 66, line, sizeof line) > 0);
        CHECK(strcmp(line, "cheat entry 67: no free cheat slot") == 0);
        CHECK(ts2_cheat_file_describe_entry(&g_r, 70, line, sizeof line) > 0 &&
              strcmp(line, "cheat entry: none") == 0);
    }
}

static void test_encoding(void)
{
    static const struct { const char *bytes; size_t n; } bad[] = {
        { HEADER "x = \"\xC3\x28\"\n", sizeof(HEADER "x = \"\xC3\x28\"\n") - 1 },          /* bad continuation */
        { HEADER "x = \"\xC0\xAF\"\n", sizeof(HEADER "x = \"\xC0\xAF\"\n") - 1 },          /* overlong */
        { HEADER "x = \"\xE0\x80\xAF\"\n", sizeof(HEADER "x = \"\xE0\x80\xAF\"\n") - 1 },  /* overlong 3 */
        { HEADER "x = \"\xED\xA0\x80\"\n", sizeof(HEADER "x = \"\xED\xA0\x80\"\n") - 1 },  /* surrogate */
        { HEADER "x = \"\xF4\x90\x80\x80\"\n", sizeof(HEADER "x = \"\xF4\x90\x80\x80\"\n") - 1 }, /* > U+10FFFF */
        { HEADER "x = \"\xFF\"\n", sizeof(HEADER "x = \"\xFF\"\n") - 1 },
        { HEADER "x = \"\xE2\x82", sizeof(HEADER "x = \"\xE2\x82") - 1 },                  /* truncated */
        { HEADER "\0", sizeof(HEADER "\0") - 1 },                                         /* NUL */
        { "sch\0ema", 7 },
    };
    static const char bom_doc[] = "\xEF\xBB\xBF" HEADER HEALTH_ENTRY;
    size_t i;
    g_test = "encoding";
    for (i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        fresh();
        CHECK(ts2_cheat_file_load(write_bytes("enc.toml", bad[i].bytes, bad[i].n), &g_r) ==
              TS2_CHEAT_FILE_ERR_ENCODING);
        CHECK(builtins_only());
    }
    /* One BOM is removed; valid UTF-8 in a comment and a string is fine. */
    fresh();
    CHECK(ts2_cheat_file_load(write_bytes("bom.toml", bom_doc, sizeof bom_doc - 1), &g_r) ==
          TS2_CHEAT_FILE_OK);
    CHECK(g_r.accepted == 1u && g_r.bytes == sizeof bom_doc - 1);
    CHECK(load_text("# Gr\xc3\xbc\xc3\x9f\xe2\x82\xac \xf0\x9f\x8e\xae\n" HEADER HEALTH_ENTRY) ==
          TS2_CHEAT_FILE_OK && g_r.accepted == 1u);
    /* BOM policy: zero or one leading BOM; two or more are refused (toml11
     * would silently skip a second one). A BOM later in the text is
     * ordinary content (TOML decides). */
    {
        static const char two[] = "\xEF\xBB\xBF\xEF\xBB\xBF" HEADER HEALTH_ENTRY;
        static const char three[] = "\xEF\xBB\xBF\xEF\xBB\xBF\xEF\xBB\xBF" HEADER;
        static const char only_two[] = "\xEF\xBB\xBF\xEF\xBB\xBF";
        static const char only_one[] = "\xEF\xBB\xBF";
        static const char later[] = HEADER "\xEF\xBB\xBF" HEALTH_ENTRY;
        static const char in_string[] = HEADER "[[cheat]]\nid = \"b\"\nlabel = \"B\"\n"
            "description = \"\xEF\xBB\xBF\"\ncode = [\"800B221E 000E\"]\n";
        fresh();
        CHECK(ts2_cheat_file_load(write_bytes("bom2.toml", two, sizeof two - 1), &g_r) ==
              TS2_CHEAT_FILE_ERR_ENCODING);
        CHECK(builtins_only());
        fresh();
        CHECK(ts2_cheat_file_load(write_bytes("bom3.toml", three, sizeof three - 1), &g_r) ==
              TS2_CHEAT_FILE_ERR_ENCODING);
        fresh();
        CHECK(ts2_cheat_file_load_buffer((const unsigned char *)only_two, sizeof only_two - 1, &g_r) ==
              TS2_CHEAT_FILE_ERR_ENCODING);
        fresh();
        CHECK(ts2_cheat_file_load_buffer((const unsigned char *)only_one, sizeof only_one - 1, &g_r) ==
              TS2_CHEAT_FILE_ERR_SCHEMA);   /* empty document */
        fresh();
        CHECK(ts2_cheat_file_load_buffer((const unsigned char *)later, sizeof later - 1, &g_r) ==
              TS2_CHEAT_FILE_ERR_SYNTAX);
        fresh();
        CHECK(ts2_cheat_file_load_buffer((const unsigned char *)in_string, sizeof in_string - 1, &g_r) ==
              TS2_CHEAT_FILE_OK);
        CHECK(g_r.shape[0] == TS2_CHEAT_ENTRY_OK && g_r.verdict[0] == TS2_CHEAT_EXT_ERR_DESCRIPTION);
    }
}

static void test_size_and_nesting(void)
{
    static unsigned char big[TS2_CHEAT_FILE_MAX_BYTES + 2u];
    size_t base = strlen(HEADER);
    g_test = "size / nesting";
    /* Exactly the limit: header, then a comment filling the rest. */
    memcpy(big, HEADER, base);
    big[base] = '#';
    memset(big + base + 1, 'x', TS2_CHEAT_FILE_MAX_BYTES - base - 2u);
    big[TS2_CHEAT_FILE_MAX_BYTES - 1u] = '\n';
    fresh();
    CHECK(ts2_cheat_file_load(write_bytes("max.toml", big, TS2_CHEAT_FILE_MAX_BYTES), &g_r) ==
          TS2_CHEAT_FILE_OK);
    CHECK(g_r.bytes == TS2_CHEAT_FILE_MAX_BYTES);
    fresh();
    CHECK(ts2_cheat_file_load_buffer(big, TS2_CHEAT_FILE_MAX_BYTES, &g_r) == TS2_CHEAT_FILE_OK);
    /* One byte more. */
    big[TS2_CHEAT_FILE_MAX_BYTES - 1u] = 'x';
    big[TS2_CHEAT_FILE_MAX_BYTES] = '\n';
    fresh();
    CHECK(ts2_cheat_file_load(write_bytes("over.toml", big, TS2_CHEAT_FILE_MAX_BYTES + 1u), &g_r) ==
          TS2_CHEAT_FILE_ERR_TOO_LARGE);
    CHECK(builtins_only());
    fresh();
    CHECK(ts2_cheat_file_load_buffer(big, TS2_CHEAT_FILE_MAX_BYTES + 1u, &g_r) ==
          TS2_CHEAT_FILE_ERR_TOO_LARGE);
    /* Nesting: 4 is fine (toml decides), 5 is refused before toml11. */
    CHECK(load_text(HEADER "x = [[[[1]]]]\n") == TS2_CHEAT_FILE_ERR_STRUCTURE);
    CHECK(load_text(HEADER "x = [[[[[1]]]]]\n") == TS2_CHEAT_FILE_ERR_NESTING);
    CHECK(load_text(HEADER "x = {a={b={c={d={}}}}}\n") == TS2_CHEAT_FILE_ERR_NESTING);
    /* 64 KiB of brackets: refused quickly, no recursion. */
    memset(big, '[', TS2_CHEAT_FILE_MAX_BYTES);
    fresh();
    CHECK(ts2_cheat_file_load_buffer(big, TS2_CHEAT_FILE_MAX_BYTES, &g_r) == TS2_CHEAT_FILE_ERR_NESTING);
    memset(big, '{', TS2_CHEAT_FILE_MAX_BYTES);
    fresh();
    CHECK(ts2_cheat_file_load_buffer(big, TS2_CHEAT_FILE_MAX_BYTES, &g_r) == TS2_CHEAT_FILE_ERR_NESTING);
    CHECK(builtins_only());
    /* Dotted keys / headers nest one table per dot (toml11 recursion):
     * at most TS2_CHEAT_FILE_MAX_DOTS unquoted dots per line. */
    CHECK(load_text(HEADER "a.b.c.d.e = 1\n") == TS2_CHEAT_FILE_ERR_STRUCTURE);   /* 4 dots */
    CHECK(load_text(HEADER "a.b.c.d.e.f = 1\n") == TS2_CHEAT_FILE_ERR_NESTING);   /* 5 dots */
    CHECK(load_text(HEADER "a . b . c . d . e . f = 1\n") == TS2_CHEAT_FILE_ERR_NESTING);
    CHECK(load_text(HEADER "\"a\".\"b\".\"c\".\"d\".\"e\".\"f\" = 1\n") == TS2_CHEAT_FILE_ERR_NESTING);
    CHECK(load_text(HEADER "[a.b.c.d.e.f]\n") == TS2_CHEAT_FILE_ERR_NESTING);
    CHECK(load_text(HEADER "x = [1.5, 2.5, 3.5, 4.5, 5.5]\n") == TS2_CHEAT_FILE_ERR_NESTING);
    CHECK(load_text(HEADER "x = 1.5\ny = 2.5\nz = 3.5\nw = 4.5\nv = 5.5\n") == TS2_CHEAT_FILE_ERR_STRUCTURE);
    CHECK(load_text("schema = \"toystory2recomp.cheats\"\nversion = 1.0\ngame = \"SLUS-00893\"\n") ==
          TS2_CHEAT_FILE_ERR_VERSION);
    /* Dots in strings and comments do not count. */
    CHECK(load_text(HEADER "# a.b.c.d.e.f.g.h\n[[cheat]]\nid = \"d\"\nlabel = \"v1.2.3.4.5.6\"\n"
                    "description = 'x.y.z.w.v.u.t'\ncode = [\"800B221E 000E\"]\n") == TS2_CHEAT_FILE_OK);
    CHECK(g_r.shape[0] == TS2_CHEAT_ENTRY_OK);
    /* A 64 KiB dotted key / header: refused before toml11 (which would
     * overflow the stack). */
    {
        size_t n = strlen(HEADER), k;
        memcpy(big, HEADER, n);
        for (k = n; k + 2 < TS2_CHEAT_FILE_MAX_BYTES; k += 2) { big[k] = 'a'; big[k + 1] = '.'; }
        big[k - 1] = '=';
        big[k] = '1';
        fresh();
        CHECK(ts2_cheat_file_load_buffer(big, k + 1, &g_r) == TS2_CHEAT_FILE_ERR_NESTING);
        memcpy(big, HEADER, n);
        big[n] = '[';
        for (k = n + 1; k + 2 < TS2_CHEAT_FILE_MAX_BYTES; k += 2) { big[k] = 'a'; big[k + 1] = '.'; }
        big[k - 1] = ']';
        fresh();
        CHECK(ts2_cheat_file_load_buffer(big, k, &g_r) == TS2_CHEAT_FILE_ERR_NESTING);
        CHECK(builtins_only());
    }
    /* Brackets inside strings and comments do not count. */
    CHECK(load_text("# [[[[[[[[[[\n" HEADER
                    "[[cheat]]\nid = \"b\"\nlabel = \"[[[[[[ {{{{{\"\ndescription = '[[[[[[[['\n"
                    "code = [\"800B221E 000E\"] # ]]]]]] [[[[[[\n") == TS2_CHEAT_FILE_OK);
    CHECK(g_r.accepted == 1u);
    CHECK(load_text(HEADER "x = \"\"\"\n[[[[[[[[\n\"\"\"\n") == TS2_CHEAT_FILE_ERR_STRUCTURE);
    CHECK(load_text(HEADER "x = '''[[[[[[[['''\n") == TS2_CHEAT_FILE_ERR_STRUCTURE);
    CHECK(load_text(HEADER "x = \"\\\"[[[[[[[\"\n") == TS2_CHEAT_FILE_ERR_STRUCTURE);   /* escaped quote */
    /* Empty buffer and NULL buffer. */
    fresh();
    CHECK(ts2_cheat_file_load_buffer(NULL, 0, &g_r) == TS2_CHEAT_FILE_ERR_SCHEMA);
    fresh();
    CHECK(ts2_cheat_file_load_buffer(NULL, 5, &g_r) == TS2_CHEAT_FILE_ERR_READ && builtins_only());
}

static void test_file_types(void)
{
    char target[640], link[640];
    struct timespec t0, t1;
    g_test = "file types";
    /* No path. */
    fresh();
    CHECK(ts2_cheat_file_load(NULL, &g_r) == TS2_CHEAT_FILE_NONE && builtins_only());
    fresh();
    CHECK(ts2_cheat_file_load("", &g_r) == TS2_CHEAT_FILE_NONE && g_r.build == TS2_CHEAT_BUILD_OK);
    /* Missing file. */
    fresh();
    CHECK(ts2_cheat_file_load(path_of("does-not-exist.toml"), &g_r) == TS2_CHEAT_FILE_ERR_OPEN);
    CHECK(builtins_only());
    /* Directory. */
    mkdir(path_of("a-directory.toml"), 0755);
    fresh();
    CHECK(ts2_cheat_file_load(path_of("a-directory.toml"), &g_r) == TS2_CHEAT_FILE_ERR_NOT_REGULAR);
    /* FIFO without a writer: refused at once, never blocks. */
    unlink(path_of("a-fifo.toml"));
    CHECK(mkfifo(path_of("a-fifo.toml"), 0644) == 0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    fresh();
    CHECK(ts2_cheat_file_load(path_of("a-fifo.toml"), &g_r) == TS2_CHEAT_FILE_ERR_NOT_REGULAR);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    CHECK(t1.tv_sec - t0.tv_sec < 2);
    /* Device. */
    fresh();
    CHECK(ts2_cheat_file_load("/dev/null", &g_r) == TS2_CHEAT_FILE_ERR_NOT_REGULAR);
    /* Symlinks: to a regular file fine, to a directory or dangling refused. */
    snprintf(target, sizeof target, "%s", write_text("target.toml", HEADER HEALTH_ENTRY));
    snprintf(link, sizeof link, "%s", path_of("link.toml"));
    unlink(link);
    CHECK(symlink(target, link) == 0);
    fresh();
    CHECK(ts2_cheat_file_load(link, &g_r) == TS2_CHEAT_FILE_OK && g_r.accepted == 1u);
    snprintf(link, sizeof link, "%s", path_of("link-dir.toml"));
    unlink(link);
    CHECK(symlink(path_of("a-directory.toml"), link) == 0);
    fresh();
    CHECK(ts2_cheat_file_load(link, &g_r) == TS2_CHEAT_FILE_ERR_NOT_REGULAR);
    snprintf(link, sizeof link, "%s", path_of("link-dangling.toml"));
    unlink(link);
    CHECK(symlink(path_of("nowhere.toml"), link) == 0);
    fresh();
    CHECK(ts2_cheat_file_load(link, &g_r) == TS2_CHEAT_FILE_ERR_OPEN);
    /* Unreadable (not meaningful as root). */
    if (geteuid() != 0) {
        const char *p = write_text("locked.toml", HEADER HEALTH_ENTRY);
        chmod(p, 0000);
        fresh();
        CHECK(ts2_cheat_file_load(p, &g_r) == TS2_CHEAT_FILE_ERR_OPEN);
        chmod(p, 0644);
    }
    CHECK(builtins_only());
}

static void test_no_modification(void)
{
    struct stat before, after;
    unsigned char a[512], b[512];
    size_t na, nb;
    FILE *f;
    const char *p;
    int k;
    g_test = "no modification";
    p = write_text("readonly.toml", HEADER HEALTH_ENTRY);
    f = fopen(p, "rb"); na = fread(a, 1, sizeof a, f); fclose(f);
    CHECK(stat(p, &before) == 0);
    sleep(1);   /* an mtime change would be visible */
    for (k = 0; k < 3; ++k) {
        fresh();
        CHECK(ts2_cheat_file_load(p, &g_r) == TS2_CHEAT_FILE_OK);
    }
    CHECK(stat(p, &after) == 0);
    f = fopen(p, "rb"); nb = fread(b, 1, sizeof b, f); fclose(f);
    CHECK(na == nb && memcmp(a, b, na) == 0);
    CHECK(before.st_size == after.st_size && before.st_mode == after.st_mode);
    CHECK(before.st_mtime == after.st_mtime && before.st_ino == after.st_ino);
    /* A read-only file loads (no write access is ever requested). */
    chmod(p, 0444);
    fresh();
    CHECK(ts2_cheat_file_load(p, &g_r) == TS2_CHEAT_FILE_OK);
    chmod(p, 0644);
}

static void test_lifecycle(void)
{
    size_t n = 0, n2 = 0;
    const ts2_cheat_def *t;
    char pa_copy[640], pb[640];
    g_test = "lifecycle";
    snprintf(pa_copy, sizeof pa_copy, "%s", write_text("session-a.toml", HEADER HEALTH_ENTRY));
    snprintf(pb, sizeof pb, "%s", write_text("session-b.toml", HEADER
             "[[cheat]]\nid = \"half\"\nlabel = \"Half Health\"\ncode = [\"800B221E 0007\"]\nenabled = true\n"));
    /* Session 1: load, then the engine seals. */
    fresh();
    CHECK(ts2_cheat_file_load(pa_copy, &g_r) == TS2_CHEAT_FILE_OK);
    ts2_cheat_session_seal();
    t = ts2_cheat_active_table(&n);
    CHECK(n == 3u && strcmp(S(t[2].id), "health-14") == 0);
    /* Sealed: refused, nothing changes, the file is not even opened. */
    CHECK(ts2_cheat_file_load(pb, &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(g_r.build == TS2_CHEAT_BUILD_ERR_SEALED && g_r.bytes == 0u && g_r.entries == 0u);
    CHECK(ts2_cheat_file_load(path_of("does-not-exist.toml"), &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(ts2_cheat_file_load(NULL, &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(ts2_cheat_file_load_buffer((const unsigned char *)"x", 1, &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(ts2_cheat_active_table(&n2) == t && n2 == 3u && strcmp(S(t[2].id), "health-14") == 0);
    /* Referenced by the menu (even unsealed): refused too. */
    ts2_cheat_table_ref_acquire();
    CHECK(ts2_cheat_session_begin() == 0);
    CHECK(ts2_cheat_file_load(pb, &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(ts2_cheat_active_table(&n2) == t && n2 == 3u);
    ts2_cheat_table_ref_release();
    /* Session 2: another file. */
    CHECK(ts2_cheat_session_begin() == 1);
    CHECK(ts2_cheat_file_load(pb, &g_r) == TS2_CHEAT_FILE_OK && g_r.accepted == 1u);
    t = ts2_cheat_active_table(&n);
    CHECK(n == 3u && strcmp(S(t[2].id), "half") == 0 && ts2_cheat_active_external_enabled(2) == 1);
    ts2_cheat_session_seal();
    /* Session 3: a broken file -> built-ins only (externals gone). */
    CHECK(ts2_cheat_session_begin() == 1);
    CHECK(ts2_cheat_file_load(write_text("broken.toml", "not toml at all ]["), &g_r) ==
          TS2_CHEAT_FILE_ERR_SYNTAX);
    CHECK(g_r.build == TS2_CHEAT_BUILD_OK && builtins_only());
    /* Session 4: no file. */
    ts2_cheat_session_seal();
    CHECK(ts2_cheat_session_begin() == 1);
    CHECK(ts2_cheat_file_load(NULL, &g_r) == TS2_CHEAT_FILE_NONE && builtins_only());
    /* Sealed: a broken file or buffer is refused before anything is read,
     * so the fallback build cannot replace the active table either. */
    fresh();
    CHECK(ts2_cheat_file_load(pa_copy, &g_r) == TS2_CHEAT_FILE_OK);
    ts2_cheat_session_seal();
    t = ts2_cheat_active_table(&n);
    CHECK(ts2_cheat_file_load(write_text("broken2.toml", "][ not toml"), &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(ts2_cheat_file_load_buffer((const unsigned char *)"\xFF\xFE", 2, &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(g_r.build == TS2_CHEAT_BUILD_ERR_SEALED);
    CHECK(ts2_cheat_active_table(&n2) == t && n2 == 3u && strcmp(S(t[2].id), "health-14") == 0);
    /* Accepted entries own their data: the file is gone, a failed load ran,
     * the parser's memory is long freed -- the table is intact (ASan would
     * report a use-after-free here). */
    CHECK(unlink(pa_copy) == 0);
    CHECK(ts2_cheat_file_load(pa_copy, &g_r) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(strcmp(S(t[2].id), "health-14") == 0 && strcmp(S(t[2].label), "Full Health") == 0);
    CHECK(strcmp(S(t[2].off_hint), "Keeps health at 14") == 0 && strcmp(S(t[2].code), "800B221E 000E\n") == 0);
    CHECK(strcmp(S(t[2].menu_key), "cheat_menu.ext.health-14") == 0 && t[2].ranges[0].start == 0x800B221Eu);
    snprintf(pa_copy, sizeof pa_copy, "%s", write_text("session-a.toml", HEADER HEALTH_ENTRY));
    /* report may be NULL. */
    fresh();
    CHECK(ts2_cheat_file_load(pa_copy, NULL) == TS2_CHEAT_FILE_OK && active_count() == 3u);
}

static void test_engine(void)
{
    size_t n = 0, nv = 0;
    const ts2_cheat_def *t;
    const psxcheat_psx_range *v;
    int on[TS2_CHEAT_MAX] = { 0 };
    g_test = "engine";
    CHECK(load_text(HEADER HEALTH_ENTRY "enabled = true\n") == TS2_CHEAT_FILE_OK);
    t = ts2_cheat_active_table(&n);
    v = ts2_cheat_active_verified(&nv);
    on[2] = ts2_cheat_active_external_enabled(2);
    stub_reset();
    CHECK(ts2_cheat_engine_init_table(t, n, v, nv, on) == TS2_CHEAT_TABLE_OK);
    CHECK(ts2_cheat_engine_count() == 3u && ts2_cheat_engine_state_at(2) == TS2_CHEAT_ARMED);
    g_stub.ram[0x0B221E] = 3;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 1u && g_stub.log[0].address == 0x800B221Eu && g_stub.log[0].value == 14u);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_OFF && ts2_cheat_engine_state_at(1) == TS2_CHEAT_OFF);
    ts2_cheat_engine_shutdown();
}

static void test_names(void)
{
    char line[64];
    g_test = "names";
    CHECK(strcmp(ts2_cheat_file_status_name(TS2_CHEAT_FILE_ERR_NESTING), "nesting too deep") == 0);
    CHECK(strcmp(ts2_cheat_entry_shape_name(TS2_CHEAT_ENTRY_CODE_ELEMENT), "code element is not a string") == 0);
    CHECK(ts2_cheat_file_describe(NULL, line, sizeof line) > 0 && strcmp(line, "cheat file: no report") == 0);
    CHECK(ts2_cheat_file_describe(&g_r, NULL, 0) == 0);
    CHECK(ts2_cheat_file_describe_entry(NULL, 0, line, sizeof line) > 0);
}

int main(void)
{
    snprintf(g_dir, sizeof g_dir, "%s", TS2_CHEAT_FILE_TEST_DIR);
    mkdir(g_dir, 0755);
    test_valid();
    test_document_errors();
    test_entry_shapes();
    test_validator_decisions();
    test_encoding();
    test_size_and_nesting();
    test_file_types();
    test_no_modification();
    test_lifecycle();
    test_engine();
    test_names();
    ts2_cheat_combined_reset();
    printf("cheat file: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
