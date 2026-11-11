/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "ts2_cheat_engine.h"

#include <string.h>

#include "mod_plugins.h"  /* psx_mod_game_started, psx_mod_current_option_value */
#include "psxcheat_psxrecomp.h"
#include "ts2_cheat_external.h"
#include "ts2_cheat_memory.h"

/* One independent cheat. */
typedef struct ts2_cheat_slot {
    int ready;
    int enabled;
    /* 1 after a failed run disabled the cheat; cleared only by an explicit
     * set_enabled() call, never by the engine itself. */
    int error;
    /* 1 once a run completed since the cheat was last (re-)enabled or the
     * guest state was restored: ARMED vs ACTIVE. */
    int applied_since_enable;
    ts2_cheat_def_status setup;     /* why the slot is not ready */
    psxcheat_program program;
    psxcheat_psx_adapter adapter;   /* only this cheat's ranges */
    ts2_cheat_engine_stats stats;
} ts2_cheat_slot;

typedef struct ts2_cheat_engine {
    const ts2_cheat_def *defs;
    size_t count;                   /* 0 before setup or after a table error */
    ts2_cheat_table_status table;
    ts2_cheat_slot slots[TS2_CHEAT_MAX];
} ts2_cheat_engine;

/* Emulation thread only (see the header); no lock by design. */
static ts2_cheat_engine g_engine;

static ts2_cheat_slot *slot_at(size_t index)
{
    return index < g_engine.count ? &g_engine.slots[index] : NULL;
}

/* Parse and configure one slot whose definition already validated. */
static int setup_slot(ts2_cheat_slot *s, const ts2_cheat_def *d)
{
    psxcheat_psx_config cfg;
    psxcheat_error err;
    if (psxcheat_parse(d->code, strlen(d->code), &s->program, &err) != PSXCHEAT_OK ||
        s->program.count == 0u) {
        s->stats.last_status = err.status;
        s->setup = TS2_CHEAT_DEF_ERR_PARSE;
        return 0;
    }
    cfg.writable = d->ranges;
    cfg.writable_count = d->range_count;
    cfg.verify_readback = 1;
    if (psxcheat_psx_init(&s->adapter, &cfg) != PSXCHEAT_PSX_OK ||
        !ts2_cheat_program_targets_valid(&s->program, &s->adapter)) {
        /* Validated already; refusing here too keeps setup fail-closed. */
        memset(&s->program, 0, sizeof s->program);
        s->stats.last_status = PSXCHEAT_ERR_INVALID_PROGRAM;
        s->setup = TS2_CHEAT_DEF_ERR_TARGET;
        return 0;
    }
    s->ready = 1;
    return 1;
}

ts2_cheat_table_status ts2_cheat_engine_init_table(const ts2_cheat_def *defs, size_t n,
                                                   const psxcheat_psx_range *policy,
                                                   size_t policy_n,
                                                   const int *enabled)
{
    ts2_cheat_def_status per_def[TS2_CHEAT_MAX];
    size_t i;

    memset(&g_engine, 0, sizeof g_engine);
    g_engine.table = ts2_cheat_validate_table(defs, n, policy, policy_n, per_def);
    if (g_engine.table != TS2_CHEAT_TABLE_OK) return g_engine.table;  /* no slot */
    g_engine.defs = defs;
    g_engine.count = n;
    for (i = 0; i < n; ++i) {
        ts2_cheat_slot *s = &g_engine.slots[i];
        s->stats.last_status = PSXCHEAT_OK;
        s->setup = per_def[i];
        if (per_def[i] != TS2_CHEAT_DEF_OK) {
            /* UNAVAILABLE; last_status says why in core terms. A failed
             * parse leaves program.count 0, so it can never run. */
            if (per_def[i] == TS2_CHEAT_DEF_ERR_PARSE && defs[i].code) {
                psxcheat_error err;
                s->stats.last_status = psxcheat_parse(defs[i].code, strlen(defs[i].code),
                                                      &s->program, &err) == PSXCHEAT_OK
                    ? PSXCHEAT_ERR_INVALID_PROGRAM : err.status;  /* parsed, but empty */
                memset(&s->program, 0, sizeof s->program);
            } else {
                s->stats.last_status = PSXCHEAT_ERR_INVALID_PROGRAM;
            }
            continue;
        }
        if (setup_slot(s, &defs[i])) s->enabled = enabled && enabled[i] != 0;
    }
    return g_engine.table;
}

