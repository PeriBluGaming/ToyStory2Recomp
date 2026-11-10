/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 2E-D2: the production table with both verified cheats, Max Coins
 * (index 0, 800B2226 0063) and Infinite Lives (index 1, 800B2222 0009).
 * Real plugin activation, engine, policy, adapters, bridge, view and
 * recomp-ui's runtime menu model; psxrecomp services and the menu host are
 * stubs. No guest memory, no file. */
#include "cheat_menu.h"
#include "cheat_menu_view.h"
#include "plugin_stub.h"
#include "psx_menu_host.h"
#include "psx_runtime_stub.h"
#include "recomp_runtime_ui.h"
#include "ts2_cheat_engine.h"
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

#define COINS 0x0B2226u
#define LIVES 0x0B2222u
#define IDX_COINS 0u
#define IDX_LIVES 1u

/* ---- fake host ---------------------------------------------------------- */

typedef struct call { char o[32], v[16]; int ids_ok; } call;
static call g_calls[16];
static int g_ncalls;
static PsxMenuHost g_host;

static int fake_mod_set(const char *p, const char *f, const char *o, const char *v)
{
    if (g_ncalls < 16) {
        snprintf(g_calls[g_ncalls].o, sizeof g_calls[0].o, "%s", o ? o : "(null)");
        snprintf(g_calls[g_ncalls].v, sizeof g_calls[0].v, "%s", v ? v : "(null)");
        g_calls[g_ncalls].ids_ok = p && f && strcmp(p, "toystory2.enhancement.cheat-menu") == 0 &&
                                   strcmp(f, "cheat-menu") == 0;
    }
    ++g_ncalls;
    return 0;
}

static int call_is(int k, const char *o, const char *v)
{
    return k < g_ncalls && k < 16 && g_calls[k].ids_ok &&
           strcmp(g_calls[k].o, o) == 0 && strcmp(g_calls[k].v, v) == 0;
}

/* ---- provider stand-ins ------------------------------------------------- */

static cheat_menu_view g_view;
static RecompRuntimeUi *g_ui;

static void cb_visibility(void *ctx, int open)
{
    (void)ctx;
    if (open) recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
}

