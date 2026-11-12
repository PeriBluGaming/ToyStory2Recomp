/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3B: external cheat validation and the combined cheat table. All
 * external entries are synthetic in-memory records (no file I/O). Synthetic
 * verified lists and external policies stay inside the real D1/D2 rules;
 * the production wrapper is checked as it is. Real plugin activation,
 * engine, adapters, bridge, view and recomp-ui menu model; psxrecomp
 * services and the menu host are stubs. No guest memory, no file. */
#include "cheat_menu.h"
#include "cheat_menu_view.h"
#include "plugin_stub.h"
#include "psx_menu_host.h"
#include "psx_runtime_stub.h"
#include "recomp_runtime_ui.h"
#include "ts2_cheat_engine.h"
#include "ts2_cheat_external.h"
#include "ts2_cheat_memory.h"
#include "ts2_cheat_table.h"

#include <stdio.h>
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

#define N_OF(a) (sizeof (a) / sizeof (a)[0])
#define OFF(addr) ((addr) - 0x80000000u)

/* ---- Synthetic trusted lists -------------------------------------------- */

/* Verified: the two real variables plus D1 spans (any size allowed in D1). */
static const psxcheat_psx_range k_verified[] = {
    { 0x800B2226u, 0x800B2228u },   /* coins */
    { 0x800B2222u, 0x800B2224u },   /* lives */
    { 0x800A0500u, 0x800A0510u },
    { 0x800A0600u, 0x800A0604u },
    { 0x800A0700u, 0x800A0720u },   /* for synthetic built-ins */
};
/* External policy: the two D1 spans only (inside the verified list). */
static const psxcheat_psx_range k_policy[] = {
    { 0x800A0500u, 0x800A0510u },
    { 0x800A0600u, 0x800A0604u },
};
/* External policy that also allows the coins (owned by a built-in). */
static const psxcheat_psx_range k_policy_coins[] = {
    { 0x800A0500u, 0x800A0510u },
    { 0x800B2226u, 0x800B2228u },
};

static const ts2_cheat_def *g_builtins;
static size_t g_nb;
static ts2_cheat_ext_status g_st[80];

static void use_production_builtins(void) { g_builtins = ts2_cheat_table(&g_nb); }

static ts2_cheat_build_status build(const ts2_cheat_external_input *in, size_t n)
{
    return ts2_cheat_combined_build_with(g_builtins, g_nb, k_verified, N_OF(k_verified),
                                         k_policy, N_OF(k_policy), in, n, g_st);
}

/* One-line input helper. */
#define LINES(...) ((const char *const[]){ __VA_ARGS__ })
static ts2_cheat_external_input one(const char *id, const char *label,
                                    const char *const *lines, size_t nlines)
{
    ts2_cheat_external_input in;
    memset(&in, 0, sizeof in);
    in.id = id;
    in.label = label;
    in.code_lines = lines;
    in.code_line_count = nlines;
    return in;
}

static const ts2_cheat_def *active(size_t *n) { return ts2_cheat_active_table(n); }

/* ---- Menu host stand-ins ------------------------------------------------ */

typedef struct call { char o[40], v[8]; } call;
static call g_calls[16];
static int g_ncalls;
static PsxMenuHost g_host;
static cheat_menu_view g_view;
static RecompRuntimeUi *g_ui;

static int fake_mod_set(const char *p, const char *f, const char *o, const char *v)
{
    (void)p; (void)f;
    if (g_ncalls < 16) {
        snprintf(g_calls[g_ncalls].o, sizeof g_calls[0].o, "%s", o ? o : "(null)");
        snprintf(g_calls[g_ncalls].v, sizeof g_calls[0].v, "%s", v ? v : "(null)");
    }
    ++g_ncalls;
    return 0;
}

static void cb_visibility(void *ctx, int open)
{
    (void)ctx;
    if (open) recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
}

/* p_attach on the active table, persistence through the fake host. */
static void attach(int remember)
{
    RecompRuntimeUiConfig cfg;
    memset(&g_host, 0, sizeof g_host);
    g_host.abi_version = PSX_MENU_ABI_VERSION;
    g_host.mod_set = fake_mod_set;
    memset(g_calls, 0, sizeof g_calls);
    g_ncalls = 0;
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    cheat_menu_view_init(&g_view);
    cheat_menu_view_set_persist(&g_view, remember, cheat_menu_persist_via_host, &g_host);
    (void)cheat_menu_view_sync(&g_view);
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "Cheat Menu";
    cfg.items = cheat_menu_view_items(&g_view, &cfg.item_count);
    cheat_menu_view_callbacks(&g_view, &cfg.callbacks);
    cfg.callbacks.visibility_changed = cb_visibility;
    g_ui = recomp_runtime_ui_create(&cfg);
}

static void in(RecompRuntimeUiInput i) { recomp_runtime_ui_handle_input(g_ui, i, 1, 0); }