int ts2_cheat_engine_init(int enabled)
{
    size_t n = 0, policy_n = 0;
    const ts2_cheat_def *defs = ts2_cheat_table(&n);
    const psxcheat_psx_range *policy = ts2_cheat_writable_ranges(&policy_n);
    int initial[TS2_CHEAT_MAX];
    memset(initial, 0, sizeof initial);
    initial[0] = enabled != 0;
    (void)ts2_cheat_engine_init_table(defs, n, policy, policy_n, initial);
    return ts2_cheat_engine_ready_at(0);
}

void ts2_cheat_engine_activate(void)
{
    /* The active table: built-ins, plus accepted external entries once a
     * combined table was built (ts2_cheat_external.h). Built-ins start from
     * their launcher option, external entries from their own start state. */
    size_t n = 0, policy_n = 0, i;
    const ts2_cheat_def *defs = ts2_cheat_active_table(&n);
    const size_t builtins = ts2_cheat_active_builtin_count();
    const psxcheat_psx_range *policy = ts2_cheat_active_verified(&policy_n);
    int initial[TS2_CHEAT_MAX];
    memset(initial, 0, sizeof initial);
    for (i = 0; i < n && i < TS2_CHEAT_MAX; ++i) {
        char value[16];
        if (i >= builtins) {
            initial[i] = ts2_cheat_active_external_enabled(i);
            continue;
        }
        initial[i] = defs[i].id &&
            psx_mod_current_option_value(defs[i].id, value, sizeof value) &&
            strcmp(value, "true") == 0;
    }
    (void)ts2_cheat_engine_init_table(defs, n, policy, policy_n, initial);
}

ts2_cheat_table_status ts2_cheat_engine_table_status(void) { return g_engine.table; }
size_t ts2_cheat_engine_count(void) { return g_engine.count; }

const ts2_cheat_def *ts2_cheat_engine_def(size_t index)
{
    return slot_at(index) ? &g_engine.defs[index] : NULL;
}

ts2_cheat_def_status ts2_cheat_engine_setup_status(size_t index)
{
    const ts2_cheat_slot *s = slot_at(index);
    return s ? s->setup : TS2_CHEAT_DEF_ERR_NOT_CHECKED;
}

int ts2_cheat_engine_ready_at(size_t index)
{
    const ts2_cheat_slot *s = slot_at(index);
    return s ? s->ready : 0;
}

int ts2_cheat_engine_enabled_at(size_t index)
{
    const ts2_cheat_slot *s = slot_at(index);
    return s ? s->enabled : 0;
}

ts2_cheat_engine_stats ts2_cheat_engine_stats_at(size_t index)
{
    ts2_cheat_engine_stats none;
    const ts2_cheat_slot *s = slot_at(index);
    if (s) return s->stats;
    memset(&none, 0, sizeof none);
    none.last_status = PSXCHEAT_OK;
    return none;
}

int ts2_cheat_engine_set_enabled_at(size_t index, int enabled)
{
    ts2_cheat_slot *s = slot_at(index);
    if (!s || !s->ready) return 0;
    /* An explicit choice acknowledges a previous error: enabling re-arms
     * (the next eligible VBlank runs again), disabling just switches off. */
    s->error = 0;
    s->applied_since_enable = 0;
    s->enabled = enabled != 0;
    return 1;
}

