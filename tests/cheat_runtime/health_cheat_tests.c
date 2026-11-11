/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3B.3: the verified player-health span [0x800B221E, 0x800B2220) in
 * the trusted verified and external policies, external health cheats
 * through the production wrapper, and the opt-in runtime-test entry.
 *
 * Built twice: ts2_cheat_health_tests (plugin as in production: the
 * runtime-test entry must NOT appear) and ts2_cheat_health_runtime_tests
 * (plugin compiled with TS2_CHEAT_RUNTIME_TEST_HEALTH: it must appear,
 * through the normal validation, off at start). psxrecomp services and the
 * menu host are stubs; no guest memory, no file. */
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

#define HEALTH 0x0B221Eu
#define LINES(...) ((const char *const[]){ __VA_ARGS__ })

static ts2_cheat_ext_status g_st[8];

/* Entry k of an active table of n entries. Out of range (a build that
 * failed): a failed check and a harmless empty entry, so a failing test
 * reports instead of dereferencing NULL fields. */
static const psxcheat_psx_range k_none_range = { 0u, 0u };
static const ts2_cheat_def k_none = { "", "", "", &k_none_range, 0u, "", NULL, NULL };
static const ts2_cheat_def *entry(const ts2_cheat_def *t, size_t n, size_t k)
{
    CHECK(t != NULL && k < n);
    return t && k < n ? &t[k] : &k_none;
}

static ts2_cheat_external_input one(const char *id, const char *label,
                                    const char *const *lines, size_t n)
{
    ts2_cheat_external_input in;
    memset(&in, 0, sizeof in);
    in.id = id;
    in.label = label;
    in.code_lines = lines;
    in.code_line_count = n;
    return in;
}

/* Production wrapper: real built-ins, verified list and external policy. */
static ts2_cheat_ext_status build_one(const char *line)
{
    ts2_cheat_external_input e = one("x", "X", LINES(line), 1);
    CHECK(ts2_cheat_combined_build(&e, 1, g_st) == TS2_CHEAT_BUILD_OK);
    return g_st[0];
}

/* ---- menu host stand-ins ------------------------------------------------ */

static int g_ncalls;
static PsxMenuHost g_host;
static cheat_menu_view g_view;
static RecompRuntimeUi *g_ui;

static int fake_mod_set(const char *p, const char *f, const char *o, const char *v)
{
    (void)p; (void)f; (void)o; (void)v;
    ++g_ncalls;
    return 0;
}

static void cb_visibility(void *ctx, int open)
{
    (void)ctx;
    if (open) recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
}

static void attach(void)
{
    RecompRuntimeUiConfig cfg;
    memset(&g_host, 0, sizeof g_host);
    g_host.abi_version = PSX_MENU_ABI_VERSION;
    g_host.mod_set = fake_mod_set;
    g_ncalls = 0;
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    cheat_menu_view_init(&g_view);
    cheat_menu_view_set_persist(&g_view, ts2_cheat_menu_remember_changes(),
                                cheat_menu_persist_via_host, &g_host);
    (void)cheat_menu_view_sync(&g_view);
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "Cheat Menu";
    cfg.items = cheat_menu_view_items(&g_view, &cfg.item_count);
    cheat_menu_view_callbacks(&g_view, &cfg.callbacks);
    cfg.callbacks.visibility_changed = cb_visibility;
    g_ui = recomp_runtime_ui_create(&cfg);
}

static void activate(const char *coins, const char *lives, const char *remember)
{
    g_plugin.option_value = coins;
    g_plugin.lives_value = lives;
    g_plugin.remember_value = remember;
    g_plugin.activation();
}

/* ------------------------------------------------------------------------- */

static void test_policies(void)
{
    size_t nv = 0, np = 0, i, j, nb = 0;
    const psxcheat_psx_range *v = ts2_cheat_writable_ranges(&nv);
    const psxcheat_psx_range *p = ts2_cheat_external_policy(&np);
    const ts2_cheat_def *b = ts2_cheat_table(&nb);
    int in_v = 0, in_p = 0;
    g_test = "policies";
    CHECK(TS2_CHEAT_HEALTH_ADDR == 0x800B221Eu);
    CHECK(nv == 3u && np == 3u);
    CHECK(ts2_cheat_config_valid() == 1);      /* permitted, pairwise disjoint */
    for (i = 0; i < nv; ++i)
        if (v[i].start == 0x800B221Eu && v[i].end == 0x800B2220u) in_v = 1;
    for (i = 0; i < np; ++i)
        if (p[i].start == 0x800B221Eu && p[i].end == 0x800B2220u) in_p = 1;
    CHECK(in_v && in_p);                       /* exactly the two bytes */
    /* Containment: every external entry lies inside one verified entry. */
    for (i = 0; i < np; ++i) {
        int covered = 0;
        for (j = 0; j < nv; ++j)
            if (p[i].start >= v[j].start && p[i].end <= v[j].end) covered = 1;
        CHECK(covered);
        CHECK(p[i].end - p[i].start == 2u);
        CHECK(ts2_cheat_range_permitted(p[i]) == 1);
    }
    /* No built-in uses health; coins and lives spans unchanged. */
    CHECK(nb == 2u);
    for (i = 0; i < nb; ++i)
        for (j = 0; j < b[i].range_count; ++j)
            CHECK(b[i].ranges[j].end <= 0x800B221Eu || b[i].ranges[j].start >= 0x800B2220u);
    CHECK(b[0].ranges[0].start == 0x800B2226u && b[0].ranges[0].end == 0x800B2228u);
    CHECK(b[1].ranges[0].start == 0x800B2222u && b[1].ranges[0].end == 0x800B2224u);
    CHECK(strcmp(b[0].code, "800B2226 0063") == 0 && strcmp(b[1].code, "800B2222 0009") == 0);
}