static void activate(const char *coins, const char *lives, const char *remember)
{
    g_plugin.option_value = coins;
    g_plugin.lives_value = lives;
    g_plugin.remember_value = remember;
    g_plugin.activation();
}

/* The Phase 3C.2A session order with a build in the window, as the plugin's
 * activation will run it with a file loader: options and activation, then
 * engine shutdown, a fresh session, the build, engine activation (seal). */
static ts2_cheat_build_status activate_and_build(const char *coins, const char *lives,
                                                 const char *remember,
                                                 const ts2_cheat_external_input *e, size_t n)
{
    ts2_cheat_build_status b;
    activate(coins, lives, remember);
    ts2_cheat_engine_shutdown();
    CHECK(ts2_cheat_session_begin() == 1);
    b = build(e, n);
    ts2_cheat_engine_activate();
    return b;
}

/* ------------------------------------------------------------------------- */

static void test_zero_external(void)
{
    size_t n = 0;
    const ts2_cheat_def *t;
    g_test = "zero external";
    ts2_cheat_combined_reset();
    use_production_builtins();
    /* No combined table yet: active = built-ins, verified = production. */
    t = active(&n);
    CHECK(t == g_builtins && n == 2u);
    CHECK(ts2_cheat_active_builtin_count() == 2u && ts2_cheat_active_external_count() == 0u);
    {
        size_t nv = 0, pv = 0;
        CHECK(ts2_cheat_active_verified(&nv) == ts2_cheat_writable_ranges(&pv) && nv == pv);
    }
    CHECK(build(NULL, 0) == TS2_CHEAT_BUILD_OK);
    t = active(&n);
    CHECK(n == 2u && t != g_builtins);                    /* combined copy */
    CHECK(strcmp(t[0].id, "max-coins") == 0 && strcmp(t[1].id, "infinite-lives") == 0);
    CHECK(t[0].ranges == g_builtins[0].ranges);           /* built-in data shared, unchanged */
    CHECK(ts2_cheat_active_external_count() == 0u);
    CHECK(ts2_cheat_def_is_external(&t[0]) == 0 && ts2_cheat_def_is_external(&t[1]) == 0);
    CHECK(ts2_cheat_def_is_external(NULL) == 0);
    /* Production wrapper, no inputs. */
    CHECK(ts2_cheat_combined_build(NULL, 0, NULL) == TS2_CHEAT_BUILD_OK);
    CHECK(ts2_cheat_active_external_count() == 0u);
    ts2_cheat_combined_reset();
    CHECK(active(&n) == g_builtins);
}

static void test_production_policy(void)
{
    size_t np = 0, nv = 0, i, j;
    const psxcheat_psx_range *p = ts2_cheat_external_policy(&np);
    const psxcheat_psx_range *v = ts2_cheat_writable_ranges(&nv);
    ts2_cheat_external_input e[3];
    ts2_cheat_ext_status st[3];
    g_test = "production policy";
    /* Trusted external policy: verified variables only, inside the list. */
    CHECK(np == 3u);
    for (i = 0; i < np; ++i) {
        int covered = 0;
        CHECK(ts2_cheat_range_permitted(p[i]) == 1);
        for (j = 0; j < nv; ++j)
            if (p[i].start >= v[j].start && p[i].end <= v[j].end) covered = 1;
        CHECK(covered);
    }
    /* Coins and lives belong to built-ins: rejected as overlapping. Health
     * (Phase 3B.3) is open to external cheats; anything else is refused. */
    e[0] = one("coins-50", "Coins 50", LINES("800B2226 0032"), 1);
    e[1] = one("lives-5", "Lives 5", LINES("800B2222 0005"), 1);
    e[2] = one("health", "Health", LINES("800B221E 000E"), 1);
    CHECK(ts2_cheat_combined_build(e, 3, st) == TS2_CHEAT_BUILD_OK);
    CHECK(st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    CHECK(st[1] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    CHECK(st[2] == TS2_CHEAT_EXT_OK);                     /* health: verified, external */
    CHECK(ts2_cheat_active_external_count() == 1u);
    ts2_cheat_combined_reset();
}

static void test_accept_and_copy(void)
{
    char id[] = "multi-write", label[] = "Multi Write", desc[] = "writes four bytes";
    char l0[] = "300A0501 0002", l1[] = "300A0500 0001", l2[] = "800A0502 0003", l3[] = "800a0508 0004";
    const char *lines[] = { l0, l1, l2, l3 };
    ts2_cheat_external_input e;
    const ts2_cheat_def *t;
    size_t n = 0;
    g_test = "accept / copy";
    use_production_builtins();
    e = one(id, label, lines, 4);
    e.description = desc;
    e.enabled = 1;
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK);
    t = active(&n);
    CHECK(n == 3u && ts2_cheat_active_external_count() == 1u);
    CHECK(strcmp(t[2].id, "multi-write") == 0 && strcmp(t[2].label, "Multi Write") == 0);
    CHECK(strcmp(t[2].menu_key, "cheat_menu.ext.multi-write") == 0);
    CHECK(strcmp(t[2].off_hint, "writes four bytes") == 0 && t[2].active_hint == t[2].off_hint);
    CHECK(strcmp(t[2].code, "300A0501 0002\n300A0500 0001\n800A0502 0003\n800a0508 0004\n") == 0);
    /* Derived spans: 0500..0504 merged (touching, same policy entry), 0508..050A. */
    CHECK(t[2].range_count == 2u);
    CHECK(t[2].ranges[0].start == 0x800A0500u && t[2].ranges[0].end == 0x800A0504u);
    CHECK(t[2].ranges[1].start == 0x800A0508u && t[2].ranges[1].end == 0x800A050Au);
    CHECK(ts2_cheat_def_is_external(&t[2]) == 1);
    CHECK(ts2_cheat_active_external_enabled(2) == 1);
    CHECK(ts2_cheat_active_external_enabled(0) == 0 && ts2_cheat_active_external_enabled(9) == 0);
    /* Copied: changing the caller's buffers changes nothing. */
    memset(id, 'z', sizeof id - 1u);
    memset(label, 'z', sizeof label - 1u);
    memset(l0, '0', sizeof l0 - 1u);
    memset(desc, 'z', sizeof desc - 1u);
    CHECK(strcmp(t[2].id, "multi-write") == 0 && strcmp(t[2].label, "Multi Write") == 0);
    CHECK(strncmp(t[2].code, "300A0501 0002\n", 14) == 0);
    CHECK(strcmp(t[2].off_hint, "writes four bytes") == 0);
    /* No description: generic texts. */
    e = one("plain", "Plain", LINES("300A0600 0001"), 1);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_OK);
    t = active(&n);
    CHECK(t[2].off_hint == NULL && t[2].active_hint == NULL);
    CHECK(ts2_cheat_active_external_enabled(2) == 0);     /* default off */
}

