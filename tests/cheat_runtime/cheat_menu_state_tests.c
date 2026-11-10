/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 2B: engine state model + cheat_menu_state bridge (mailbox and
 * snapshot). Real engine, adapter, TS2 policy and Action Replay core;
 * psxrecomp services are stubs. No guest memory. */
#include "cheat_menu_state.h"
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
#include "ts2_cheat_engine.h"

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

#define COINS_OFFSET 0x0B2226u

static cheat_menu_state g_ui;

/* Guest-memory traffic seen by the runtime stubs. */
static uint32_t traffic(void)
{
    return g_stub.read_calls + g_stub.write_calls + g_stub.started_calls;
}

/* ------------------------------------------------------------------------- */

static void test_engine_unavailable_before_setup(void)
{
    g_test = "engine unavailable";
    stub_reset();
    /* First test in this process: no activation/init yet. */
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_UNAVAILABLE);
    CHECK(ts2_cheat_engine_set_enabled(1) == 0);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_UNAVAILABLE);

    /* The bridge drops a request the engine cannot take. */
    cheat_menu_state_init(&g_ui);
    cheat_menu_state_request(&g_ui, 1);
    CHECK(cheat_menu_state_sync(&g_ui) == 0);
    CHECK(cheat_menu_state_pending(&g_ui, NULL) == 0);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_UNAVAILABLE);
    CHECK(cheat_menu_state_display_enabled(&g_ui) == 0);
    CHECK(traffic() == 0);
    CHECK(strcmp(ts2_cheat_state_name(TS2_CHEAT_UNAVAILABLE), "unavailable") == 0);
}

static void test_engine_states(void)
{
    g_test = "engine states";
    stub_reset();
    CHECK(ts2_cheat_engine_init(0) == 1);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_OFF);
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_OFF && g_stub.write_calls == 0);

    CHECK(ts2_cheat_engine_set_enabled(1) == 1);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ARMED);     /* no VBlank yet */
    g_stub.game_started = 0;
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ARMED);     /* waiting for game start */
    g_stub.game_started = 1;
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ACTIVE && g_stub.write_calls == 1);

    /* Restore: ARMED until the next VBlank re-applies. */
    ts2_cheat_engine_savestate();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ARMED && ts2_cheat_engine_enabled() == 1);
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ACTIVE && g_stub.write_calls == 2);

    /* Losing game-started (reset) drops back to ARMED. */
    g_stub.game_started = 0;
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ARMED && g_stub.write_calls == 2);
    g_stub.game_started = 1;

    CHECK(ts2_cheat_engine_set_enabled(0) == 1);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_OFF);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 2);
    CHECK(strcmp(ts2_cheat_state_name(TS2_CHEAT_ACTIVE), "active") == 0);
}

static void test_engine_error_fail_closed(void)
{
    int k;
    g_test = "engine error";
    stub_reset();
    CHECK(ts2_cheat_engine_init(1) == 1);
    g_stub.drop_writes = 1;
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ERROR);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(ts2_cheat_engine_get_stats().last_status == PSXCHEAT_ERR_MEMORY_WRITE);

    /* Never re-enabled by the engine: VBlanks and restores keep ERROR. */
    g_stub.drop_writes = 0;
    for (k = 0; k < 5; ++k) ts2_cheat_engine_vblank();
    ts2_cheat_engine_savestate();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ERROR && g_stub.write_calls == 1);

    /* Explicit enable re-arms and runs again. */
    CHECK(ts2_cheat_engine_set_enabled(1) == 1);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ARMED);
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ACTIVE && g_stub.write_calls == 2);

    /* Explicit disable from ERROR switches off and clears the error. The
     * game changes the value first: a dropped write of the value RAM already
     * holds would read back fine (documented read-back limitation). */
    g_stub.ram[COINS_OFFSET] = 7;
    g_stub.drop_writes = 1;
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_ERROR);
    CHECK(ts2_cheat_engine_set_enabled(0) == 1);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_OFF);
}

