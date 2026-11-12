/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3C.2A: session lifecycle of the combined cheat table and the Cheat
 * Menu's plan-membership check. Simulates what psxrecomp does in one
 * process: session 1 (activation, provider attach), return to the frontend
 * (provider detach, no more VBlank), session 2 with the feature enabled or
 * deselected. Real plugin activation, engine, table, view; the runtime's
 * plugin registry, plan probe and psxrecomp services are stubs. The menu
 * provider (C++/ImGui) is represented by what it does with the table: a
 * table reference from attach to detach and a view built from the active
 * table. No guest memory, no file. */
#include "cheat_menu.h"
#include "cheat_menu_view.h"
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
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
/* NULL-safe string for checks on entries that a broken lifecycle could
 * have wiped (the test then reports instead of crashing). */
#define S(p) ((p) ? (p) : "")
/* Start of entry k's first range, 0 when it has none. */
static uint32_t first_start(const ts2_cheat_def *t, size_t n, size_t k)
{
    return t && k < n && t[k].ranges && t[k].range_count ? t[k].ranges[0].start : 0u;
}
#define LINES(...) ((const char *const[]){ __VA_ARGS__ })

static ts2_cheat_ext_status g_st[4];
static cheat_menu_view g_view;
static int g_attached;

static ts2_cheat_external_input one(const char *id, const char *label, const char *line, int on)
{
    static const char *lines[4][1];
    static int k;
    ts2_cheat_external_input in;
    k = (k + 1) % 4;
    lines[k][0] = line;
    memset(&in, 0, sizeof in);
    in.id = id;
    in.label = label;
    in.code_lines = lines[k];
    in.code_line_count = 1;
    in.enabled = on;
    return in;
}

/* The runtime activating the plugin for a session whose plan contains the
 * feature, with options. extra != NULL: a build inside the session window
 * (the place the Phase 3C file loader will use). */
static void session(const char *coins, const char *lives,
                    const ts2_cheat_external_input *extra, size_t n_extra)
{
    g_plugin.not_in_plan = 0;
    g_plugin.option_value = coins;
    g_plugin.lives_value = lives;
    g_plugin.remember_value = NULL;
    g_plugin.activation();
    if (extra) {
        ts2_cheat_engine_shutdown();
        CHECK(ts2_cheat_session_begin() == 1);
        CHECK(ts2_cheat_combined_build(extra, n_extra, g_st) == TS2_CHEAT_BUILD_OK);
        ts2_cheat_engine_activate();
    }
}

/* Provider attach / detach, as cheat_menu_provider.cpp does them. */
static int provider_attach(void)
{
    if (!ts2_cheat_menu_activated()) return 0;
    ts2_cheat_table_ref_acquire();
    cheat_menu_view_init(&g_view);
    (void)cheat_menu_view_sync(&g_view);
    g_attached = 1;
    return 1;
}

static void provider_detach(void)
{
    if (!g_attached) return;
    ts2_cheat_table_ref_release();
    g_attached = 0;
}

/* Back to the frontend: provider detached; the next plan decides. */
static void return_to_frontend(void) { provider_detach(); }

/* ------------------------------------------------------------------------- */

static void test_probe_registration(void)
{
    g_test = "registration";
    /* Registered once by the constructor; reports "not applied live". */
    CHECK(g_plugin.option_changed_regs == 1 && g_plugin.option_changed != NULL);
    CHECK(g_plugin.option_changed("max-coins", "true") == 0);
    CHECK(g_plugin.option_changed("remember-changes", "false") == 0);
    CHECK(g_plugin.refused_regs == 0);
    /* Before any activation: not active, whatever the plan says. */
    g_plugin.not_in_plan = 0;
    CHECK(ts2_cheat_menu_activated() == 0);
}

