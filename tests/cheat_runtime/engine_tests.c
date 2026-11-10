/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Cheat Menu plugin + engine lifecycle tests: the REAL cheat_menu_plugin.c
 * (its constructor registers with the stub registry before main), the real
 * engine, adapter, TS2 policy and Action Replay core; psxrecomp services are
 * stubs (psx_runtime_stub.c, plugin_stub.c). No guest memory. */
#include "cheat_menu.h"
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
#include "ts2_cheat_engine.h"

#include <stdio.h>
#include <stdlib.h>

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

static void activate(const char *option)
{
    g_plugin.option_value = option;
    g_plugin.activation();
}

/* ------------------------------------------------------------------------- */

static void test_registration(void)
{
    g_test = "registration";
    /* The constructor ran before main: one callback of each kind, one
     * provider, no refused (duplicate/foreign) registration. */
    CHECK(g_plugin.activation_regs == 1);
    CHECK(g_plugin.vblank_regs == 1);
    CHECK(g_plugin.savestate_regs == 1);
    CHECK(g_plugin.refused_regs == 0);
    CHECK(g_plugin.activation && g_plugin.vblank && g_plugin.savestate);
    CHECK(g_plugin.provider_sets == 1 && g_plugin.provider == ts2_cheat_menu_provider());
}

static void test_before_activation(void)
{
    g_test = "before activation";
    stub_reset();
    /* A plan without the feature never activates it; even a stray VBlank
     * must not write. */
    CHECK(ts2_cheat_menu_activated() == 0);
    CHECK(ts2_cheat_engine_ready() == 0);
    g_plugin.vblank();
    g_plugin.savestate();
    CHECK(g_stub.write_calls == 0 && g_stub.read_calls == 0 && g_stub.started_calls == 0);
}

static void test_default_disabled(void)
{
    static const char *const not_opted_in[] = { NULL, "false", "", "TRUE", "1", "yes" };
    size_t i;
    int k;
    g_test = "default disabled";
    for (i = 0; i < sizeof not_opted_in / sizeof not_opted_in[0]; ++i) {
        stub_reset();
        activate(not_opted_in[i]);
        CHECK(ts2_cheat_menu_activated() == 1);
        CHECK(ts2_cheat_engine_ready() == 1);
        CHECK(ts2_cheat_engine_enabled() == 0);
        for (k = 0; k < 5; ++k) g_plugin.vblank();
        CHECK(g_stub.write_calls == 0 && g_stub.read_calls == 0);
        CHECK(ts2_cheat_engine_get_stats().vblanks == 5 && ts2_cheat_engine_get_stats().applied == 0);
    }
}

static void test_opt_in_and_once_per_vblank(void)
{
    int k;
    g_test = "opt-in / once per VBlank";
    stub_reset();
    activate("true");
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(g_stub.write_calls == 0);  /* activation itself writes nothing */

    /* Not started: nothing written, not counted as a failure. */
    g_stub.game_started = 0;
    for (k = 0; k < 3; ++k) g_plugin.vblank();
    CHECK(g_stub.write_calls == 0);
    CHECK(ts2_cheat_engine_get_stats().not_started == 3);
    CHECK(ts2_cheat_engine_get_stats().failures == 0 && ts2_cheat_engine_enabled() == 1);

    /* Started: exactly one 16-bit write of 99 per VBlank. */
    g_stub.game_started = 1;
    for (k = 1; k <= 4; ++k) {
        g_plugin.vblank();
        CHECK(g_stub.write_calls == (uint32_t)k);
    }
    CHECK(g_stub.log[0].address == 0x800B2226u && g_stub.log[0].width == 2 &&
          g_stub.log[0].value == 0x0063u);
    CHECK(g_stub.log[3].address == 0x800B2226u);
    CHECK(stub_u16(COINS_OFFSET) == 99);
    CHECK(stub_u8(COINS_OFFSET - 1u) == 0 && stub_u8(COINS_OFFSET + 2u) == 0);  /* neighbours */
    CHECK(g_stub.bad_addresses == 0);
    CHECK(ts2_cheat_engine_get_stats().applied == 4 && ts2_cheat_engine_get_stats().busy == 0);

    /* The game changing the value is overwritten at the next VBlank. */
    g_stub.ram[COINS_OFFSET] = 12;
    g_plugin.vblank();
    CHECK(stub_u16(COINS_OFFSET) == 99);
}