static void test_launcher_initial_state(void)
{
    g_test = "launcher initial state";
    stub_reset();
    g_plugin.option_value = NULL;
    ts2_cheat_engine_activate();
    cheat_menu_state_init(&g_ui);
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_OFF);
    CHECK(cheat_menu_state_display_enabled(&g_ui) == 0);

    g_plugin.option_value = "true";
    ts2_cheat_engine_activate();
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ARMED);
    CHECK(cheat_menu_state_display_enabled(&g_ui) == 1);
    CHECK(g_stub.write_calls == 0);  /* activation never writes */
    g_plugin.option_value = NULL;
}

static void test_mailbox(void)
{
    int value = -1;
    g_test = "mailbox";
    stub_reset();
    CHECK(ts2_cheat_engine_init(0) == 1);
    cheat_menu_state_init(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_UNAVAILABLE);  /* before first sync */
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_OFF);

    /* A request only reaches the engine at sync (main thread). */
    cheat_menu_state_request(&g_ui, 1);
    CHECK(cheat_menu_state_pending(&g_ui, &value) == 1 && value == 1);
    CHECK(cheat_menu_state_display_enabled(&g_ui) == 1);   /* immediate UI feedback */
    CHECK(ts2_cheat_engine_enabled() == 0);                /* engine untouched */
    CHECK(cheat_menu_state_sync(&g_ui) == 1);
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(cheat_menu_state_pending(&g_ui, NULL) == 0);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ARMED);

    /* The latest request wins. */
    cheat_menu_state_request(&g_ui, 0);
    cheat_menu_state_request(&g_ui, 1);
    cheat_menu_state_request(&g_ui, 0);
    CHECK(cheat_menu_state_sync(&g_ui) == 1);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_OFF);
    CHECK(cheat_menu_state_sync(&g_ui) == 0);              /* nothing pending */
    CHECK(g_stub.write_calls == 0);
}

static void test_pause_then_vblank(void)
{
    int k;
    g_test = "pause / VBlank";
    stub_reset();
    CHECK(ts2_cheat_engine_init(0) == 1);
    cheat_menu_state_init(&g_ui);
    cheat_menu_state_sync(&g_ui);

    /* Overlay open = host pause: no VBlank. The request reaches the engine
     * but nothing is written. */
    cheat_menu_state_request(&g_ui, 1);
    for (k = 0; k < 10; ++k) cheat_menu_state_sync(&g_ui);   /* is_open each frame */
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ARMED);
    CHECK(g_stub.write_calls == 0);

    /* Resume: the next VBlank writes, the next sync shows ACTIVE. */
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 1 && stub_u16(COINS_OFFSET) == 99);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ARMED);  /* not synced yet */
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ACTIVE);
    CHECK(cheat_menu_state_snapshot(&g_ui).applied == 1);

    /* Disable while paused: no write after resume, no restore. */
    cheat_menu_state_request(&g_ui, 0);
    cheat_menu_state_sync(&g_ui);
    g_stub.ram[COINS_OFFSET] = 5;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 1 && stub_u16(COINS_OFFSET) == 5);
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_OFF);
}

static void test_error_published(void)
{
    int k;
    cheat_menu_snapshot snap;
    g_test = "error published";
    stub_reset();
    CHECK(ts2_cheat_engine_init(1) == 1);
    cheat_menu_state_init(&g_ui);
    g_stub.drop_writes = 1;
    ts2_cheat_engine_vblank();
    cheat_menu_state_sync(&g_ui);
    snap = cheat_menu_state_snapshot(&g_ui);
    CHECK(snap.state == TS2_CHEAT_ERROR && snap.enabled == 0);
    CHECK(snap.last_status == PSXCHEAT_ERR_MEMORY_WRITE);
    CHECK(snap.last_status_name && strcmp(snap.last_status_name, "memory write failed") == 0);
    CHECK(snap.failures == 1);
    CHECK(cheat_menu_state_display_enabled(&g_ui) == 0);

    /* No automatic re-enable through syncs or VBlanks. */
    g_stub.drop_writes = 0;
    for (k = 0; k < 5; ++k) { ts2_cheat_engine_vblank(); cheat_menu_state_sync(&g_ui); }
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ERROR);
    CHECK(g_stub.write_calls == 1);

    /* Explicit re-enable from the overlay. */
    cheat_menu_state_request(&g_ui, 1);
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ARMED);
    ts2_cheat_engine_vblank();
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ACTIVE);
    CHECK(g_stub.write_calls == 2);
}