static void test_first_session(void)
{
    size_t n = 0;
    const ts2_cheat_external_input h = one("full-health", "Full Health", TS2_CHEAT_HEALTH_RECOMMENDED, 1);
    g_test = "first session";
    ts2_cheat_combined_reset();
    stub_reset();
    session("true", NULL, &h, 1);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK);
    CHECK(ts2_cheat_menu_activated() == 1);
    CHECK(ts2_cheat_session_sealed() == 1 && ts2_cheat_table_refs() == 0u);
    CHECK(ts2_cheat_engine_count() == 3u);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ARMED);   /* coins option */
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_OFF);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ARMED);   /* enabled = true */
    CHECK(provider_attach() == 1 && ts2_cheat_table_refs() == 1u);
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 4u && strcmp(S(g_view.items[3].label), "Full Health") == 0);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(HEALTH) == 14 && stub_u16(0x0B2226) == 99);
}

static void test_rebuild_refused_while_active(void)
{
    size_t n = 0, n2 = 0;
    const ts2_cheat_def *t, *t2;
    const ts2_cheat_def *d2;
    const char *label;
    const ts2_cheat_external_input other = one("other", "Other", "300B221F 0000", 1);
    g_test = "rebuild refused";
    /* Session 1 still running (sealed, menu attached). */
    t = ts2_cheat_active_table(&n);
    d2 = ts2_cheat_engine_def(2);
    label = g_view.items[3].label;
    CHECK(n == 3u && d2 == &t[2] && strcmp(S(label), "Full Health") == 0);
    /* Any rebuild is refused and changes nothing. */
    g_st[0] = TS2_CHEAT_EXT_OK;
    CHECK(ts2_cheat_combined_build(&other, 1, g_st) == TS2_CHEAT_BUILD_ERR_SEALED);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_NOT_CHECKED);
    CHECK(ts2_cheat_combined_build(NULL, 0, NULL) == TS2_CHEAT_BUILD_ERR_SEALED);
    t2 = ts2_cheat_active_table(&n2);
    CHECK(t2 == t && n2 == 3u);
    CHECK(strcmp(S(t[2].id), "full-health") == 0 && first_start(t, n2, 2) == 0x800B221Eu);
    CHECK(ts2_cheat_engine_def(2) == d2 && ts2_cheat_engine_count() == 3u);
    CHECK(g_view.items[3].label == label && strcmp(S(label), "Full Health") == 0);
    /* Opening a new session while the menu still holds the table: refused,
     * table and sealing untouched. */
    CHECK(ts2_cheat_session_begin() == 0);
    CHECK(ts2_cheat_session_sealed() == 1 && ts2_cheat_active_table(&n2) == t && n2 == 3u);
    CHECK(strcmp(S(t[2].label), "Full Health") == 0);
    /* An activation in this state (should not happen; defensive): the engine
     * re-activates on the SAME table, the view stays bound. */
    stub_reset();
    session("true", NULL, NULL, 0);
    CHECK(ts2_cheat_active_table(&n2) == t && n2 == 3u);
    CHECK(ts2_cheat_engine_count() == 3u && ts2_cheat_engine_def(2) == d2);
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_view.bound[2] == 1);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(HEALTH) == 14);
}

