/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 2C integration: the Cheat Menu rows (cheat_menu_view) driven through
 * recomp-ui's real runtime menu model (recomp_runtime_ui.c, compiled in
 * unchanged), the cheat_menu_state bridge, the real engine, adapter, TS2
 * policy and Action Replay core. psxrecomp services are stubs; no guest
 * memory. The provider's SDL/ImGui glue is not compiled here: open_menu()
 * and sync() below mirror what cheat_menu_provider.cpp does (visibility
 * callback enters the section; p_event / p_is_open sync on the main
 * thread). */
#include "cheat_menu_view.h"
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
#include "recomp_runtime_ui.h"
#include "ts2_cheat_engine.h"

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

#define COINS_OFFSET 0x0B2226u

static cheat_menu_view g_view;
static RecompRuntimeUi *g_ui;
static int g_resume_runs;

/* ---- provider stand-ins ------------------------------------------------- */

static int cb_action(void *ctx, const RecompRuntimeUiItem *item)
{
    (void)ctx;
    if (item && item->key && strcmp(item->key, CHEAT_MENU_KEY_RESUME) == 0) {
        ++g_resume_runs;
        recomp_runtime_ui_close(g_ui);
    }
    return 0;
}

static void cb_visibility(void *ctx, int open)
{
    (void)ctx;
    /* As the provider: show the rows directly. */
    if (open) recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
}

/* p_attach: init, first sync, create the menu from the view. */
static void attach(void)
{
    RecompRuntimeUiConfig cfg;
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    cheat_menu_view_init(&g_view);
    (void)cheat_menu_view_sync(&g_view);
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "Cheat Menu";
    cfg.subtitle = "Developer build";
    cfg.items = cheat_menu_view_items(&g_view, &cfg.item_count);
    cheat_menu_view_callbacks(&g_view, &cfg.callbacks);
    cfg.callbacks.run_action = cb_action;
    cfg.callbacks.visibility_changed = cb_visibility;
    g_ui = recomp_runtime_ui_create(&cfg);
}

static void input(RecompRuntimeUiInput in) { recomp_runtime_ui_handle_input(g_ui, in, 1, 0); }
static void sync(void) { (void)cheat_menu_view_sync(&g_view); }

/* The value the row's value column shows: -1 "Unavailable", else 0/1. */
static int row_value(void)
{
    int v = -7;
    if (!cheat_menu_view_get_value(&g_view, &g_view.items[1], &v)) return -1;
    return v;
}

static int text_has(const char *needle)
{
    return strstr(g_view.coins_text, needle) != NULL;
}

static uint32_t traffic(void)
{
    return g_stub.read_calls + g_stub.write_calls + g_stub.started_calls;
}

/* ------------------------------------------------------------------------- */

static void test_rows(void)
{
    size_t n = 0;
    const RecompRuntimeUiItem *it;
    g_test = "rows";
    cheat_menu_view_init(&g_view);
    it = cheat_menu_view_items(&g_view, &n);
    CHECK(n == 2u && it == g_view.items);
    CHECK(strcmp(it[0].key, CHEAT_MENU_KEY_RESUME) == 0);
    CHECK(it[0].type == RECOMP_RUNTIME_UI_ACTION);
    CHECK(strcmp(it[0].label, "Resume Game") == 0);
    CHECK(strcmp(it[1].key, CHEAT_MENU_KEY_MAX_COINS) == 0);
    CHECK(it[1].type == RECOMP_RUNTIME_UI_BOOL);
    CHECK(it[1].minimum == 0 && it[1].maximum == 1);
    CHECK(strcmp(it[1].label, "Max Coins") == 0);
    CHECK(it[1].description == g_view.coins_text);   /* live text */
    CHECK(strcmp(it[0].section, it[1].section) == 0); /* one page */
    CHECK(cheat_menu_view_items(NULL, &n) == NULL && n == 0u);
    /* Unknown items are refused by every callback. */
    {
        RecompRuntimeUiItem other = it[1];
        int v = 0;
        other.key = "cheat_menu.other";
        CHECK(cheat_menu_view_get_value(&g_view, &other, &v) == 0);
        CHECK(cheat_menu_view_set_value(&g_view, &other, 1) == 0);
        CHECK(cheat_menu_view_get_value(&g_view, &it[0], &v) == 0);
        CHECK(cheat_menu_view_set_value(&g_view, &it[0], 1) == 0);
        CHECK(cheat_menu_view_get_value(NULL, &it[1], &v) == 0);
        CHECK(cheat_menu_view_set_value(NULL, &it[1], 1) == 0);
        CHECK(cheat_menu_view_is_enabled(NULL, &it[1]) == 0);
        CHECK(cheat_menu_state_pending(&g_view.state, NULL) == 0);
    }
}

