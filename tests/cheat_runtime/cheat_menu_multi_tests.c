/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 2E-C: several independent cheat rows. A synthetic N = 3 table runs
 * through the real engine, bridge, view and recomp-ui's runtime menu model;
 * persistence goes to a fake PsxMenuHost.mod_set that only records calls.
 * psxrecomp services are stubs; no guest memory, no file. */
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

#define N_OF(a) (sizeof (a) / sizeof (a)[0])
#define OFF(addr) ((addr) - 0x80000000u)

/* ---- Synthetic table (inside the real D1/D2 rules) ---------------------- */

static const psxcheat_psx_range k_policy[] = {
    { 0x800A0500u, 0x800A0510u },
    { 0x800A0600u, 0x800A0604u },
    { 0x800A0700u, 0x800A0708u },
};
static const psxcheat_psx_range r_a[] = { { 0x800A0500u, 0x800A0502u } };
static const psxcheat_psx_range r_b[] = { { 0x800A0600u, 0x800A0604u } };
static const psxcheat_psx_range r_c[] = { { 0x800A0700u, 0x800A0702u },
                                          { 0x800A0704u, 0x800A0706u } };

static const ts2_cheat_def k_abc[] = {
    { "a", "Cheat A", "800A0500 1234", r_a, 1, NULL, "sets A", "A held" },
    { "b", "Cheat B", "300A0600 0011\n300A0601 0022\n800A0602 3344", r_b, 1,
      NULL, NULL, NULL },
    { "c", "Cheat C", "800A0700 0001\n800A0704 0002", r_c, 2,
      "cheat_menu.custom_c", NULL, NULL },
};

/* B broken: only B must be unavailable. */
static const ts2_cheat_def k_bad_b[] = {
    { "a", "Cheat A", "800A0500 1234", r_a, 1, NULL, NULL, NULL },
    { "b", "Cheat B", "800A0600 0001\nnot a code", r_b, 1, NULL, NULL, NULL },
    { "c", "Cheat C", "800A0700 0001", r_c, 2, NULL, NULL, NULL },
};

/* ---- Fake host ---------------------------------------------------------- */

typedef struct call { char p[64], f[32], o[32], v[16]; } call;
static call g_calls[32];
static int g_ncalls, g_netplay;
static PsxMenuHost g_host;

static void cp(char *d, size_t n, const char *s) { snprintf(d, n, "%s", s ? s : "(null)"); }

static int fake_mod_set(const char *p, const char *f, const char *o, const char *v)
{
    if (g_ncalls < 32) {
        cp(g_calls[g_ncalls].p, sizeof g_calls[0].p, p);
        cp(g_calls[g_ncalls].f, sizeof g_calls[0].f, f);
        cp(g_calls[g_ncalls].o, sizeof g_calls[0].o, o);
        cp(g_calls[g_ncalls].v, sizeof g_calls[0].v, v);
    }
    ++g_ncalls;
    return 0;
}
static int fake_netplay(void) { return g_netplay; }

static void host_reset(void)
{
    memset(&g_host, 0, sizeof g_host);
    g_host.abi_version = PSX_MENU_ABI_VERSION;
    g_host.mod_set = fake_mod_set;
    g_host.netplay_active = fake_netplay;
    memset(g_calls, 0, sizeof g_calls);
    g_ncalls = 0;
    g_netplay = 0;
}

static int call_is(int k, const char *option, const char *value)
{
    return k < g_ncalls && k < 32 &&
           strcmp(g_calls[k].p, "toystory2.enhancement.cheat-menu") == 0 &&
           strcmp(g_calls[k].f, "cheat-menu") == 0 &&
           strcmp(g_calls[k].o, option) == 0 && strcmp(g_calls[k].v, value) == 0;
}

/* ---- Provider stand-ins ------------------------------------------------- */

static cheat_menu_view g_view;
static RecompRuntimeUi *g_ui;
static int g_resume_runs;

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
    if (open) recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
}