static void test_ids(void)
{
    static const char *bad[] = { NULL, "", "A", "Abc", "-x", "x_y", "a b", "a.b", "\xc3\xa4",
                                 "abcdefghijklmnopqrstuvwxyz0123456" /* 33 */ };
    ts2_cheat_external_input e;
    size_t i;
    g_test = "ids";
    use_production_builtins();
    for (i = 0; i < N_OF(bad); ++i) {
        e = one(bad[i], "Label", LINES("300A0500 0001"), 1);
        CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK);
        CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_ID);
        CHECK(ts2_cheat_active_external_count() == 0u);
    }
    e = one("abcdefghijklmnopqrstuvwxyz012345", "Label", LINES("300A0500 0001"), 1);  /* 32 */
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_OK);
    {
        size_t n = 0;
        const ts2_cheat_def *t = active(&n);
        CHECK(strcmp(t[2].menu_key, "cheat_menu.ext.abcdefghijklmnopqrstuvwxyz012345") == 0);
        CHECK(strlen(t[2].menu_key) <= TS2_CHEAT_KEY_MAX);
    }
    e = one("x-9", "Label", LINES("300A0500 0001"), 1);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_OK);
    /* Built-in ids are taken. */
    e = one("max-coins", "Label", LINES("300A0500 0001"), 1);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_ID_BUILTIN);
    e = one("infinite-lives", "Label", LINES("300A0500 0001"), 1);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_ID_BUILTIN);
}

static void test_duplicate_ids(void)
{
    ts2_cheat_external_input e[4];
    size_t n = 0;
    const ts2_cheat_def *t;
    g_test = "duplicate ids";
    use_production_builtins();
    e[0] = one("dup", "One", LINES("300A0500 0001"), 1);
    e[1] = one("other", "Other", LINES("300A0501 0001"), 1);
    e[2] = one("dup", "Two", LINES("300A0502 0001"), 1);
    e[3] = one("dup", "", LINES("300A0503 0001"), 1);     /* invalid: not compared */
    CHECK(build(e, 4) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_ID_DUPLICATE && g_st[2] == TS2_CHEAT_EXT_ERR_ID_DUPLICATE);
    CHECK(g_st[1] == TS2_CHEAT_EXT_OK && g_st[3] == TS2_CHEAT_EXT_ERR_LABEL);
    t = active(&n);
    CHECK(n == 3u && strcmp(t[2].id, "other") == 0);
    /* A duplicate of an invalid entry does not hurt the valid one. */
    e[0] = one("solo", "Solo", LINES("300A0500 0001"), 1);
    e[1] = one("solo", "Broken", LINES("XYZ"), 1);
    CHECK(build(e, 2) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_st[1] == TS2_CHEAT_EXT_ERR_PARSE);
}