static void test_disable_enable(void)
{
    int k;
    g_test = "explicit disable / enable";
    stub_reset();
    activate("true");
    g_plugin.vblank();
    CHECK(g_stub.write_calls == 1);

    CHECK(ts2_cheat_engine_set_enabled(0) == 1 && ts2_cheat_engine_enabled() == 0);
    g_stub.ram[COINS_OFFSET] = 40;   /* game keeps playing */
    for (k = 0; k < 3; ++k) g_plugin.vblank();
    CHECK(g_stub.write_calls == 1);
    CHECK(stub_u16(COINS_OFFSET) == 40);  /* no restore, no write */

    CHECK(ts2_cheat_engine_set_enabled(1) == 1);
    g_plugin.vblank();
    CHECK(g_stub.write_calls == 2 && stub_u16(COINS_OFFSET) == 99);
}

static void test_savestate(void)
{
    g_test = "savestate";
    stub_reset();
    activate("true");
    g_plugin.vblank();
    CHECK(g_stub.write_calls == 1);

    /* A restore brings back other RAM; the callback itself touches nothing
     * and the enabled state survives. */
    g_stub.ram[COINS_OFFSET] = 7;
    g_stub.ram[COINS_OFFSET + 1u] = 0;
    g_plugin.savestate();
    CHECK(g_stub.write_calls == 1 && g_stub.read_calls == 1);  /* the earlier read-back only */
    CHECK(ts2_cheat_engine_enabled() == 1);
    CHECK(ts2_cheat_engine_get_stats().savestates == 1);
    CHECK(stub_u16(COINS_OFFSET) == 7);
    g_plugin.vblank();
    CHECK(g_stub.write_calls == 2 && stub_u16(COINS_OFFSET) == 99);

    /* A disabled cheat stays disabled across a restore. */
    CHECK(ts2_cheat_engine_set_enabled(0) == 1);
    g_plugin.savestate();
    g_plugin.vblank();
    CHECK(ts2_cheat_engine_enabled() == 0 && g_stub.write_calls == 2);
}

static void test_failure_disables(void)
{
    int k;
    g_test = "failure disables";
    stub_reset();
    activate("true");
    /* A runtime that drops the write: read-back mismatch -> the run fails,
     * the cheat switches itself off and stops writing. */
    g_stub.drop_writes = 1;
    g_plugin.vblank();
    CHECK(g_stub.write_calls == 1);
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(ts2_cheat_engine_get_stats().failures == 1);
    CHECK(ts2_cheat_engine_get_stats().last_status == PSXCHEAT_ERR_MEMORY_WRITE);
    for (k = 0; k < 3; ++k) g_plugin.vblank();
    CHECK(g_stub.write_calls == 1);
}

static void test_reactivation_resets(void)
{
    g_test = "reactivation";
    stub_reset();
    activate("true");
    g_plugin.vblank();
    /* A new activation (next session) starts from the option again. */
    activate("false");
    CHECK(ts2_cheat_engine_enabled() == 0);
    CHECK(ts2_cheat_engine_get_stats().applied == 0 && ts2_cheat_engine_get_stats().vblanks == 0);
    g_plugin.vblank();
    CHECK(g_stub.write_calls == 1);
}

int main(void)
{
    test_registration();
    test_before_activation();
    test_default_disabled();
    test_opt_in_and_once_per_vblank();
    test_disable_enable();
    test_savestate();
    test_failure_disables();
    test_reactivation_resets();
    printf("cheat engine lifecycle: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