static void test_unavailable(void)
{
    uint32_t before;
    g_test = "unavailable";
    stub_reset();
    /* First engine use in this process: no activation. */
    attach();
    CHECK(g_ui != NULL);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_UNAVAILABLE);
    CHECK(row_value() == -1);
    CHECK(cheat_menu_view_is_enabled(&g_view, &g_view.items[1]) == 0);
    CHECK(cheat_menu_view_is_enabled(&g_view, &g_view.items[0]) == 1);
    CHECK(text_has("Unavailable"));

    before = traffic();
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    input(RECOMP_RUNTIME_UI_INPUT_RIGHT);
    CHECK(cheat_menu_state_pending(&g_view.state, NULL) == 0);  /* disabled row */
    CHECK(cheat_menu_view_set_value(&g_view, &g_view.items[1], 1) == 0);
    sync();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_UNAVAILABLE);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Max Coins: unavailable") == 0);
    CHECK(traffic() == before);
    recomp_runtime_ui_close(g_ui);
}

static void test_resume_first_and_off_by_default(void)
{
    g_test = "resume first";
    stub_reset();
    g_plugin.option_value = NULL;          /* launcher option unset */
    ts2_cheat_engine_activate();
    attach();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_OFF);
    CHECK(row_value() == 0);
    CHECK(text_has("Off") && text_has("800B2226 0063"));

    g_resume_runs = 0;
    recomp_runtime_ui_open(g_ui);
    CHECK(recomp_runtime_ui_is_open(g_ui));
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);  /* row 0 */
    CHECK(g_resume_runs == 1);
    CHECK(!recomp_runtime_ui_is_open(g_ui));
    CHECK(cheat_menu_state_pending(&g_view.state, NULL) == 0);
    CHECK(ts2_cheat_engine_enabled() == 0);

    /* Wraps: Up from Resume lands on Max Coins. */
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_UP);
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    {
        int want = -1;
        CHECK(cheat_menu_state_pending(&g_view.state, &want) == 1 && want == 1);
    }
    sync();
    CHECK(ts2_cheat_engine_enabled() == 1);
    /* Reopening starts on Resume again. */
    recomp_runtime_ui_close(g_ui);
    g_resume_runs = 0;
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_resume_runs == 1);
    CHECK(ts2_cheat_engine_set_enabled(0) == 1);
    sync();
}

static void test_toggle_through_mailbox(void)
{
    int want = -1;
    uint32_t before;
    g_test = "toggle mailbox";
    stub_reset();
    g_plugin.option_value = NULL;
    ts2_cheat_engine_activate();
    attach();
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);

    before = traffic();
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    /* The callback only recorded the request: the engine is untouched. */
    CHECK(cheat_menu_state_pending(&g_view.state, &want) == 1 && want == 1);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_OFF);
    CHECK(row_value() == 1);                   /* shows the request */
    CHECK(traffic() == before);

    CHECK(cheat_menu_view_sync(&g_view) == 1);
    CHECK(cheat_menu_state_pending(&g_view.state, NULL) == 0);
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED);
    CHECK(row_value() == 1);
    CHECK(text_has("Armed"));
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Max Coins: armed, applies after resume") == 0);
    CHECK(traffic() == before);                /* sync never touches RAM */

    /* Key repeat never toggles a BOOL row. */
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 1);
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_RIGHT, 1, 1);
    CHECK(cheat_menu_state_pending(&g_view.state, NULL) == 0);

    /* Paused guest: no VBlank while the menu is open, still ARMED. */
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED && g_stub.write_calls == 0);

    /* Resume: the first VBlank applies; the next sync shows ACTIVE. */
    recomp_runtime_ui_close(g_ui);
    g_stub.ram[COINS_OFFSET] = 5;
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(COINS_OFFSET) == 0x63u);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED);  /* not yet synced */
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ACTIVE);
    CHECK(text_has("Active"));
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Max Coins: active") == 0);

    /* Left / Right toggle as well; off stops the writes. */
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);
    input(RECOMP_RUNTIME_UI_INPUT_LEFT);
    CHECK(cheat_menu_view_sync(&g_view) == 1);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_OFF && row_value() == 0);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Max Coins: off") == 0);
    input(RECOMP_RUNTIME_UI_INPUT_RIGHT);
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED && row_value() == 1);
    input(RECOMP_RUNTIME_UI_INPUT_RIGHT);
    sync();
    recomp_runtime_ui_close(g_ui);
    before = g_stub.write_calls;
    g_stub.ram[COINS_OFFSET] = 5;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == before && stub_u8(COINS_OFFSET) == 5);
}