static void test_health_codes(void)
{
    size_t n = 0;
    const ts2_cheat_def *t;
    g_test = "health codes";
    /* The recommended code: accepted, exactly the health halfword. */
    CHECK(strcmp(TS2_CHEAT_HEALTH_RECOMMENDED, "800B221E 000E") == 0);
    CHECK(build_one(TS2_CHEAT_HEALTH_RECOMMENDED) == TS2_CHEAT_EXT_OK);
    t = ts2_cheat_active_table(&n);
    CHECK(n == 3u && entry(t, n, 2)->range_count == 1u);
    CHECK(entry(t, n, 2)->ranges[0].start == 0x800B221Eu &&
          entry(t, n, 2)->ranges[0].end == 0x800B2220u);
    CHECK(strcmp(entry(t, n, 2)->menu_key, "cheat_menu.ext.x") == 0);
    /* 000F: the policy authorises the address, not the value. It is not
     * the recommended value (15 is outside the game's 0..14), but no global
     * value restriction exists, so it passes address validation. */
    CHECK(strcmp(TS2_CHEAT_HEALTH_RECOMMENDED, "800B221E 000F") != 0);
    CHECK(build_one("800B221E 000F") == TS2_CHEAT_EXT_OK);
    /* The reported unaligned code: a 16-bit write at an odd address. */
    CHECK(build_one("800B2221 000F") == TS2_CHEAT_EXT_ERR_PARSE);
    /* Byte-level edges of the span. */
    CHECK(build_one("300B221E 000E") == TS2_CHEAT_EXT_OK);    /* low byte */
    CHECK(build_one("300B221F 0000") == TS2_CHEAT_EXT_OK);    /* high byte */
    CHECK(build_one("300B2221 000F") == TS2_CHEAT_EXT_ERR_TARGET);   /* timer high byte */
    CHECK(build_one("300B2220 000F") == TS2_CHEAT_EXT_ERR_TARGET);   /* timer */
    CHECK(build_one("800B2220 0384") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(build_one("300B221D 0000") == TS2_CHEAT_EXT_ERR_TARGET);   /* state above */
    CHECK(build_one("800B221C FF60") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(build_one("800C1614 000E") == TS2_CHEAT_EXT_ERR_TARGET);   /* save-block copy */
    /* Built-in spans stay owned by the built-ins. */
    CHECK(build_one("800B2222 0009") == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    CHECK(build_one("800B2226 0063") == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
}

static void test_overlaps(void)
{
    ts2_cheat_external_input e[3];
    size_t n = 0;
    g_test = "overlaps";
    /* Two external health cheats: both rejected. */
    e[0] = one("h1", "H1", LINES("800B221E 000E"), 1);
    e[1] = one("h2", "H2", LINES("300B221F 0000"), 1);
    e[2] = one("h3", "H3", LINES("800B221E 0005"), 1);
    CHECK(ts2_cheat_combined_build(e, 3, g_st) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL && g_st[1] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL &&
          g_st[2] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL);
    CHECK(ts2_cheat_active_table(&n) != NULL && n == 2u);
    /* Health plus a built-in variable in one entry: rejected as a whole. */
    e[0] = one("mix", "Mix", LINES("800B221E 000E", "800B2226 0063"), 2);
    CHECK(ts2_cheat_combined_build(e, 1, g_st) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    /* Health plus the timer in one entry: rejected (target). */
    e[0] = one("mix", "Mix", LINES("800B221E 000E", "800B2220 0384"), 2);
    CHECK(ts2_cheat_combined_build(e, 1, g_st) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(ts2_cheat_active_external_count() == 0u);
}

static void test_engine_and_isolation(void)
{
    ts2_cheat_external_input e[2];
#if !defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
    size_t n = 0;
    uint32_t w;
#endif
    g_test = "engine";
    e[0] = one("full-health", "Full Health", LINES(TS2_CHEAT_HEALTH_RECOMMENDED), 1);
    e[0].description = "holds health at 14";
    e[1] = one("bad", "Bad", LINES("800B2221 000F"), 1);
    CHECK(ts2_cheat_combined_build(e, 2, g_st) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_st[1] == TS2_CHEAT_EXT_ERR_PARSE);
    stub_reset();
#if !defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
    activate("true", "true", "true");
    CHECK(ts2_cheat_engine_count() == 3u);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ARMED && ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_OFF);      /* off at start */
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);
    g_stub.ram[0x0B221C] = 0xA1; g_stub.ram[0x0B221D] = 0xA2;  /* neighbours */
    g_stub.ram[0x0B2220] = 0xA3; g_stub.ram[0x0B2221] = 0xA4;
    g_stub.ram[HEALTH] = 13;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 3u);            /* coins, lives, health */
    CHECK(g_stub.log[2].address == 0x800B221Eu && g_stub.log[2].width == 2 && g_stub.log[2].value == 0x000E);
    CHECK(stub_u16(HEALTH) == 14);
    CHECK(stub_u16(0x0B2226) == 99 && stub_u16(0x0B2222) == 9);
    CHECK(g_stub.ram[0x0B221C] == 0xA1 && g_stub.ram[0x0B221D] == 0xA2 &&
          g_stub.ram[0x0B2220] == 0xA3 && g_stub.ram[0x0B2221] == 0xA4);
    /* A hit (game -1) is restored on the next VBlank. */
    g_stub.ram[HEALTH] = 13;
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(HEALTH) == 14);
    /* Documented runtime risk: a negative (dead) value is overwritten too.
     * What the game then does mid-death is a manual-test question. */
    g_stub.ram[HEALTH] = 0xFF; g_stub.ram[HEALTH + 1u] = 0xFF;   /* -1 */
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(HEALTH) == 14);
    /* Runtime failure of the external slot: built-ins unaffected. */
    g_stub.ram[HEALTH] = 3;
    g_stub.drop_from = HEALTH;
    g_stub.drop_to = HEALTH + 2u;
    ts2_cheat_engine_vblank();
    g_stub.drop_to = 0;
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ERROR);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ACTIVE && ts2_cheat_engine_state_at(1) == TS2_CHEAT_ACTIVE);
    w = g_stub.write_calls;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == w + 2u);
    /* Menu: health row, session only, never persisted. */
    attach();
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 4u && strcmp(g_view.items[3].label, "Full Health") == 0);
    CHECK(strstr(g_view.row_text[2], "Error (") != NULL);
    recomp_runtime_ui_open(g_ui);
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_UP, 1, 0);
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ARMED);
    CHECK(g_ncalls == 0);
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Full Health: armed, applies after resume - session only") == 0);
#endif
}

