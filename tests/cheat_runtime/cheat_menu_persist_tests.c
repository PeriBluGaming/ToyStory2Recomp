/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 2D: "remember-changes" persistence of in-game Max Coins toggles.
 * Real plugin activation (cheat_menu_plugin.c), engine, bridge, view and
 * recomp-ui's runtime menu model; the psxrecomp services and the menu host
 * are stubs. The fake PsxMenuHost.mod_set only records calls: no file is
 * written and no guest memory exists. */
#include "cheat_menu.h"
#include "cheat_menu_view.h"
#include "plugin_stub.h"
#include "psx_menu_host.h"
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

/* ---- fake menu host ----------------------------------------------------- */

typedef struct mod_set_call {
    char package[64], feature[32], option[32], value[16];
    int option_null;
} mod_set_call;

static mod_set_call g_calls[16];
static int g_call_count;
static int g_mod_set_return;
static int g_netplay;

static void copy(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src ? src : "(null)");
}

static int fake_mod_set(const char *p, const char *f, const char *o, const char *v)
{
    if (g_call_count < 16) {
        mod_set_call *c = &g_calls[g_call_count];
        copy(c->package, sizeof c->package, p);
        copy(c->feature, sizeof c->feature, f);
        copy(c->option, sizeof c->option, o);
        copy(c->value, sizeof c->value, v);
        c->option_null = o == NULL;
    }
    ++g_call_count;
    return g_mod_set_return;
}

static int fake_netplay(void) { return g_netplay; }

static PsxMenuHost g_host;

static void host_reset(void)
{
    memset(&g_host, 0, sizeof g_host);
    g_host.abi_version = PSX_MENU_ABI_VERSION;
    g_host.mod_set = fake_mod_set;
    g_host.netplay_active = fake_netplay;
    memset(g_calls, 0, sizeof g_calls);
    g_call_count = 0;
    g_mod_set_return = 0;   /* the runtime's usual answer: not applied live */
    g_netplay = 0;
}

/* ---- provider stand-ins (as cheat_menu_view_tests.c) -------------------- */

static cheat_menu_view g_view;
static RecompRuntimeUi *g_ui;

static void cb_visibility(void *ctx, int open)
{
    (void)ctx;
    if (open) recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
}

/* p_attach after activation: the provider passes the plugin's flag and the
 * attached host. */
static void attach(void *host_ctx)
{
    RecompRuntimeUiConfig cfg;
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    cheat_menu_view_init(&g_view);
    cheat_menu_view_set_persist(&g_view, ts2_cheat_menu_remember_changes(),
                                cheat_menu_persist_via_host, host_ctx);
    (void)cheat_menu_view_sync(&g_view);
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "Cheat Menu";
    cfg.items = cheat_menu_view_items(&g_view, &cfg.item_count);
    cheat_menu_view_callbacks(&g_view, &cfg.callbacks);
    cfg.callbacks.visibility_changed = cb_visibility;
    g_ui = recomp_runtime_ui_create(&cfg);
}

/* Launcher options, then the runtime's activation callback. */
static void activate(const char *max_coins, const char *remember)
{
    g_plugin.option_value = max_coins;
    g_plugin.remember_value = remember;
    g_plugin.activation();
}

static void input(RecompRuntimeUiInput in) { recomp_runtime_ui_handle_input(g_ui, in, 1, 0); }

/* Open, select Max Coins, press `in`, sync (p_event). */
static void toggle(RecompRuntimeUiInput in)
{
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);
    input(in);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
}

static int last_call_is(const char *value)
{
    const mod_set_call *c;
    if (g_call_count < 1 || g_call_count > 16) return 0;
    c = &g_calls[g_call_count - 1];
    return !c->option_null &&
           strcmp(c->package, "toystory2.enhancement.cheat-menu") == 0 &&
           strcmp(c->feature, "cheat-menu") == 0 &&
           strcmp(c->option, "max-coins") == 0 &&
           strcmp(c->value, value) == 0;
}

/* ------------------------------------------------------------------------- */