static void attach(const ts2_cheat_def *defs, size_t n, int remember)
{
    RecompRuntimeUiConfig cfg;
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    cheat_menu_view_init_table(&g_view, defs, n);
    cheat_menu_view_set_persist(&g_view, remember, cheat_menu_persist_via_host, &g_host);
    (void)cheat_menu_view_sync(&g_view);
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "Cheat Menu";
    cfg.items = cheat_menu_view_items(&g_view, &cfg.item_count);
    cheat_menu_view_callbacks(&g_view, &cfg.callbacks);
    cfg.callbacks.run_action = cb_action;
    cfg.callbacks.visibility_changed = cb_visibility;
    g_ui = recomp_runtime_ui_create(&cfg);
}

static void engine(const ts2_cheat_def *defs, size_t n, const int *on)
{
    (void)ts2_cheat_engine_init_table(defs, n, k_policy, N_OF(k_policy), on);
}

static void in(RecompRuntimeUiInput i) { recomp_runtime_ui_handle_input(g_ui, i, 1, 0); }
static int sync(void) { return cheat_menu_view_sync(&g_view); }

/* Open, go `down` rows from Resume, press `key`, sync (p_event), close. */
static void toggle_row(int down, RecompRuntimeUiInput key)
{
    int k;
    recomp_runtime_ui_open(g_ui);
    for (k = 0; k < down; ++k) in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(key);
    (void)sync();
    recomp_runtime_ui_close(g_ui);
}

/* Value column of cheat row i: -1 Unavailable, else 0/1. */
static int value(size_t i)
{
    int v = -7;
    if (!cheat_menu_view_get_value(&g_view, &g_view.items[1 + i], &v)) return -1;
    return v;
}

static int text_has(size_t i, const char *needle)
{
    return strstr(g_view.row_text[i], needle) != NULL;
}

static ts2_cheat_state snap(size_t i) { return cheat_menu_state_snapshot_at(&g_view.state, i).state; }

/* ------------------------------------------------------------------------- */

static void test_rows_from_table(void)
{
    size_t n = 0;
    const RecompRuntimeUiItem *it;
    g_test = "rows";
    stub_reset();
    host_reset();
    engine(k_abc, 3, NULL);
    attach(k_abc, 3, 0);
    CHECK(g_ui != NULL);
    it = cheat_menu_view_items(&g_view, &n);
    CHECK(n == 4u);
    CHECK(strcmp(it[0].key, CHEAT_MENU_KEY_RESUME) == 0 && it[0].type == RECOMP_RUNTIME_UI_ACTION);
    CHECK(strcmp(it[1].key, "cheat_menu.a") == 0 && strcmp(it[1].label, "Cheat A") == 0);
    CHECK(strcmp(it[2].key, "cheat_menu.b") == 0 && strcmp(it[2].label, "Cheat B") == 0);
    CHECK(strcmp(it[3].key, "cheat_menu.custom_c") == 0);       /* menu_key override */
    CHECK(it[1].type == RECOMP_RUNTIME_UI_BOOL && it[3].type == RECOMP_RUNTIME_UI_BOOL);
    CHECK(it[1].description == g_view.row_text[0] && it[3].description == g_view.row_text[2]);
    CHECK(cheat_menu_view_row_index(&g_view, &it[0]) == -1);
    CHECK(cheat_menu_view_row_index(&g_view, &it[2]) == 1);
    {
        RecompRuntimeUiItem copy = it[3];          /* not from the array: key match */
        CHECK(cheat_menu_view_row_index(&g_view, &copy) == 2);
        copy.key = "cheat_menu.zzz";
        CHECK(cheat_menu_view_row_index(&g_view, &copy) == -1);
        copy.key = NULL;
        CHECK(cheat_menu_view_row_index(&g_view, &copy) == -1);
    }
    CHECK(value(0) == 0 && value(1) == 0 && value(2) == 0);
    CHECK(strcmp(g_view.row_text[0], "Off - sets A") == 0);       /* hint */
    CHECK(strcmp(g_view.row_text[1], "Off") == 0);                /* generic */

    /* Navigation over Resume + 3 rows, both directions, wrapping. */
    g_resume_runs = 0;
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);                            /* Resume first */
    CHECK(g_resume_runs == 1 && !recomp_runtime_ui_is_open(g_ui));
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN); in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN); in(RECOMP_RUNTIME_UI_INPUT_DOWN);  /* wraps */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_resume_runs == 2);
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_UP);                                /* wraps to C */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 2, NULL) == 1);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 0, NULL) == 0);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 1, NULL) == 0);
    (void)sync();
    CHECK(ts2_cheat_engine_enabled_at(2) == 1 && ts2_cheat_engine_enabled_at(0) == 0);
    recomp_runtime_ui_close(g_ui);
}