static void test_labels_descriptions(void)
{
    static const char *bad_labels[] = { NULL, "", " lead", "tab\there", "nl\nx", "\xc3\xa4pfel",
                                        "0123456789012345678901234567890123456789X" /* 41 */ };
    static const char *bad_desc[] = { "", " lead", "bell\a",
        "0123456789012345678901234567890123456789012345678901234567890123X" /* 65 */ };
    ts2_cheat_external_input e;
    size_t i;
    g_test = "labels";
    use_production_builtins();
    for (i = 0; i < N_OF(bad_labels); ++i) {
        e = one("x", bad_labels[i], LINES("300A0500 0001"), 1);
        CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_LABEL);
    }
    e = one("x", "0123456789012345678901234567890123456789", LINES("300A0500 0001"), 1);  /* 40 */
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_OK);
    e = one("x", "Mixed Case 1!", LINES("300A0500 0001"), 1);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_OK);
    for (i = 0; i < N_OF(bad_desc); ++i) {
        e = one("x", "Label", LINES("300A0500 0001"), 1);
        e.description = bad_desc[i];
        CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_DESCRIPTION);
    }
    e = one("x", "Label", LINES("300A0500 0001"), 1);
    e.description = "0123456789012345678901234567890123456789012345678901234567890123";  /* 64 */
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_OK);
}

static void test_code_lines(void)
{
    const char *many[TS2_CHEAT_EXT_LINES_MAX + 1u];
    static char storage[TS2_CHEAT_EXT_LINES_MAX + 1u][16];
    ts2_cheat_external_input e;
    size_t i;
    g_test = "code lines";
    use_production_builtins();
#define CODE(expect, ...)                                                     \
    do {                                                                      \
        const char *const ls_[] = { __VA_ARGS__ };                           \
        e = one("x", "Label", ls_, N_OF(ls_));                                \
        CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK);                            \
        CHECK(g_st[0] == (expect));                                           \
    } while (0)
    CODE(TS2_CHEAT_EXT_ERR_CODE, "");
    CODE(TS2_CHEAT_EXT_ERR_CODE, NULL);
    CODE(TS2_CHEAT_EXT_ERR_CODE, "300A0500 0001\n300A0501 0001");   /* embedded newline */
    CODE(TS2_CHEAT_EXT_ERR_CODE, "300A0500 0001\r");
    CODE(TS2_CHEAT_EXT_ERR_CODE, "300A0500 0001                   X");  /* over 32 bytes */
    CODE(TS2_CHEAT_EXT_ERR_CODE, "300A0500 \xc3\xa4");
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "# only a comment");                 /* no code */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "   ");                              /* blank */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "D00A0500 0001");                    /* conditional: unsupported */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "500A0500 0001");                    /* unsupported type */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "380A0500 0001");                    /* Xplorer */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "800A0501 0001");                    /* odd 16-bit */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "300A0500 0100");                    /* 8-bit too wide */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "30200000 0001");                    /* beyond 2 MiB */
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "300A0500");
    CODE(TS2_CHEAT_EXT_ERR_PARSE, "300A0500 0001", "garbage");         /* one bad line */
    CODE(TS2_CHEAT_EXT_OK, "300A0500 0001 # trailing comment");
    CODE(TS2_CHEAT_EXT_OK, "300a0500\t0001");
#undef CODE
    e = one("x", "Label", NULL, 1);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_CODE);
    e = one("x", "Label", LINES("300A0500 0001"), 0);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_CODE);
    /* 32 lines fine, 33 too many. */
    for (i = 0; i < N_OF(many); ++i) {
        snprintf(storage[i], sizeof storage[i], "300A05%02X 0001", (unsigned)(i % 16u));
        many[i] = storage[i];
    }
    e = one("x", "Label", many, TS2_CHEAT_EXT_LINES_MAX);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_OK);
    {
        size_t n = 0;
        const ts2_cheat_def *t = active(&n);
        CHECK(t[2].range_count == 1u && t[2].ranges[0].start == 0x800A0500u &&
              t[2].ranges[0].end == 0x800A0510u);              /* merged, duplicates folded */
    }
    e = one("x", "Label", many, TS2_CHEAT_EXT_LINES_MAX + 1u);
    CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_CODE);
}