/* Launcher options, activation (the real plugin), then p_attach. */
static void start(const char *coins, const char *lives, const char *remember)
{
    RecompRuntimeUiConfig cfg;
    stub_reset();
    memset(&g_host, 0, sizeof g_host);
    g_host.abi_version = PSX_MENU_ABI_VERSION;
    g_host.mod_set = fake_mod_set;
    memset(g_calls, 0, sizeof g_calls);
    g_ncalls = 0;
    g_plugin.option_value = coins;
    g_plugin.lives_value = lives;
    g_plugin.remember_value = remember;
    g_plugin.activation();
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

static void in(RecompRuntimeUiInput i) { recomp_runtime_ui_handle_input(g_ui, i, 1, 0); }
static void sync(void) { (void)cheat_menu_view_sync(&g_view); }

/* Open, `down` rows from Resume (1 Max Coins, 2 Infinite Lives), Accept,
 * sync, close. */
static void toggle_row(int down)
{
    int k;
    recomp_runtime_ui_open(g_ui);
    for (k = 0; k < down; ++k) in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    sync();
    recomp_runtime_ui_close(g_ui);
}

static ts2_cheat_state st(size_t i) { return ts2_cheat_engine_state_at(i); }

/* Sentinels around and between the two spans. */
static void put_sentinels(void)
{
    g_stub.ram[0x0B2220] = 0xA1; g_stub.ram[0x0B2221] = 0xA2;   /* below lives */
    g_stub.ram[0x0B2224] = 0xA3; g_stub.ram[0x0B2225] = 0xA4;   /* between */
    g_stub.ram[0x0B2228] = 0xA5; g_stub.ram[0x0B2229] = 0xA6;   /* above coins */
    g_stub.ram[0x0B221E] = 14;                                    /* health */
}

static int sentinels_intact(void)
{
    return g_stub.ram[0x0B2220] == 0xA1 && g_stub.ram[0x0B2221] == 0xA2 &&
           g_stub.ram[0x0B2224] == 0xA3 && g_stub.ram[0x0B2225] == 0xA4 &&
           g_stub.ram[0x0B2228] == 0xA5 && g_stub.ram[0x0B2229] == 0xA6 &&
           g_stub.ram[0x0B221E] == 14 && g_stub.bad_addresses == 0;
}

/* ------------------------------------------------------------------------- */

static void test_table_and_policy(void)
{
    size_t n = 0, pn = 0;
    ts2_cheat_def_status per[TS2_CHEAT_MAX];
    ts2_cheat_def copy[2];
    const ts2_cheat_def *t = ts2_cheat_table(&n);
    const psxcheat_psx_range *p = ts2_cheat_writable_ranges(&pn);
    g_test = "table / policy";
    CHECK(n == 2u && pn == 2u);
    CHECK(ts2_cheat_config_valid() == 1);
    CHECK(ts2_cheat_validate_table(t, n, p, pn, per) == TS2_CHEAT_TABLE_OK);
    CHECK(per[0] == TS2_CHEAT_DEF_OK && per[1] == TS2_CHEAT_DEF_OK);
    /* Both spans are narrow D2 entries the D1/D2 rules permit. */
    CHECK(ts2_cheat_range_permitted(t[0].ranges[0]) == 1);
    CHECK(ts2_cheat_range_permitted(t[1].ranges[0]) == 1);
    CHECK(t[1].ranges[0].end - t[1].ranges[0].start == 2u);
    CHECK(t[1].ranges[0].start >= TS2_CHEAT_D2_START && t[1].ranges[0].end <= TS2_CHEAT_D2_END);
    /* Disjoint: [2222,2224) and [2226,2228). */
    CHECK(t[1].ranges[0].end <= t[0].ranges[0].start);
    /* Each cheat may only write its own span: swapped codes are rejected. */
    memcpy(copy, t, sizeof copy);
    copy[1].code = "800B2226 0009";            /* lives entry writing coins */
    CHECK(ts2_cheat_validate_table(copy, 2, p, pn, per) == TS2_CHEAT_TABLE_OK);
    CHECK(per[0] == TS2_CHEAT_DEF_OK && per[1] == TS2_CHEAT_DEF_ERR_TARGET);
    memcpy(copy, t, sizeof copy);
    copy[0].code = "800B2222 0063";            /* coins entry writing lives */
    CHECK(ts2_cheat_validate_table(copy, 2, p, pn, per) == TS2_CHEAT_TABLE_OK);
    CHECK(per[0] == TS2_CHEAT_DEF_ERR_TARGET && per[1] == TS2_CHEAT_DEF_OK);
    memcpy(copy, t, sizeof copy);
    copy[1].code = "800B2224 0009";            /* the gap: not in any span */
    CHECK(ts2_cheat_validate_table(copy, 2, p, pn, per) == TS2_CHEAT_TABLE_OK);
    CHECK(per[1] == TS2_CHEAT_DEF_ERR_TARGET);
    copy[1].code = "300B2223 0009";            /* odd byte inside the span: fine */
    CHECK(ts2_cheat_validate_table(copy, 2, p, pn, per) == TS2_CHEAT_TABLE_OK);
    CHECK(per[1] == TS2_CHEAT_DEF_OK);
}

static void test_independent_activation(void)
{
    g_test = "activation";
    start(NULL, NULL, NULL);
    CHECK(ts2_cheat_engine_count() == 2u);
    CHECK(st(IDX_COINS) == TS2_CHEAT_OFF && st(IDX_LIVES) == TS2_CHEAT_OFF);
    start(NULL, "true", NULL);
    CHECK(st(IDX_COINS) == TS2_CHEAT_OFF && st(IDX_LIVES) == TS2_CHEAT_ARMED);
    start("true", NULL, NULL);
    CHECK(st(IDX_COINS) == TS2_CHEAT_ARMED && st(IDX_LIVES) == TS2_CHEAT_OFF);
    start("true", "true", NULL);
    CHECK(st(IDX_COINS) == TS2_CHEAT_ARMED && st(IDX_LIVES) == TS2_CHEAT_ARMED);
    start("true", "1", NULL);                  /* only "true" opts in */
    CHECK(st(IDX_LIVES) == TS2_CHEAT_OFF);
    start(NULL, "TRUE", "true");
    CHECK(st(IDX_LIVES) == TS2_CHEAT_OFF && ts2_cheat_menu_remember_changes() == 1);
    CHECK(g_stub.write_calls == 0 && g_ncalls == 0);   /* activation never writes */
}

static void test_lives_alone(void)
{
    g_test = "lives alone";
    start(NULL, "true", NULL);
    put_sentinels();
    g_stub.ram[LIVES] = 5;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 1);
    CHECK(g_stub.log[0].address == 0x800B2222u && g_stub.log[0].width == 2 &&
          g_stub.log[0].value == 9);
    CHECK(stub_u16(LIVES) == 9 && stub_u16(COINS) == 0);
    CHECK(st(IDX_LIVES) == TS2_CHEAT_ACTIVE && st(IDX_COINS) == TS2_CHEAT_OFF);
    /* The game takes a life (death path) or reaches 0: restored next VBlank. */
    g_stub.ram[LIVES] = 8;
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(LIVES) == 9);
    g_stub.ram[LIVES] = 0;
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(LIVES) == 9);
    CHECK(sentinels_intact());
    CHECK(ts2_cheat_engine_stats_at(IDX_LIVES).applied == 3);
    CHECK(ts2_cheat_engine_stats_at(IDX_COINS).applied == 0);
}