static void test_independent_toggles(void)
{
    uint32_t writes, g0;
    g_test = "independent";
    stub_reset();
    host_reset();
    engine(k_abc, 3, NULL);
    attach(k_abc, 3, 0);

    toggle_row(2, RECOMP_RUNTIME_UI_INPUT_ACCEPT);                 /* B on */
    CHECK(ts2_cheat_engine_enabled_at(1) == 1);
    CHECK(ts2_cheat_engine_enabled_at(0) == 0 && ts2_cheat_engine_enabled_at(2) == 0);
    CHECK(snap(1) == TS2_CHEAT_ARMED && snap(0) == TS2_CHEAT_OFF && snap(2) == TS2_CHEAT_OFF);
    CHECK(value(1) == 1 && value(0) == 0 && value(2) == 0);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Cheat B: armed, applies after resume") == 0);
    CHECK(text_has(1, "Armed") && text_has(0, "Off"));
    CHECK(g_stub.write_calls == 0);                                /* paused: nothing yet */

    toggle_row(1, RECOMP_RUNTIME_UI_INPUT_RIGHT);                  /* A on */
    toggle_row(3, RECOMP_RUNTIME_UI_INPUT_LEFT);                   /* C on */
    CHECK(ts2_cheat_engine_enabled_at(0) == 1 && ts2_cheat_engine_enabled_at(2) == 1);
    ts2_cheat_engine_vblank();
    g0 = g_view.state.generation;
    (void)sync();
    CHECK(g_view.state.generation == g0 + 1u);
    CHECK(snap(0) == TS2_CHEAT_ACTIVE && snap(1) == TS2_CHEAT_ACTIVE && snap(2) == TS2_CHEAT_ACTIVE);
    CHECK(strcmp(g_view.row_text[0], "Active - A held") == 0);
    CHECK(strcmp(g_view.row_text[1], "Active - applied every frame") == 0);
    CHECK(stub_u16(OFF(0x800A0500u)) == 0x1234 && stub_u16(OFF(0x800A0602u)) == 0x3344 &&
          stub_u16(OFF(0x800A0704u)) == 2);

    /* B off: A and C keep running. */
    toggle_row(2, RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(snap(1) == TS2_CHEAT_OFF && snap(0) == TS2_CHEAT_ACTIVE && snap(2) == TS2_CHEAT_ACTIVE);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Cheat B: off") == 0);
    writes = g_stub.write_calls;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == writes + 3u);                      /* A 1 + C 2 */

    /* Idle syncs (p_is_open every frame) change nothing and name row 1. */
    g0 = g_view.state.generation;
    (void)sync(); (void)sync();
    CHECK(g_view.state.generation == g0 + 1u);   /* the VBlank's stats, once */
    CHECK(g_view.state.requested_mask == 0u && g_view.state.applied_mask == 0u);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Cheat A: active") == 0);
}

static void test_rapid_input(void)
{
    int want = -1;
    g_test = "rapid input";
    stub_reset();
    host_reset();
    engine(k_abc, 3, NULL);
    attach(k_abc, 3, 0);
    recomp_runtime_ui_open(g_ui);
    /* Same row three times before a sync: the latest wins. */
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT); in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 0, &want) == 1 && want == 1);
    /* Then C on, then A off again: per row, independent. */
    in(RECOMP_RUNTIME_UI_INPUT_DOWN); in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    in(RECOMP_RUNTIME_UI_INPUT_UP); in(RECOMP_RUNTIME_UI_INPUT_UP);
    in(RECOMP_RUNTIME_UI_INPUT_LEFT);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 0, &want) == 1 && want == 0);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 2, &want) == 1 && want == 1);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 1, NULL) == 0);
    CHECK(value(0) == 0 && value(2) == 1);              /* immediate feedback */
    CHECK(ts2_cheat_engine_enabled_at(2) == 0);         /* engine untouched */
    CHECK(sync() == 1);
    CHECK(ts2_cheat_engine_enabled_at(0) == 0 && ts2_cheat_engine_enabled_at(2) == 1);
    CHECK(g_view.state.requested_mask == 0x5u && g_view.state.applied_mask == 0x5u);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Cheat A: off (+1 more)") == 0);
    /* Key repeat never toggles. */
    recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 1);
    CHECK(cheat_menu_state_any_pending(&g_view.state) == 0);
    recomp_runtime_ui_close(g_ui);
}

