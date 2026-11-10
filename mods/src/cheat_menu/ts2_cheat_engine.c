/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "ts2_cheat_engine.h"

#include <string.h>

#include "mod_plugins.h"  /* psx_mod_game_started, psx_mod_current_option_value */
#include "psxcheat_psxrecomp.h"
#include "ts2_cheat_memory.h"

typedef struct ts2_cheat_engine {
    int ready;
    int enabled;
    psxcheat_program program;
    psxcheat_psx_adapter adapter;
    ts2_cheat_engine_stats stats;
} ts2_cheat_engine;

/* Emulation thread only (see the header); no lock by design. */
static ts2_cheat_engine g_engine;

int ts2_cheat_engine_init(int enabled)
{
    psxcheat_psx_config cfg;
    psxcheat_error err;
    static const char code[] = TS2_CHEAT_MAX_COINS_CODE;

    memset(&g_engine, 0, sizeof g_engine);
    g_engine.stats.last_status = PSXCHEAT_OK;
    if (psxcheat_parse(code, sizeof code - 1u, &g_engine.program, &err) != PSXCHEAT_OK ||
        g_engine.program.count == 0u) {
        g_engine.stats.last_status = err.status;
        return 0;
    }
    if (!ts2_cheat_adapter_config(&cfg) ||
        psxcheat_psx_init(&g_engine.adapter, &cfg) != PSXCHEAT_PSX_OK) {
        g_engine.stats.last_status = PSXCHEAT_ERR_INVALID_PROGRAM;
        return 0;
    }
    g_engine.ready = 1;
    g_engine.enabled = enabled != 0;
    return 1;
}

void ts2_cheat_engine_activate(void)
{
    char value[16];
    const int opted_in =
        psx_mod_current_option_value(TS2_CHEAT_MAX_COINS_OPTION, value, sizeof value) &&
        strcmp(value, "true") == 0;
    (void)ts2_cheat_engine_init(opted_in);
}

int ts2_cheat_engine_ready(void) { return g_engine.ready; }
int ts2_cheat_engine_enabled(void) { return g_engine.enabled; }
ts2_cheat_engine_stats ts2_cheat_engine_get_stats(void) { return g_engine.stats; }

int ts2_cheat_engine_set_enabled(int enabled)
{
    if (!g_engine.ready) return 0;
    g_engine.enabled = enabled != 0;
    return 1;
}

void ts2_cheat_engine_vblank(void)
{
    psxcheat_memory memory;
    psxcheat_error err;
    psxcheat_status st;

    ++g_engine.stats.vblanks;
    if (!g_engine.ready || !g_engine.enabled) return;
    /* Checked here, not only in the adapter: before the game starts the
     * adapter would refuse the span and the run would count as a failure. */
    if (!psx_mod_game_started()) {
        ++g_engine.stats.not_started;
        return;
    }
    if (!psxcheat_psx_begin(&g_engine.adapter)) {
        ++g_engine.stats.busy;
        return;
    }
    memory = psxcheat_psx_backend(&g_engine.adapter);
    st = psxcheat_execute(&g_engine.program, &memory, NULL, &err);
    psxcheat_psx_end(&g_engine.adapter);  /* every successful begin is closed */

    g_engine.stats.last_status = st;
    if (st == PSXCHEAT_OK) {
        ++g_engine.stats.applied;
    } else {
        /* Fail safe: stop writing until explicitly re-enabled. */
        ++g_engine.stats.failures;
        g_engine.enabled = 0;
    }
}

void ts2_cheat_engine_savestate(void)
{
    /* The restored guest RAM is authoritative; nothing is written here. The
     * enabled state is host state and is kept, so an enabled cheat applies
     * again at the next VBlank. */
    ++g_engine.stats.savestates;
}