static void test_unavailable_never_persists(void)
{
    g_test = "unavailable";
    stub_reset();
    host_reset();
    /* No activation yet: engine not set up, remember flag 0. Force remember
     * on to prove the dropped request is not persisted either. */
    attach(&g_host);
    cheat_menu_view_set_persist(&g_view, 1, cheat_menu_persist_via_host, &g_host);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_UNAVAILABLE);
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);          /* disabled row */
    cheat_menu_state_request(&g_view.state, 1);      /* bypassing the row */
    CHECK(cheat_menu_view_sync(&g_view) == 0);
    CHECK(g_call_count == 0 && g_view.persist_attempts == 0);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_NONE);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Max Coins: unavailable") == 0);
}

static void test_remember_option_parsing(void)
{
    g_test = "remember option";
    stub_reset();
    host_reset();
    CHECK(g_plugin.activation != NULL);
    activate(NULL, NULL);
    CHECK(ts2_cheat_menu_activated() == 1);
    CHECK(ts2_cheat_menu_remember_changes() == 0);   /* unset = off */
    activate(NULL, "false");
    CHECK(ts2_cheat_menu_remember_changes() == 0);
    activate(NULL, "1");
    CHECK(ts2_cheat_menu_remember_changes() == 0);
    activate(NULL, "TRUE");
    CHECK(ts2_cheat_menu_remember_changes() == 0);
    activate(NULL, "");
    CHECK(ts2_cheat_menu_remember_changes() == 0);
    activate(NULL, "true ");
    CHECK(ts2_cheat_menu_remember_changes() == 0);
    activate(NULL, "a-value-much-longer-than-the-buffer");
    CHECK(ts2_cheat_menu_remember_changes() == 0);
    activate(NULL, "true");
    CHECK(ts2_cheat_menu_remember_changes() == 1);
    /* Independent of max-coins: both read, neither implies the other. */
    CHECK(ts2_cheat_engine_enabled() == 0);
    activate("true", NULL);
    CHECK(ts2_cheat_menu_remember_changes() == 0 && ts2_cheat_engine_enabled() == 1);
    CHECK(g_call_count == 0);  /* activation never persists */
}

static void test_remember_off_persists_nothing(void)
{
    g_test = "remember off";
    stub_reset();
    host_reset();
    activate(NULL, NULL);
    attach(&g_host);
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Max Coins: armed, applies after resume") == 0);
    toggle(RECOMP_RUNTIME_UI_INPUT_LEFT);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(g_call_count == 0 && g_view.persist_attempts == 0);
}

static void test_remember_on_persists_accepted_toggles(void)
{
    int i;
    g_test = "remember on";
    stub_reset();
    host_reset();
    activate(NULL, "true");
    attach(&g_host);
    CHECK(g_call_count == 0);                       /* attach: no request */

    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(g_call_count == 1 && last_call_is("true"));
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_QUEUED);
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Max Coins: armed, applies after resume - saving for next start") == 0);
    CHECK(strstr(cheat_menu_view_status(&g_view), "saved") == NULL);

    /* Idle frames (p_is_open syncs every frame) never persist again. */
    for (i = 0; i < 10; ++i) (void)cheat_menu_view_sync(&g_view);
    CHECK(g_call_count == 1);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_NONE);

    /* The game running (VBlanks, ARMED -> ACTIVE) persists nothing. */
    ts2_cheat_engine_vblank();
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ACTIVE && g_call_count == 1);

    toggle(RECOMP_RUNTIME_UI_INPUT_RIGHT);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(g_call_count == 2 && last_call_is("false"));
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Max Coins: off - saving for next start") == 0);

    /* Key repeat toggles nothing, so persists nothing. */
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 1);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(g_call_count == 2);

    /* mod_set's live answer is not a persistence result: 1 or 0, same. */
    g_mod_set_return = 1;
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_call_count == 3 && last_call_is("true"));
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_QUEUED);
    CHECK(g_view.persist_attempts == 3);
}