static void test_simultaneous(void)
{
    uint32_t k;
    g_test = "simultaneous";
    start("true", "true", NULL);
    put_sentinels();
    for (k = 0; k < 3; ++k) ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 6);             /* two per VBlank */
    for (k = 0; k < 6; k += 2) {                /* table order: coins, lives */
        CHECK(g_stub.log[k].address == 0x800B2226u && g_stub.log[k].value == 0x63);
        CHECK(g_stub.log[k + 1].address == 0x800B2222u && g_stub.log[k + 1].value == 9);
        CHECK(g_stub.log[k].width == 2 && g_stub.log[k + 1].width == 2);
    }
    CHECK(stub_u16(COINS) == 99 && stub_u16(LIVES) == 9);
    CHECK(sentinels_intact());
    CHECK(st(IDX_COINS) == TS2_CHEAT_ACTIVE && st(IDX_LIVES) == TS2_CHEAT_ACTIVE);

    /* Not started: nothing written, asked once per VBlank for both. */
    g_stub.game_started = 0;
    k = g_stub.started_calls;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.started_calls == k + 1u && g_stub.write_calls == 6);
    CHECK(st(IDX_COINS) == TS2_CHEAT_ARMED && st(IDX_LIVES) == TS2_CHEAT_ARMED);
    g_stub.game_started = 1;

    /* Savestate / rewind: both re-arm, both apply again. */
    ts2_cheat_engine_vblank();
    ts2_cheat_engine_savestate();
    CHECK(st(IDX_COINS) == TS2_CHEAT_ARMED && st(IDX_LIVES) == TS2_CHEAT_ARMED);
    g_stub.ram[LIVES] = 2;
    g_stub.ram[COINS] = 3;
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(LIVES) == 9 && stub_u16(COINS) == 99);
    CHECK(st(IDX_COINS) == TS2_CHEAT_ACTIVE && st(IDX_LIVES) == TS2_CHEAT_ACTIVE);
}

static void test_errors_isolated(void)
{
    uint32_t w;
    g_test = "errors";
    /* Lives write dropped: only Infinite Lives fails. */
    start("true", "true", NULL);
    g_stub.ram[LIVES] = 3;
    g_stub.drop_from = LIVES;
    g_stub.drop_to = LIVES + 2u;
    ts2_cheat_engine_vblank();
    CHECK(st(IDX_LIVES) == TS2_CHEAT_ERROR && st(IDX_COINS) == TS2_CHEAT_ACTIVE);
    CHECK(ts2_cheat_engine_stats_at(IDX_LIVES).last_status == PSXCHEAT_ERR_MEMORY_WRITE);
    CHECK(stub_u16(COINS) == 99 && stub_u16(LIVES) == 3);
    g_stub.drop_to = 0;
    w = g_stub.write_calls;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == w + 1u && g_stub.log[w].address == 0x800B2226u);  /* coins only */
    sync();
    CHECK(strstr(g_view.row_text[1], "Error (") != NULL);
    CHECK(strstr(g_view.row_text[0], "Active") != NULL);
    toggle_row(2);                             /* explicit retry from the menu */
    ts2_cheat_engine_vblank();
    CHECK(st(IDX_LIVES) == TS2_CHEAT_ACTIVE && stub_u16(LIVES) == 9);

    /* Coins write dropped: only Max Coins fails. */
    start("true", "true", NULL);
    g_stub.ram[COINS] = 4;
    g_stub.drop_from = COINS;
    g_stub.drop_to = COINS + 2u;
    ts2_cheat_engine_vblank();
    CHECK(st(IDX_COINS) == TS2_CHEAT_ERROR && st(IDX_LIVES) == TS2_CHEAT_ACTIVE);
    CHECK(stub_u16(LIVES) == 9);
    g_stub.drop_to = 0;
}