static void test_targets(void)
{
    ts2_cheat_external_input e;
    size_t n = 0;
    const ts2_cheat_def *t;
    g_test = "targets";
    use_production_builtins();
#define TGT(line, expect)                                                     \
    do {                                                                      \
        e = one("x", "Label", LINES(line), 1);                                \
        CHECK(build(&e, 1) == TS2_CHEAT_BUILD_OK);                            \
        CHECK(g_st[0] == (expect));                                           \
    } while (0)
    TGT("300A0700 0001", TS2_CHEAT_EXT_ERR_TARGET);   /* verified, but not external */
    TGT("30000100 0001", TS2_CHEAT_EXT_ERR_TARGET);   /* kernel */
    TGT("30010000 0001", TS2_CHEAT_EXT_ERR_TARGET);   /* EXE code */
    TGT("300D0000 0001", TS2_CHEAT_EXT_ERR_TARGET);   /* overlay area */
    TGT("300A04FF 0001", TS2_CHEAT_EXT_ERR_TARGET);   /* one byte below the span */
    TGT("300A0510 0001", TS2_CHEAT_EXT_ERR_TARGET);   /* one byte above */
    TGT("800A0604 0001", TS2_CHEAT_EXT_ERR_TARGET);   /* just past the second span */
    TGT("800B2226 0063", TS2_CHEAT_EXT_ERR_TARGET);   /* coins: not in this policy */
    TGT("800A050E 0001", TS2_CHEAT_EXT_OK);           /* last halfword of the span */
    TGT("800A0602 0001", TS2_CHEAT_EXT_OK);
#undef TGT
    /* Spans touching across two policy entries are never merged into one. */
    {
        static const psxcheat_psx_range v2[] = { { 0x800A0500u, 0x800A0510u } };
        static const psxcheat_psx_range p2[] = { { 0x800A0500u, 0x800A0508u },
                                                 { 0x800A0508u, 0x800A0510u } };
        e = one("x", "Label", LINES("800A0506 0001", "800A0508 0002"), 2);
        CHECK(ts2_cheat_combined_build_with(g_builtins, g_nb, k_verified, N_OF(k_verified),
                                            p2, 2, &e, 1, g_st) == TS2_CHEAT_BUILD_OK);
        (void)v2;
        CHECK(g_st[0] == TS2_CHEAT_EXT_OK);
        t = active(&n);
        CHECK(t[2].range_count == 2u);
    }
    /* A policy that allows the coins: an external cheat there overlaps the
     * built-in Max Coins and is rejected; Max Coins is untouched. */
    e = one("coins-50", "Coins 50", LINES("800B2226 0032"), 1);
    CHECK(ts2_cheat_combined_build_with(g_builtins, g_nb, k_verified, N_OF(k_verified),
                                        k_policy_coins, N_OF(k_policy_coins), &e, 1, g_st) ==
          TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    e = one("coins-lo", "Coins low byte", LINES("300B2227 0001"), 1);   /* partial overlap */
    CHECK(ts2_cheat_combined_build_with(g_builtins, g_nb, k_verified, N_OF(k_verified),
                                        k_policy_coins, N_OF(k_policy_coins), &e, 1, g_st) ==
          TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    t = active(&n);
    CHECK(n == 2u && t[0].ranges == g_builtins[0].ranges && strcmp(t[0].code, "800B2226 0063") == 0);
}

static void test_external_overlap(void)
{
    ts2_cheat_external_input e[5];
    size_t n = 0;
    const ts2_cheat_def *t;
    g_test = "external overlap";
    use_production_builtins();
    e[0] = one("a", "A", LINES("300A0500 0001"), 1);
    e[1] = one("b", "B", LINES("300A0508 0001"), 1);
    e[2] = one("c", "C", LINES("800A0500 0001"), 1);           /* overlaps A */
    e[3] = one("d", "D", LINES("300A0600 0001", "300A0508 0001"), 2);  /* overlaps B */
    e[4] = one("e", "E", LINES("300A0602 0001"), 1);           /* disjoint from all */
    CHECK(build(e, 5) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL && g_st[2] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL);
    CHECK(g_st[1] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL && g_st[3] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL);
    CHECK(g_st[4] == TS2_CHEAT_EXT_OK);
    t = active(&n);
    CHECK(n == 3u && strcmp(t[2].id, "e") == 0);
    /* Adjacent (touching) spans are not an overlap. */
    e[0] = one("a", "A", LINES("300A0500 0001"), 1);
    e[1] = one("b", "B", LINES("300A0501 0001"), 1);
    CHECK(build(e, 2) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_st[1] == TS2_CHEAT_EXT_OK);
    /* A rejected entry does not take part in overlap checks. */
    e[0] = one("a", "", LINES("300A0500 0001"), 1);
    e[1] = one("b", "B", LINES("300A0500 0001"), 1);
    CHECK(build(e, 2) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_LABEL && g_st[1] == TS2_CHEAT_EXT_OK);
}

static void test_capacity_and_order(void)
{
    static char ids[80][12], lines[80][16];
    static const char *lp[80];
    ts2_cheat_external_input e[80];
    size_t i, n = 0;
    const ts2_cheat_def *t;
    g_test = "capacity / order";
    use_production_builtins();
    /* 20 valid, pairwise disjoint 8-bit writes (16 in the first span, 4 in
     * the second): the first 16 in input order are accepted. */
    for (i = 0; i < 20; ++i) {
        const uint32_t addr = i < 16 ? 0x0A0500u + (uint32_t)i : 0x0A0600u + (uint32_t)(i - 16);
        snprintf(ids[i], sizeof ids[i], "c%02u", (unsigned)(19 - i));   /* not sorted */
        snprintf(lines[i], sizeof lines[i], "30%06X 0001", (unsigned)addr);
        lp[i] = lines[i];
        e[i] = one(ids[i], "Cheat", &lp[i], 1);
    }
    CHECK(build(e, 20) == TS2_CHEAT_BUILD_OK);
    for (i = 0; i < 16; ++i) CHECK(g_st[i] == TS2_CHEAT_EXT_OK);
    for (i = 16; i < 20; ++i) CHECK(g_st[i] == TS2_CHEAT_EXT_ERR_CAPACITY);
    t = active(&n);
    CHECK(n == 18u && ts2_cheat_active_external_count() == TS2_CHEAT_EXTERNAL_MAX);
    for (i = 0; i < 16; ++i) CHECK(strcmp(t[2 + i].id, ids[i]) == 0);   /* input order */
    /* Deterministic: the same input builds the same table. */
    CHECK(build(e, 20) == TS2_CHEAT_BUILD_OK);
    t = active(&n);
    for (i = 0; i < 16; ++i) CHECK(strcmp(t[2 + i].menu_key + strlen(TS2_CHEAT_EXT_KEY_PREFIX), ids[i]) == 0);
    /* An earlier rejection frees the slot for a later entry. */
    e[3].label = "";
    CHECK(build(e, 20) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[3] == TS2_CHEAT_EXT_ERR_LABEL && g_st[16] == TS2_CHEAT_EXT_OK &&
          g_st[17] == TS2_CHEAT_EXT_ERR_CAPACITY);
    e[3].label = "Cheat";
    /* Input limit: entries beyond 64 are not looked at. */
    for (i = 0; i < 70; ++i) {
        snprintf(ids[i], sizeof ids[i], "z%02u", (unsigned)i);
        e[i] = one(ids[i], "", LINES("300A0500 0001"), 1);   /* invalid label */
    }
    e[66] = one("late", "Late", LINES("300A0500 0001"), 1);
    CHECK(build(e, 70) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[63] == TS2_CHEAT_EXT_ERR_LABEL);
    CHECK(g_st[64] == TS2_CHEAT_EXT_ERR_CAPACITY && g_st[66] == TS2_CHEAT_EXT_ERR_CAPACITY &&
          g_st[69] == TS2_CHEAT_EXT_ERR_CAPACITY);
    CHECK(ts2_cheat_active_external_count() == 0u);
    /* NULL status array: still validated. */
    e[0] = one("ok", "Ok", LINES("300A0500 0001"), 1);
    e[1] = one("BAD", "Bad", LINES("300A0501 0001"), 1);
    CHECK(ts2_cheat_combined_build_with(g_builtins, g_nb, k_verified, N_OF(k_verified),
                                        k_policy, N_OF(k_policy), e, 2, NULL) == TS2_CHEAT_BUILD_OK);
    CHECK(ts2_cheat_active_external_count() == 1u);
}

static void test_all_rejected(void)
{
    ts2_cheat_external_input e[3];
    size_t n = 0;
    g_test = "all rejected";
    use_production_builtins();
    e[0] = one("max-coins", "Dup", LINES("300A0500 0001"), 1);
    e[1] = one("x", "X", LINES("D00A0500 0001"), 1);
    e[2] = one("y", "Y", LINES("30000000 0001"), 1);
    CHECK(build(e, 3) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_ID_BUILTIN && g_st[1] == TS2_CHEAT_EXT_ERR_PARSE &&
          g_st[2] == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(active(&n) != NULL && n == 2u && ts2_cheat_active_external_count() == 0u);
    /* Built-ins run as before. */
    stub_reset();
    CHECK(activate_and_build("true", "true", NULL, e, 3) == TS2_CHEAT_BUILD_OK);
    CHECK(ts2_cheat_engine_count() == 2u);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ARMED && ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99 && stub_u16(OFF(0x800B2222u)) == 9);
    ts2_cheat_combined_reset();                 /* next tests build freely */
}

static void test_build_refusals(void)
{
    ts2_cheat_external_input e = one("x", "X", LINES("300A0500 0001"), 1);
    ts2_cheat_def bad[2];
    size_t n = 0;
    static const psxcheat_psx_range p_outside[] = { { 0x800A0800u, 0x800A0804u } };   /* not verified */
    static const psxcheat_psx_range p_overlap[] = { { 0x800A0500u, 0x800A0508u },
                                                    { 0x800A0506u, 0x800A0510u } };
    static const psxcheat_psx_range p_kernel[] = { { 0x80000100u, 0x80000104u } };
    g_test = "refusals";
    use_production_builtins();
    /* Invalid external policies: built-ins only, nothing looked at. */
#define POL(p, np_)                                                                          \
    do {                                                                                     \
        CHECK(ts2_cheat_combined_build_with(g_builtins, g_nb, k_verified, N_OF(k_verified), \
                                            p, np_, &e, 1, g_st) == TS2_CHEAT_BUILD_ERR_POLICY); \
        CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_NOT_CHECKED);                                     \
        CHECK(active(&n) != NULL && n == 2u);                                                \
    } while (0)
    POL(p_outside, 1);
    POL(p_overlap, 2);
    POL(p_kernel, 1);
    POL(NULL, 1);
#undef POL
    /* Empty external policy: valid, nothing writable. */
    CHECK(ts2_cheat_combined_build_with(g_builtins, g_nb, k_verified, N_OF(k_verified),
                                        NULL, 0, &e, 1, g_st) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_TARGET);
    /* Broken built-in table: the engine's fatal case; externals untouched. */
    memcpy(bad, g_builtins, sizeof bad);
    bad[1].id = "max-coins";
    CHECK(ts2_cheat_combined_build_with(bad, 2, k_verified, N_OF(k_verified),
                                        k_policy, N_OF(k_policy), &e, 1, g_st) ==
          TS2_CHEAT_BUILD_ERR_BUILTINS);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_NOT_CHECKED && ts2_cheat_active_external_count() == 0u);
    /* A built-in key in the reserved external namespace. */
    memcpy(bad, g_builtins, sizeof bad);
    bad[1].menu_key = "cheat_menu.ext.lives";
    CHECK(ts2_cheat_combined_build_with(bad, 2, k_verified, N_OF(k_verified),
                                        k_policy, N_OF(k_policy), &e, 1, g_st) ==
          TS2_CHEAT_BUILD_ERR_POLICY);
    CHECK(ts2_cheat_combined_build_with(NULL, 0, k_verified, N_OF(k_verified),
                                        k_policy, N_OF(k_policy), &e, 1, g_st) ==
          TS2_CHEAT_BUILD_ERR_BUILTINS);
    CHECK(strcmp(ts2_cheat_ext_status_name(TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN),
                 "overlaps a built-in cheat") == 0);
    CHECK(strcmp(ts2_cheat_ext_status_name(TS2_CHEAT_EXT_ERR_TARGET), "address not permitted") == 0);
}