static void test_per_row_error_and_unavailable(void)
{
    static const int all_on[3] = { 1, 1, 1 };
    g_test = "error / unavailable";
    stub_reset();
    host_reset();
    engine(k_abc, 3, all_on);
    attach(k_abc, 3, 0);
    g_stub.drop_from = OFF(0x800A0601u);
    g_stub.drop_to = OFF(0x800A0602u);
    ts2_cheat_engine_vblank();
    g_stub.drop_to = 0;
    (void)sync();
    CHECK(snap(1) == TS2_CHEAT_ERROR && snap(0) == TS2_CHEAT_ACTIVE && snap(2) == TS2_CHEAT_ACTIVE);
    CHECK(value(1) == 0 && value(0) == 1 && value(2) == 1);
    CHECK(text_has(1, "Error (") && text_has(1, psxcheat_status_name(PSXCHEAT_ERR_MEMORY_WRITE)));
    CHECK(text_has(0, "Active") && text_has(2, "Active"));
    CHECK(cheat_menu_view_is_enabled(&g_view, &g_view.items[2]) == 1);
    toggle_row(2, RECOMP_RUNTIME_UI_INPUT_ACCEPT);                 /* retry B only */
    CHECK(snap(1) == TS2_CHEAT_ARMED && snap(0) == TS2_CHEAT_ACTIVE);

    /* A broken table entry: only its row is unavailable. */
    stub_reset();
    engine(k_bad_b, 3, all_on);
    attach(k_bad_b, 3, 0);
    CHECK(snap(1) == TS2_CHEAT_UNAVAILABLE && snap(0) == TS2_CHEAT_ARMED && snap(2) == TS2_CHEAT_ARMED);
    CHECK(value(1) == -1 && value(0) == 1);
    CHECK(cheat_menu_view_is_enabled(&g_view, &g_view.items[2]) == 0);
    CHECK(cheat_menu_view_is_enabled(&g_view, &g_view.items[1]) == 1);
    CHECK(cheat_menu_view_set_value(&g_view, &g_view.items[2], 1) == 0);
    CHECK(text_has(1, "Unavailable"));
    toggle_row(2, RECOMP_RUNTIME_UI_INPUT_ACCEPT);                 /* disabled row */
    CHECK(cheat_menu_state_any_pending(&g_view.state) == 0);
    CHECK(ts2_cheat_engine_enabled_at(0) == 1 && ts2_cheat_engine_enabled_at(2) == 1);
}

static void test_unbound_rows_never_reach_engine(void)
{
    g_test = "unbound";
    stub_reset();
    host_reset();
    /* The engine runs the production table (Max Coins at slot 0); the view
     * was built from the synthetic table: no row may switch Max Coins. */
    CHECK(ts2_cheat_engine_init(0) == 1);
    attach(k_abc, 3, 1);
    CHECK(snap(0) == TS2_CHEAT_OFF);                /* the engine's slot 0 ... */
    CHECK(value(0) == -1 && value(1) == -1);        /* ... but the rows are unbound */
    CHECK(text_has(0, "Unavailable"));
    CHECK(cheat_menu_view_set_value(&g_view, &g_view.items[1], 1) == 0);
    CHECK(cheat_menu_state_request_at(&g_view.state, 0, 1) == 1);   /* bypass the row */
    CHECK(sync() == 0);
    CHECK(ts2_cheat_engine_enabled() == 0);         /* Max Coins untouched */
    CHECK(g_ncalls == 0);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Cheat A: unavailable") == 0);
}