static void test_latest_request_wins(void)
{
    int want = -1;
    g_test = "latest wins";
    stub_reset();
    g_plugin.option_value = NULL;
    ts2_cheat_engine_activate();
    attach();
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);   /* on */
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);   /* off again, before any sync */
    CHECK(cheat_menu_state_pending(&g_view.state, &want) == 1 && want == 0);
    CHECK(row_value() == 0);
    sync();
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_OFF);
    recomp_runtime_ui_close(g_ui);
}

static void test_error_shown_and_retry(void)
{
    g_test = "error";
    stub_reset();
    g_plugin.option_value = "true";
    ts2_cheat_engine_activate();
    attach();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED && row_value() == 1);

    g_stub.ram[COINS_OFFSET] = 7;
    g_stub.drop_writes = 1;
    ts2_cheat_engine_vblank();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ERROR);
    CHECK(row_value() == 0);                   /* fail closed: shows Off */
    CHECK(text_has("Error (") && text_has(psxcheat_status_name(PSXCHEAT_ERR_MEMORY_WRITE)));
    CHECK(cheat_menu_view_is_enabled(&g_view, &g_view.items[1]) == 1);

    /* The engine never re-enables itself. */
    g_stub.drop_writes = 0;
    ts2_cheat_engine_vblank();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ERROR);

    /* An explicit toggle from the menu retries. */
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(cheat_menu_view_sync(&g_view) == 1);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED);
    recomp_runtime_ui_close(g_ui);
    ts2_cheat_engine_vblank();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ACTIVE);
    CHECK(stub_u16(COINS_OFFSET) == 0x63u);

    /* ERROR -> explicit off. */
    g_stub.ram[COINS_OFFSET] = 7;
    g_stub.drop_writes = 1;
    ts2_cheat_engine_vblank();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ERROR);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Max Coins: error") == 0);
    CHECK(cheat_menu_view_set_value(&g_view, &g_view.items[1], 0) == 1);
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_OFF);
    g_stub.drop_writes = 0;
}

static void test_launcher_on_and_savestate(void)
{
    g_test = "launcher on";
    stub_reset();
    g_plugin.option_value = "true";
    ts2_cheat_engine_activate();
    attach();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED && row_value() == 1);
    g_stub.game_started = 0;
    ts2_cheat_engine_vblank();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED && g_stub.write_calls == 0);
    g_stub.game_started = 1;
    ts2_cheat_engine_vblank();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ACTIVE);
    ts2_cheat_engine_savestate();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED && row_value() == 1);

    /* Re-attach (renderer restart) keeps the engine's state. */
    attach();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED);
    ts2_cheat_engine_vblank();
    sync();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ACTIVE);

    g_plugin.option_value = "1";              /* only "true" opts in */
    ts2_cheat_engine_activate();
    attach();
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_OFF);
}

int main(void)
{
    test_rows();
    test_unavailable();
    test_resume_first_and_off_by_default();
    test_toggle_through_mailbox();
    test_latest_request_wins();
    test_error_shown_and_retry();
    test_launcher_on_and_savestate();
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    printf("cheat menu view: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
