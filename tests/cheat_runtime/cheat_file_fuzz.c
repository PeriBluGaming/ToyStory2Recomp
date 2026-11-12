/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3C.2B: bounded, deterministic mutation fuzzer for the external
 * cheat file loader (from memory, no files). Seeds are valid and
 * near-valid documents; each iteration mutates bytes, inserts TOML tokens,
 * truncates or splices, then loads the result in a fresh session window.
 * Checks: no crash (run it under ASan/UBSan too), statuses in range, the
 * report consistent, the built-in cheats always present and unchanged, and
 * the table within its limits. Fixed seed and iteration count. */
#include "ts2_cheat_external.h"
#include "ts2_cheat_file.h"
#include "ts2_cheat_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITERATIONS 20000u
#define BUF_MAX 2048u

static int g_failures;
static unsigned long g_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            if (g_failures++ < 20)                                           \
                printf("  FAIL iteration %u: %s\n", it, #cond);              \
        }                                                                    \
    } while (0)

static unsigned long long g_state = 0x9E3779B97F4A7C15ull;

static unsigned rnd(unsigned n)
{
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return n ? (unsigned)(g_state % n) : 0u;
}

static const char *const k_seeds[] = {
    "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00893\"\n"
    "[[cheat]]\nid = \"health-14\"\nlabel = \"Full Health\"\ndescription = \"Keeps health at 14\"\n"
    "code = [\"800B221E 000E\"]\nenabled = false\n",
    "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00893\"\n"
    "cheat = [ { id = \"a\", label = \"A\", code = [\"300B221E 000E\", \"300B221F 0000\"] },"
    " { id = \"b\", label = \"B\", code = [\"800B2226 0063\"], enabled = true } ]\n",
    "\xEF\xBB\xBF# comment [[ {{\nschema = 'toystory2recomp.cheats'\nversion = 1\n"
    "game = \"\"\"SLUS-00893\"\"\"\n[[cheat]]\nid = \"x\"\nlabel = \"X\"\ncode = [\n\"800B221E 000E\",\n]\n",
    "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00893\"\n"
    "[[cheat]]\nid = \"dup\"\nlabel = \"1\"\ncode = [\"800B221E 000E\"]\n"
    "[[cheat]]\nid = \"dup\"\nlabel = \"2\"\ncode = [\"D00B221E 0001\"]\ncolor = 1\n",
};

static const char *const k_tokens[] = {
    "[", "]", "[[", "]]", "{", "}", "\"", "'", "\"\"\"", "'''", "=", ",", "\n", "#", "\\",
    "\\u0000", "\\uD800", "true", "1.5", "-0", "0x10", "1979-05-27", "inf", "nan", "\xEF\xBB\xBF",
    "\xC3\x28", "\xF0\x9F\x8E\xAE", "\x00", "[[cheat]]\n", "id = \"z\"\n", "code = []\n",
    "version = 1\n", "\r\n", "a.b.c = 1\n", "[cheat.x]\n", ".", "a.", "\"q\".",
    "a.b.c.d.e.f.g.h = 1\n", "[x.y.z.w.v.u]\n", "1.5", "\xEF\xBB\xBF\xEF\xBB\xBF",
};

int main(void)
{
    static unsigned char buf[BUF_MAX];
    static ts2_cheat_file_report r;
    unsigned it;
    unsigned long ok = 0, file_err = 0;
    size_t nb = 0;
    const ts2_cheat_def *builtins = ts2_cheat_table(&nb);

    for (it = 0; it < ITERATIONS; ++it) {
        const char *seed = k_seeds[rnd(sizeof k_seeds / sizeof k_seeds[0])];
        size_t len = strlen(seed), k, n = 0;
        unsigned m, rounds = 1u + rnd(8u);
        const ts2_cheat_def *t;
        ts2_cheat_file_status s;
        memcpy(buf, seed, len);
        for (m = 0; m < rounds; ++m) {
            switch (rnd(6u)) {
            case 0:     /* flip a byte */
                if (len) buf[rnd((unsigned)len)] ^= (unsigned char)(1u << rnd(8u));
                break;
            case 1:     /* random byte */
                if (len) buf[rnd((unsigned)len)] = (unsigned char)rnd(256u);
                break;
            case 2: {   /* insert a token */
                const char *tok = k_tokens[rnd(sizeof k_tokens / sizeof k_tokens[0])];
                size_t tl = strlen(tok) ? strlen(tok) : 1u, at = rnd((unsigned)len + 1u);
                if (len + tl < BUF_MAX) {
                    memmove(buf + at + tl, buf + at, len - at);
                    memcpy(buf + at, tok, tl);
                    len += tl;
                }
                break;
            }
            case 3:     /* truncate */
                len = rnd((unsigned)len + 1u);
                break;
            case 4: {   /* delete a span */
                size_t at = rnd((unsigned)len + 1u), dl = rnd(16u);
                if (at + dl > len) dl = len - at;
                memmove(buf + at, buf + at + dl, len - at - dl);
                len -= dl;
                break;
            }
            default: {  /* duplicate a span */
                size_t at = rnd((unsigned)len + 1u), dl = rnd(64u);
                if (at + dl > len) dl = len - at;
                if (len + dl < BUF_MAX) {
                    memmove(buf + at + dl, buf + at, len - at);
                    len += dl;
                }
                break;
            }
            }
        }
        ts2_cheat_combined_reset();
        if (ts2_cheat_session_begin() != 1) { CHECK(0); continue; }
        s = ts2_cheat_file_load_buffer(buf, len, &r);
        CHECK(s >= TS2_CHEAT_FILE_OK && s <= TS2_CHEAT_FILE_ERR_INTERNAL);
        CHECK(s != TS2_CHEAT_FILE_ERR_INTERNAL && s != TS2_CHEAT_FILE_ERR_SEALED);
        CHECK(r.file == s && r.build == TS2_CHEAT_BUILD_OK);
        CHECK(r.accepted <= r.validated && r.validated <= TS2_CHEAT_EXTERNAL_INPUT_MAX);
        CHECK(r.validated <= r.entries || r.entries == 0u);
        t = ts2_cheat_active_table(&n);
        CHECK(t != NULL && n >= nb && n <= nb + TS2_CHEAT_EXTERNAL_MAX);
        CHECK(n == nb + r.accepted);
        for (k = 0; t && k < nb && k < n; ++k)
            CHECK(t[k].id == builtins[k].id && t[k].code == builtins[k].code &&
                  t[k].ranges == builtins[k].ranges);
        for (k = nb; t && k < n; ++k)
            CHECK(t[k].range_count >= 1u && t[k].ranges[0].start >= 0x800B221Eu &&
                  t[k].ranges[t[k].range_count - 1].end <= 0x800B2220u);
        if (s == TS2_CHEAT_FILE_OK) ++ok; else ++file_err;
    }
    ts2_cheat_combined_reset();
    printf("cheat file fuzz: %u iterations (%lu parsed, %lu file errors), %lu checks, %d failures\n",
           ITERATIONS, ok, file_err, g_checks, g_failures);
    return g_failures ? 1 : 0;
}