static void test_engine_and_menu(void)
{
    ts2_cheat_external_input e[3];
    uint32_t w;
    size_t n = 0;
    g_test = "engine / menu";
    use_production_builtins();
    e[0] = one("ext-a", "Ext A", LINES("800A0500 1234"), 1);
    e[0].enabled = 1;
    e[1] = one("ext-b", "Ext B", LINES("300A0600 0011", "300A0601 0022"), 2);
    e[1].description = "two bytes";
    e[2] = one("broken", "Broken", LINES("800B2226 0001"), 1);   /* rejected */
    stub_reset();
    /* coins off, lives on, remember on */
    CHECK(activate_and_build(NULL, "true", "true", e, 3) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_st[1] == TS2_CHEAT_EXT_OK && g_st[2] == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(ts2_cheat_engine_count() == 4u);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_OFF);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ARMED);   /* file start state */
    CHECK(ts2_cheat_engine_state_at(3) == TS2_CHEAT_OFF);
    CHECK(ts2_cheat_engine_setup_status(2) == TS2_CHEAT_DEF_OK);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2222u)) == 9 && stub_u16(OFF(0x800A0500u)) == 0x1234);
    CHECK(stub_u16(OFF(0x800B2226u)) == 0 && stub_u8(OFF(0x800A0600u)) == 0);
    /* External runtime failure: only that slot. */
    g_stub.drop_from = OFF(0x800A0500u);
    g_stub.drop_to = OFF(0x800A0502u);
    g_stub.ram[OFF(0x800A0500u)] = 1;
    ts2_cheat_engine_vblank();
    g_stub.drop_to = 0;
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ERROR && ts2_cheat_engine_state_at(1) == TS2_CHEAT_ACTIVE);
    w = g_stub.write_calls;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == w + 1u);        /* lives only */

    /* Menu: Resume + 2 built-ins + 2 externals. */
    attach(ts2_cheat_menu_remember_changes());
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 5u);
    CHECK(strcmp(g_view.items[3].key, "cheat_menu.ext.ext-a") == 0 && strcmp(g_view.items[3].label, "Ext A") == 0);
    CHECK(strcmp(g_view.items[4].key, "cheat_menu.ext.ext-b") == 0);
    CHECK(strstr(g_view.row_text[2], "Error (") != NULL);
    CHECK(strcmp(g_view.row_text[3], "Off - two bytes") == 0);
    /* Toggle Ext B (row 4): session only, no mod_set. */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_UP);             /* wraps to the last row */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(ts2_cheat_engine_state_at(3) == TS2_CHEAT_ARMED);
    CHECK(g_ncalls == 0 && g_view.last_persist == CHEAT_MENU_PERSIST_SESSION);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Ext B: armed, applies after resume - session only") == 0);
    /* Built-in and external in one sync: only the built-in is saved. */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);           /* Max Coins */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN); in(RECOMP_RUNTIME_UI_INPUT_DOWN);   /* Ext A */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);         /* retry from ERROR */
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(g_ncalls == 1 && strcmp(g_calls[0].o, "max-coins") == 0 && strcmp(g_calls[0].v, "true") == 0);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_QUEUED);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ARMED);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99 && stub_u16(OFF(0x800A0500u)) == 0x1234 &&
          stub_u8(OFF(0x800A0600u)) == 0x11 && stub_u8(OFF(0x800A0601u)) == 0x22);
    CHECK(g_stub.bad_addresses == 0);
    if (g_ui) { recomp_runtime_ui_destroy(g_ui); g_ui = NULL; }
    ts2_cheat_combined_reset();                 /* next tests build freely */
}