static void test_menu_rows(void)
{
    size_t n = 0;
    const RecompRuntimeUiItem *it;
    int want = -1;
    g_test = "menu";
    start(NULL, NULL, NULL);
    it = cheat_menu_view_items(&g_view, &n);
    CHECK(n == 3u);
    CHECK(strcmp(it[0].key, CHEAT_MENU_KEY_RESUME) == 0);
    CHECK(strcmp(it[1].key, "cheat_menu.max_coins") == 0 && strcmp(it[1].label, "Max Coins") == 0);
    CHECK(strcmp(it[2].key, "cheat_menu.infinite_lives") == 0 &&
          strcmp(it[2].label, "Infinite Lives") == 0);
    CHECK(strcmp(g_view.row_text[0], "Off - code 800B2226 0063 holds coins at 99") == 0);
    CHECK(strcmp(g_view.row_text[1], "Off - code 800B2222 0009 holds lives at 9") == 0);

    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(cheat_menu_state_pending_at(&g_view.state, IDX_LIVES, &want) == 1 && want == 1);
    CHECK(cheat_menu_state_pending_at(&g_view.state, IDX_COINS, NULL) == 0);
    CHECK(ts2_cheat_engine_enabled_at(IDX_LIVES) == 0);     /* mailbox only */
    sync();
    recomp_runtime_ui_close(g_ui);
    CHECK(st(IDX_LIVES) == TS2_CHEAT_ARMED && st(IDX_COINS) == TS2_CHEAT_OFF);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Infinite Lives: armed, applies after resume") == 0);
    CHECK(g_stub.write_calls == 0);            /* paused: nothing written */
    ts2_cheat_engine_vblank();
    sync();
    CHECK(strcmp(g_view.row_text[1], "Active - lives held at 9") == 0);
    CHECK(strcmp(g_view.row_text[0], "Off - code 800B2226 0063 holds coins at 99") == 0);

    toggle_row(1);                             /* Max Coins on */
    CHECK(st(IDX_COINS) == TS2_CHEAT_ARMED && st(IDX_LIVES) == TS2_CHEAT_ACTIVE);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Max Coins: armed, applies after resume") == 0);
    toggle_row(2);                             /* Infinite Lives off */
    CHECK(st(IDX_LIVES) == TS2_CHEAT_OFF && st(IDX_COINS) == TS2_CHEAT_ARMED);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Infinite Lives: off") == 0);
}

static void test_persistence(void)
{
    g_test = "persistence";
    start(NULL, NULL, "true");
    toggle_row(2);
    CHECK(g_ncalls == 1 && call_is(0, "infinite-lives", "true"));
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Infinite Lives: armed, applies after resume - saving for next start") == 0);
    toggle_row(1);
    CHECK(g_ncalls == 2 && call_is(1, "max-coins", "true"));
    /* Both rows before one sync: one call each, own ids, latest wins. */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);                  /* coins off */
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);                  /* lives off */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);                  /* lives on */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);                  /* lives off */
    sync();
    recomp_runtime_ui_close(g_ui);
    CHECK(g_ncalls == 4 && call_is(2, "max-coins", "false") && call_is(3, "infinite-lives", "false"));
    /* Engine events persist nothing. */
    toggle_row(2);                                       /* lives on: call 5 */
    CHECK(g_ncalls == 5 && call_is(4, "infinite-lives", "true"));
    g_stub.ram[LIVES] = 1;
    g_stub.drop_from = LIVES;
    g_stub.drop_to = LIVES + 2u;
    ts2_cheat_engine_vblank();
    g_stub.drop_to = 0;
    sync();
    CHECK(st(IDX_LIVES) == TS2_CHEAT_ERROR);
    ts2_cheat_engine_savestate();
    sync();
    CHECK(g_ncalls == 5);

    /* Remember off: nothing for either cheat. */
    start(NULL, NULL, NULL);
    toggle_row(1);
    toggle_row(2);
    CHECK(g_ncalls == 0 && g_view.persist_attempts == 0);
    CHECK(st(IDX_COINS) == TS2_CHEAT_ARMED && st(IDX_LIVES) == TS2_CHEAT_ARMED);
}

int main(void)
{
    test_table_and_policy();
    test_independent_activation();
    test_lives_alone();
    test_simultaneous();
    test_errors_isolated();
    test_menu_rows();
    test_persistence();
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    printf("production cheats: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