static void test_runtime_test_entry(void)
{
    size_t n = 0;
    const ts2_cheat_def *t;
    g_test = "runtime-test entry";
#if defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
    {
        /* The helper exists only in the runtime-test configuration: its
         * data is valid against the production policy and off at start. */
        const ts2_cheat_external_input in = ts2_cheat_runtime_test_health_input();
        CHECK(strcmp(in.id, "test-health") == 0 && strcmp(in.label, "TEST Full Health") == 0);
        CHECK(in.code_line_count == 1u &&
              strcmp(in.code_lines[0], TS2_CHEAT_HEALTH_RECOMMENDED) == 0);
        CHECK(in.enabled == 0);
        CHECK(ts2_cheat_combined_build(&in, 1, g_st) == TS2_CHEAT_BUILD_OK &&
              g_st[0] == TS2_CHEAT_EXT_OK);
    }
#endif

    /* Whether activation adds it depends only on the opt-in macro. */
    ts2_cheat_combined_reset();
    stub_reset();
    activate(NULL, NULL, NULL);
    t = ts2_cheat_active_table(&n);
#if defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
    CHECK(n == 3u && ts2_cheat_engine_count() == 3u);
    CHECK(strcmp(entry(t, n, 2)->id, "test-health") == 0 &&
          strcmp(entry(t, n, 2)->label, "TEST Full Health") == 0);
    CHECK(ts2_cheat_def_is_external(entry(t, n, 2)) == 1);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_OFF);       /* never on by itself */
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_OFF && ts2_cheat_engine_state_at(1) == TS2_CHEAT_OFF);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 0);
    /* Switched on from the menu: writes 14, nothing persisted. */
    attach();
    recomp_runtime_ui_open(g_ui);
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_UP, 1, 0);
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 1u && stub_u16(HEALTH) == 14);
    CHECK(g_ncalls == 0);
    /* Activation again (no duplicate entry). */
    activate(NULL, NULL, NULL);
    CHECK(ts2_cheat_engine_count() == 3u);
#else
    /* Production plugin: no runtime-test entry, no combined table. */
    CHECK(t == ts2_cheat_table(NULL) && n == 2u && ts2_cheat_engine_count() == 2u);
    CHECK(ts2_cheat_active_external_count() == 0u);
#endif
}

int main(void)
{
    test_policies();
    test_health_codes();
    test_overlaps();
    test_engine_and_isolation();
    test_runtime_test_entry();
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    ts2_cheat_combined_reset();
#if defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
    printf("health cheat (runtime-test build): %d checks, %d failures\n", g_checks, g_failures);
#else
    printf("health cheat: %d checks, %d failures\n", g_checks, g_failures);
#endif
    return g_failures ? 1 : 0;
}