static void test_savestate_snapshot(void)
{
    g_test = "savestate";
    stub_reset();
    CHECK(ts2_cheat_engine_init(1) == 1);
    cheat_menu_state_init(&g_ui);
    ts2_cheat_engine_vblank();
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ACTIVE);
    ts2_cheat_engine_savestate();
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ARMED);
    CHECK(cheat_menu_state_snapshot(&g_ui).enabled == 1);
    ts2_cheat_engine_vblank();
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).state == TS2_CHEAT_ACTIVE);
}

static void test_bridge_never_touches_memory(void)
{
    uint32_t before;
    int k, value;
    cheat_menu_snapshot snap;
    g_test = "no guest memory from the bridge";
    stub_reset();
    CHECK(ts2_cheat_engine_init(0) == 1);
    cheat_menu_state_init(&g_ui);
    before = traffic();
    /* Everything the render thread may call, plus main-thread sync. */
    for (k = 0; k < 100; ++k) {
        cheat_menu_state_request(&g_ui, k & 1);
        (void)cheat_menu_state_pending(&g_ui, &value);
        (void)cheat_menu_state_display_enabled(&g_ui);
        snap = cheat_menu_state_snapshot(&g_ui);
        (void)snap;
        cheat_menu_state_sync(&g_ui);
    }
    CHECK(traffic() == before);
    CHECK(traffic() == 0);
}

static void test_open_close_cycles(void)
{
    int k;
    g_test = "open/close cycles";
    stub_reset();
    CHECK(ts2_cheat_engine_init(0) == 1);
    cheat_menu_state_init(&g_ui);
    /* 50 cycles: open (pause), toggle, close, one VBlank of gameplay. */
    for (k = 0; k < 50; ++k) {
        cheat_menu_state_request(&g_ui, (k % 2) == 0);
        cheat_menu_state_sync(&g_ui);
        ts2_cheat_engine_vblank();
        cheat_menu_state_sync(&g_ui);
        CHECK(cheat_menu_state_snapshot(&g_ui).state ==
              ((k % 2) == 0 ? TS2_CHEAT_ACTIVE : TS2_CHEAT_OFF));
    }
    CHECK(g_stub.write_calls == 25);  /* only the enabled cycles wrote */
    CHECK(g_stub.bad_addresses == 0);
}

static void test_generation(void)
{
    uint32_t g0, g1;
    g_test = "generation";
    stub_reset();
    CHECK(ts2_cheat_engine_init(0) == 1);
    cheat_menu_state_init(&g_ui);
    cheat_menu_state_sync(&g_ui);
    g0 = cheat_menu_state_snapshot(&g_ui).generation;
    cheat_menu_state_sync(&g_ui);
    CHECK(cheat_menu_state_snapshot(&g_ui).generation == g0);  /* unchanged */
    cheat_menu_state_request(&g_ui, 1);
    cheat_menu_state_sync(&g_ui);
    g1 = cheat_menu_state_snapshot(&g_ui).generation;
    CHECK(g1 == g0 + 1u);
}

static void test_null_safety(void)
{
    cheat_menu_snapshot snap;
    g_test = "null safety";
    cheat_menu_state_init(NULL);
    cheat_menu_state_request(NULL, 1);
    CHECK(cheat_menu_state_pending(NULL, NULL) == 0);
    CHECK(cheat_menu_state_display_enabled(NULL) == 0);
    CHECK(cheat_menu_state_sync(NULL) == 0);
    snap = cheat_menu_state_snapshot(NULL);
    CHECK(snap.state == TS2_CHEAT_UNAVAILABLE && snap.last_status_name != NULL);
}

int main(void)
{
    test_engine_unavailable_before_setup();  /* must run first */
    test_engine_states();
    test_engine_error_fail_closed();
    test_launcher_initial_state();
    test_mailbox();
    test_pause_then_vblank();
    test_error_published();
    test_savestate_snapshot();
    test_bridge_never_touches_memory();
    test_open_close_cycles();
    test_generation();
    test_null_safety();
    printf("cheat menu state: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