static void test_return_and_second_session_enabled(void)
{
    size_t n = 0;
    const ts2_cheat_def *t;
    const ts2_cheat_external_input h2 = one("half-health", "Half Health", "800B221E 0007", 0);
    g_test = "second session (enabled)";
    /* In session 1 the player switched Infinite Lives on in the menu. */
    CHECK(ts2_cheat_engine_set_enabled_at(1, 1) == 1);
    return_to_frontend();
    CHECK(ts2_cheat_table_refs() == 0u);
    /* Session 2: the feature is in the new plan, other options, another
     * external entry. */
    stub_reset();
    session(NULL, NULL, &h2, 1);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK);
    CHECK(ts2_cheat_menu_activated() == 1);
    t = ts2_cheat_active_table(&n);
    CHECK(n == 3u && strcmp(S(t[2].id), "half-health") == 0);
    /* Fresh start state: nothing carried over from session 1. */
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_OFF);    /* option now unset */
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_OFF);    /* session-1 toggle gone */
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_OFF);    /* enabled = false */
    CHECK(ts2_cheat_engine_stats_at(2).vblanks == 0 && ts2_cheat_engine_stats_at(2).applied == 0);
    CHECK(provider_attach() == 1);
    CHECK(strcmp(S(g_view.items[3].label), "Half Health") == 0);
    CHECK(strcmp(S(g_view.items[3].key), "cheat_menu.ext.half-health") == 0);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 0);
    return_to_frontend();
    /* Session 3 without any build: built-ins only, nothing external left. */
    stub_reset();
    session(NULL, "true", NULL, 0);
    t = ts2_cheat_active_table(&n);
    CHECK(n == 2u && ts2_cheat_active_external_count() == 0u);
    CHECK(ts2_cheat_engine_count() == 2u && ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    CHECK(provider_attach() == 1);
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 3u);
    return_to_frontend();
}

static void test_second_session_disabled(void)
{
    g_test = "second session (disabled)";
    /* Session 2's plan does not contain the feature: the runtime does not
     * activate the plugin (and never calls its VBlank). The flag from the
     * earlier activation is still set inside the plugin -- the plan probe
     * must keep the menu out anyway. */
    g_plugin.not_in_plan = 1;
    CHECK(ts2_cheat_menu_activated() == 0);
    CHECK(provider_attach() == 0);
    CHECK(ts2_cheat_table_refs() == 0u && g_attached == 0);
    /* Session 3 with the feature again: active, attaches. */
    stub_reset();
    session(NULL, NULL, NULL, 0);
    CHECK(ts2_cheat_menu_activated() == 1);
    CHECK(provider_attach() == 1);
    return_to_frontend();
    /* Plan query answered by the runtime, not cached. */
    CHECK(g_plugin.live_capable_queries > 0);
}

static void test_reset_and_refs(void)
{
    g_test = "references";
    ts2_cheat_combined_reset();
    CHECK(ts2_cheat_table_refs() == 0u && ts2_cheat_session_sealed() == 0);
    ts2_cheat_table_ref_release();              /* underflow guarded */
    CHECK(ts2_cheat_table_refs() == 0u);
    ts2_cheat_table_ref_acquire();
    ts2_cheat_table_ref_acquire();
    CHECK(ts2_cheat_session_begin() == 0);
    CHECK(ts2_cheat_combined_build(NULL, 0, NULL) == TS2_CHEAT_BUILD_ERR_SEALED);
    ts2_cheat_table_ref_release();
    CHECK(ts2_cheat_session_begin() == 0);      /* still one holder */
    ts2_cheat_table_ref_release();
    CHECK(ts2_cheat_session_begin() == 1);
    CHECK(ts2_cheat_combined_build(NULL, 0, NULL) == TS2_CHEAT_BUILD_OK);
    ts2_cheat_session_seal();
    CHECK(ts2_cheat_combined_build(NULL, 0, NULL) == TS2_CHEAT_BUILD_ERR_SEALED);
    CHECK(ts2_cheat_session_begin() == 1 && ts2_cheat_session_sealed() == 0);
    /* Engine shutdown drops its table pointer. */
    CHECK(ts2_cheat_engine_init(1) == 1 && ts2_cheat_engine_count() == 2u);
    ts2_cheat_engine_shutdown();
    CHECK(ts2_cheat_engine_count() == 0u && ts2_cheat_engine_def(0) == NULL);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_UNAVAILABLE);
    stub_reset();
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 0 && g_stub.started_calls == 0);
    ts2_cheat_combined_reset();
}

int main(void)
{
    test_probe_registration();
    test_first_session();
    test_rebuild_refused_while_active();
    test_return_and_second_session_enabled();
    test_second_session_disabled();
    test_reset_and_refs();
    printf("cheat lifecycle: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