static void test_latest_request_wins(void)
{
    int want = -1;
    g_test = "latest wins";
    stub_reset();
    host_reset();
    activate(NULL, "true");
    attach(&g_host);
    recomp_runtime_ui_open(g_ui);
    input(RECOMP_RUNTIME_UI_INPUT_DOWN);
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);   /* on  */
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);   /* off */
    input(RECOMP_RUNTIME_UI_INPUT_ACCEPT);   /* on  */
    CHECK(cheat_menu_state_pending(&g_view.state, &want) == 1 && want == 1);
    CHECK(g_call_count == 0);                /* nothing before the sync */
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_call_count == 1 && last_call_is("true"));
    CHECK(ts2_cheat_engine_enabled() == 1);
    recomp_runtime_ui_close(g_ui);
}

static void test_engine_events_never_persist(void)
{
    g_test = "engine events";
    stub_reset();
    host_reset();
    activate("true", "true");                /* launcher: on, remember on */
    attach(&g_host);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED);
    CHECK(g_call_count == 0);                /* launcher value not re-saved */

    /* Automatic fail-closed ERROR: the engine switched itself off. */
    g_stub.ram[COINS_OFFSET] = 7;
    g_stub.drop_writes = 1;
    ts2_cheat_engine_vblank();
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ERROR);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(g_call_count == 0);

    /* Savestate load and rewind both arrive as the savestate callback. */
    g_stub.drop_writes = 0;
    CHECK(ts2_cheat_engine_set_enabled(1) == 1);   /* not through the menu */
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_call_count == 0);
    ts2_cheat_engine_vblank();
    g_plugin.savestate();                    /* load state */
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ARMED);
    g_plugin.savestate();                    /* rewind */
    ts2_cheat_engine_vblank();
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ACTIVE);
    CHECK(g_call_count == 0);

    /* Explicit retry from ERROR through the menu does persist. */
    g_stub.ram[COINS_OFFSET] = 7;
    g_stub.drop_writes = 1;
    ts2_cheat_engine_vblank();
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ERROR && g_call_count == 0);
    g_stub.drop_writes = 0;
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_call_count == 1 && last_call_is("true"));
}

static void test_missing_host_callbacks(void)
{
    g_test = "missing host";
    stub_reset();
    host_reset();
    activate(NULL, "true");

    /* No host at all: the toggle still works, nothing is claimed. */
    attach(NULL);
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_FAILED);
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Max Coins: armed, applies after resume - not saved") == 0);

    /* Host without mod_set. */
    g_host.mod_set = NULL;
    attach(&g_host);
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_FAILED);

    /* Host without netplay_active: still persists. */
    host_reset();
    g_host.netplay_active = NULL;
    attach(&g_host);
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_call_count == 1 && last_call_is("true"));

    /* Netplay: mod_set would refuse; not called, reported not saved. */
    host_reset();
    g_netplay = 1;
    attach(&g_host);
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_call_count == 0);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_FAILED);
    CHECK(ts2_cheat_engine_enabled() == 0);   /* the toggle itself applied */

    /* Remember on without a hook (provider detached). */
    host_reset();
    cheat_menu_view_set_persist(&g_view, 1, NULL, NULL);
    toggle(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_call_count == 0 && g_view.last_persist == CHEAT_MENU_PERSIST_FAILED);

    /* The hook itself: NULL option/value never reach mod_set. */
    CHECK(cheat_menu_persist_via_host(&g_host, NULL, "true") == 0);
    CHECK(cheat_menu_persist_via_host(&g_host, "max-coins", NULL) == 0);
    CHECK(cheat_menu_persist_via_host(NULL, "max-coins", "true") == 0);
    CHECK(g_call_count == 0);
    CHECK(cheat_menu_persist_via_host(&g_host, "max-coins", "false") == 1);
    CHECK(g_call_count == 1 && last_call_is("false"));

    cheat_menu_view_set_persist(NULL, 1, NULL, NULL);   /* no crash */
}

int main(void)
{
    test_unavailable_never_persists();
    test_remember_option_parsing();
    test_remember_off_persists_nothing();
    test_remember_on_persists_accepted_toggles();
    test_latest_request_wins();
    test_engine_events_never_persist();
    test_missing_host_callbacks();
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    printf("cheat menu persist: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