static void test_persistence_per_cheat(void)
{
    static const int all_on[3] = { 1, 1, 1 };
    g_test = "persistence";
    stub_reset();
    host_reset();
    engine(k_abc, 3, NULL);
    attach(k_abc, 3, 1);
    CHECK(g_ncalls == 0);

    toggle_row(2, RECOMP_RUNTIME_UI_INPUT_ACCEPT);                 /* B on */
    CHECK(g_ncalls == 1 && call_is(0, "b", "true"));
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "Cheat B: armed, applies after resume - saving for next start") == 0);

    /* A and C in one sync; A three times (latest wins: on). */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT); in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN); in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)sync();
    recomp_runtime_ui_close(g_ui);
    CHECK(g_ncalls == 3 && call_is(1, "a", "true") && call_is(2, "c", "true"));
    CHECK(g_view.persist_attempts == 3);

    /* Engine events persist nothing: running, ERROR, savestate. */
    ts2_cheat_engine_vblank();
    (void)sync();
    g_stub.drop_from = OFF(0x800A0700u);
    g_stub.drop_to = OFF(0x800A0702u);
    g_stub.ram[OFF(0x800A0700u)] = 9;
    ts2_cheat_engine_vblank();
    g_stub.drop_to = 0;
    (void)sync();
    CHECK(snap(2) == TS2_CHEAT_ERROR);
    ts2_cheat_engine_savestate();
    (void)sync();
    CHECK(g_ncalls == 3);

    toggle_row(3, RECOMP_RUNTIME_UI_INPUT_ACCEPT);                 /* explicit retry of C */
    CHECK(g_ncalls == 4 && call_is(3, "c", "true"));

    /* Hand-over refused (netplay): reported, not claimed. */
    g_netplay = 1;
    toggle_row(1, RECOMP_RUNTIME_UI_INPUT_ACCEPT);                 /* A off */
    CHECK(g_ncalls == 4 && g_view.last_persist == CHEAT_MENU_PERSIST_FAILED);
    CHECK(strcmp(cheat_menu_view_status(&g_view), "Cheat A: off - not saved") == 0);
    CHECK(ts2_cheat_engine_enabled_at(0) == 0);    /* the toggle itself applied */
    g_netplay = 0;

    /* Remember off: nothing, for any row. */
    engine(k_abc, 3, all_on);
    attach(k_abc, 3, 0);
    toggle_row(1, RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    toggle_row(2, RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    toggle_row(3, RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_ncalls == 4 && g_view.persist_attempts == 0);
    CHECK(ts2_cheat_engine_enabled_at(0) == 0 && ts2_cheat_engine_enabled_at(1) == 0 &&
          ts2_cheat_engine_enabled_at(2) == 0);
}

static void test_invalid_indices(void)
{
    cheat_menu_snapshot s;
    int want = -1;
    g_test = "invalid indices";
    stub_reset();
    host_reset();
    engine(k_abc, 3, NULL);
    attach(k_abc, 3, 1);
    CHECK(cheat_menu_state_request_at(&g_view.state, CHEAT_MENU_MAX_CHEATS, 1) == 0);
    CHECK(cheat_menu_state_request_at(&g_view.state, (size_t)-1, 1) == 0);
    CHECK(cheat_menu_state_request_at(NULL, 0, 1) == 0);
    CHECK(cheat_menu_state_pending_at(&g_view.state, 99, &want) == 0 && want == -1);
    CHECK(cheat_menu_state_display_enabled_at(&g_view.state, 99) == 0);
    s = cheat_menu_state_snapshot_at(&g_view.state, 99);
    CHECK(s.state == TS2_CHEAT_UNAVAILABLE && s.last_status_name != NULL);
    CHECK(cheat_menu_state_snapshot_at(&g_view.state, 5).state == TS2_CHEAT_UNAVAILABLE);
    cheat_menu_state_discard_at(&g_view.state, 99);                 /* no crash */
    /* A valid storage index the engine does not have: dropped at sync. */
    CHECK(cheat_menu_state_request_at(&g_view.state, 5, 1) == 1);
    CHECK(sync() == 0);
    CHECK(g_view.state.requested_mask == 0x20u && g_view.state.applied_mask == 0u);
    CHECK(cheat_menu_state_any_pending(&g_view.state) == 0);
    CHECK(g_ncalls == 0);
    CHECK(ts2_cheat_engine_enabled_at(0) == 0 && ts2_cheat_engine_enabled_at(1) == 0 &&
          ts2_cheat_engine_enabled_at(2) == 0);
    /* Tables the view refuses: Resume only. */
    {
        ts2_cheat_def many[CHEAT_MENU_MAX_CHEATS + 1u];
        size_t i, n = 7;
        for (i = 0; i < N_OF(many); ++i) many[i] = k_abc[0];
        cheat_menu_view_init_table(&g_view, many, N_OF(many));
        (void)cheat_menu_view_items(&g_view, &n);
        CHECK(n == 1u);
        cheat_menu_view_init_table(&g_view, NULL, 3);
        (void)cheat_menu_view_items(&g_view, &n);
        CHECK(n == 1u);
        CHECK(sync() == 0);
        CHECK(strcmp(cheat_menu_view_status(&g_view), "Cheats: unavailable") == 0);
    }
}

static void test_view_refuses_ambiguous_keys(void)
{
    ts2_cheat_def d[3];
    size_t n = 0;
    g_test = "view keys";
    stub_reset();
    host_reset();
    memcpy(d, k_abc, sizeof d);
    d[0].menu_key = "cheat_menu.b";            /* collides with B's generated key */
    engine(d, 3, NULL);                        /* fatal for the engine too */
    CHECK(ts2_cheat_engine_count() == 0u);
    attach(d, 3, 1);
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 1u);                            /* Resume only: no ambiguous rows */
    CHECK(g_ui != NULL);
    g_resume_runs = 0;
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);          /* one row: stays on Resume */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    CHECK(g_resume_runs == 1);
    CHECK(sync() == 0 && g_ncalls == 0);

    d[0].menu_key = "cheat_menu.resume";       /* reserved */
    cheat_menu_view_init_table(&g_view, d, 3);
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 1u);

    /* Distinct keys: all rows, keys as validated. */
    d[0].menu_key = "cheat_menu.Custom-1";
    engine(d, 3, NULL);
    attach(d, 3, 0);
    (void)cheat_menu_view_items(&g_view, &n);
    CHECK(n == 4u);
    CHECK(strcmp(g_view.items[1].key, "cheat_menu.Custom-1") == 0);
    CHECK(strcmp(g_view.items[2].key, "cheat_menu.b") == 0);
    CHECK(strcmp(g_view.items[3].key, "cheat_menu.custom_c") == 0);
    CHECK(value(0) == 0 && value(1) == 0 && value(2) == 0);
}