ts2_cheat_state ts2_cheat_engine_state_at(size_t index)
{
    const ts2_cheat_slot *s = slot_at(index);
    if (!s || !s->ready) return TS2_CHEAT_UNAVAILABLE;
    if (!s->enabled) return s->error ? TS2_CHEAT_ERROR : TS2_CHEAT_OFF;
    return s->applied_since_enable ? TS2_CHEAT_ACTIVE : TS2_CHEAT_ARMED;
}

/* ---- Index-0 wrappers (legacy single-cheat API) --------------------------- */

int ts2_cheat_engine_ready(void) { return ts2_cheat_engine_ready_at(0); }
int ts2_cheat_engine_enabled(void) { return ts2_cheat_engine_enabled_at(0); }
ts2_cheat_engine_stats ts2_cheat_engine_get_stats(void) { return ts2_cheat_engine_stats_at(0); }
int ts2_cheat_engine_set_enabled(int enabled) { return ts2_cheat_engine_set_enabled_at(0, enabled); }
ts2_cheat_state ts2_cheat_engine_state(void) { return ts2_cheat_engine_state_at(0); }

const char *ts2_cheat_state_name(ts2_cheat_state state)
{
    switch (state) {
    case TS2_CHEAT_UNAVAILABLE: return "unavailable";
    case TS2_CHEAT_OFF: return "off";
    case TS2_CHEAT_ARMED: return "armed";
    case TS2_CHEAT_ACTIVE: return "active";
    case TS2_CHEAT_ERROR: return "error";
    }
    return "unknown";
}

/* ---- Callbacks -------------------------------------------------------------- */

static void run_slot(ts2_cheat_slot *s)
{
    psxcheat_memory memory;
    psxcheat_error err;
    psxcheat_status st;

    if (!psxcheat_psx_begin(&s->adapter)) {
        ++s->stats.busy;
        return;
    }
    memory = psxcheat_psx_backend(&s->adapter);
    st = psxcheat_execute(&s->program, &memory, NULL, &err);
    psxcheat_psx_end(&s->adapter);  /* every successful begin is closed */

    s->stats.last_status = st;
    if (st == PSXCHEAT_OK) {
        ++s->stats.applied;
        s->applied_since_enable = 1;
    } else {
        /* Fail closed: stop writing and show the error until the cheat is
         * explicitly re-enabled; never re-enabled automatically. Only this
         * slot: the other cheats keep running. A multi-line program that
         * failed mid-run keeps the writes of the lines before the failing
         * one (no rollback); psxcheat_execute validated every line first. */
        ++s->stats.failures;
        s->enabled = 0;
        s->error = 1;
        s->applied_since_enable = 0;
    }
}

void ts2_cheat_engine_vblank(void)
{
    size_t i;
    int started = -1;  /* asked at most once per VBlank, only when needed */

    for (i = 0; i < g_engine.count; ++i) {
        ts2_cheat_slot *s = &g_engine.slots[i];
        ++s->stats.vblanks;
        if (!s->ready || !s->enabled) continue;
        /* Checked here, not only in the adapter: before the game starts the
         * adapter would refuse the span and the run would count as a failure. */
        if (started < 0) started = psx_mod_game_started() != 0;
        if (!started) {
            ++s->stats.not_started;
            s->applied_since_enable = 0;  /* waiting: ARMED, not ACTIVE */
            continue;
        }
        run_slot(s);
    }
}

void ts2_cheat_engine_savestate(void)
{
    /* The restored guest RAM is authoritative; nothing is written here. The
     * enabled state is host state and is kept, so an enabled cheat applies
     * again at the next VBlank; until then it reports ARMED, because the
     * restored RAM may not hold the cheat's value. An error stays shown. */
    size_t i;
    for (i = 0; i < g_engine.count; ++i) {
        ++g_engine.slots[i].stats.savestates;
        g_engine.slots[i].applied_since_enable = 0;
    }
}