static void test_full_capacity_24(void)
{
    static ts2_cheat_def b8[9];
    static psxcheat_psx_range r8[9];
    static char bid[9][8], bkey[9][24];
    static char ids[16][8], lines[16][16];
    static const char *lp[16];
    ts2_cheat_external_input e[16];
    int on[TS2_CHEAT_MAX];
    size_t i, n = 0;
    g_test = "24 slots";
    CHECK(TS2_CHEAT_MAX == 24u && CHEAT_MENU_MAX_CHEATS == 24u && CHEAT_MENU_MAX_ROWS == 25u);
    /* 8 synthetic built-ins in their own verified D1 halfwords. */
    for (i = 0; i < 9; ++i) {
        snprintf(bid[i], sizeof bid[i], "b%u", (unsigned)i);
        snprintf(bkey[i], sizeof bkey[i], "cheat_menu.b%u", (unsigned)i);
        r8[i].start = 0x800A0700u + 2u * (uint32_t)i;
        r8[i].end = r8[i].start + 2u;
        memset(&b8[i], 0, sizeof b8[i]);
        b8[i].id = bid[i];
        b8[i].label = "Built-in";
        b8[i].code = NULL;
        b8[i].ranges = &r8[i];
        b8[i].range_count = 1;
        b8[i].menu_key = bkey[i];
    }
    {
        static char codes[9][16];
        for (i = 0; i < 9; ++i) {
            snprintf(codes[i], sizeof codes[i], "80%06X 00%02X", (unsigned)(r8[i].start - 0x80000000u),
                     (unsigned)i);
            b8[i].code = codes[i];
        }
    }
    for (i = 0; i < 16; ++i) {
        snprintf(ids[i], sizeof ids[i], "e%02u", (unsigned)i);
        snprintf(lines[i], sizeof lines[i], "300A05%02X 00%02X", (unsigned)i, (unsigned)i);
        lp[i] = lines[i];
        e[i] = one(ids[i], "External", &lp[i], 1);
    }
    CHECK(ts2_cheat_combined_build_with(b8, 8, k_verified, N_OF(k_verified), k_policy,
                                        N_OF(k_policy), e, 16, g_st) == TS2_CHEAT_BUILD_OK);
    for (i = 0; i < 16; ++i) CHECK(g_st[i] == TS2_CHEAT_EXT_OK);
    CHECK(active(&n) != NULL && n == TS2_CHEAT_MAX);
    /* Engine: every slot set up, all on, one VBlank writes all 24. */
    memset(on, 0, sizeof on);
    for (i = 0; i < TS2_CHEAT_MAX; ++i) on[i] = 1;
    stub_reset();
    {
        size_t nv = 0;
        const psxcheat_psx_range *v = ts2_cheat_active_verified(&nv);
        CHECK(ts2_cheat_engine_init_table(active(&n), n, v, nv, on) == TS2_CHEAT_TABLE_OK);
    }
    CHECK(ts2_cheat_engine_count() == 24u);
    for (i = 0; i < 24; ++i) CHECK(ts2_cheat_engine_ready_at(i) == 1);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 24u);
    CHECK(stub_u8(OFF(0x800A050Fu)) == 0x0F && stub_u16(OFF(0x800A070Eu)) == 7);
    /* Menu: 25 rows; the last row is slot 23 and its bit is mask bit 23. */
    attach(1);
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 25u);
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_UP);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(g_view.state.applied_mask == (1u << 23));
    CHECK(ts2_cheat_engine_enabled_at(23) == 0 && ts2_cheat_engine_enabled_at(22) == 1);
    CHECK(g_ncalls == 0);                       /* external: never persisted */
    /* 9 built-ins leave room for 15 externals only. */
    CHECK(ts2_cheat_combined_build_with(b8, 9, k_verified, N_OF(k_verified), k_policy,
                                        N_OF(k_policy), e, 16, g_st) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[14] == TS2_CHEAT_EXT_OK && g_st[15] == TS2_CHEAT_EXT_ERR_CAPACITY);
    CHECK(active(&n) != NULL && n == 24u);
}

int main(void)
{
    test_zero_external();
    test_production_policy();
    test_accept_and_copy();
    test_ids();
    test_duplicate_ids();
    test_labels_descriptions();
    test_code_lines();
    test_targets();
    test_external_overlap();
    test_capacity_and_order();
    test_all_rejected();
    test_build_refusals();
    test_engine_and_menu();
    test_full_capacity_24();
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    ts2_cheat_combined_reset();
    printf("external cheats: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