static void test_production_regression(void)
{
    size_t n = 0;
    const RecompRuntimeUiItem *it;
    g_test = "production";
    stub_reset();
    host_reset();
    CHECK(ts2_cheat_engine_init(0) == 1);
    cheat_menu_view_init(&g_view);
    it = cheat_menu_view_items(&g_view, &n);
    CHECK(n == 3u);
    CHECK(strcmp(it[2].key, "cheat_menu.infinite_lives") == 0 &&
          strcmp(it[2].label, "Infinite Lives") == 0);
    CHECK(strcmp(it[1].key, "cheat_menu.max_coins") == 0 &&
          strcmp(it[1].key, CHEAT_MENU_KEY_MAX_COINS) == 0);
    CHECK(strcmp(it[1].label, "Max Coins") == 0);
    (void)sync();
    CHECK(strcmp(g_view.row_text[0], "Off - code 800B2226 0063 holds coins at 99") == 0);
    cheat_menu_view_set_persist(&g_view, 1, cheat_menu_persist_via_host, &g_host);
    CHECK(cheat_menu_view_set_value(&g_view, &it[1], 1) == 1);
    (void)sync();
    CHECK(g_ncalls == 1 && call_is(0, "max-coins", "true"));
    ts2_cheat_engine_vblank();
    (void)sync();
    CHECK(strcmp(g_view.row_text[0], "Active - coins held at 99") == 0);
    CHECK(stub_u16(OFF(0x800B2226u)) == 99);
    CHECK(g_view.state.snapshot.state == TS2_CHEAT_ACTIVE);       /* legacy mirror */
}

int main(void)
{
    test_rows_from_table();
    test_independent_toggles();
    test_rapid_input();
    test_per_row_error_and_unavailable();
    test_unbound_rows_never_reach_engine();
    test_persistence_per_cheat();
    test_invalid_indices();
    test_view_refuses_ambiguous_keys();
    test_production_regression();
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    printf("cheat menu multi: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
