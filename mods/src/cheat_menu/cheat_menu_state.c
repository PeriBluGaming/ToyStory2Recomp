/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "cheat_menu_state.h"

#include <string.h>

static cheat_menu_snapshot unavailable(void)
{
    cheat_menu_snapshot empty;
    memset(&empty, 0, sizeof empty);
    empty.state = TS2_CHEAT_UNAVAILABLE;
    empty.last_status = PSXCHEAT_OK;
    empty.last_status_name = psxcheat_status_name(PSXCHEAT_OK);
    return empty;
}

void cheat_menu_state_init(cheat_menu_state *s)
{
    size_t i;
    if (!s) return;
    memset(s, 0, sizeof *s);
    for (i = 0; i < CHEAT_MENU_MAX_CHEATS; ++i) {
        s->pending[i] = -1;
        s->snapshots[i] = unavailable();
    }
    s->snapshot = s->snapshots[0];
}

int cheat_menu_state_request_at(cheat_menu_state *s, size_t index, int enabled)
{
    if (!s || index >= CHEAT_MENU_MAX_CHEATS) return 0;
    s->pending[index] = enabled ? 1 : 0;
    return 1;
}

int cheat_menu_state_pending_at(const cheat_menu_state *s, size_t index, int *enabled)
{
    if (!s || index >= CHEAT_MENU_MAX_CHEATS || s->pending[index] < 0) return 0;
    if (enabled) *enabled = s->pending[index];
    return 1;
}

int cheat_menu_state_any_pending(const cheat_menu_state *s)
{
    size_t i;
    if (!s) return 0;
    for (i = 0; i < CHEAT_MENU_MAX_CHEATS; ++i)
        if (s->pending[i] >= 0) return 1;
    return 0;
}

void cheat_menu_state_discard_at(cheat_menu_state *s, size_t index)
{
    if (s && index < CHEAT_MENU_MAX_CHEATS) s->pending[index] = -1;
}

int cheat_menu_state_display_enabled_at(const cheat_menu_state *s, size_t index)
{
    if (!s || index >= CHEAT_MENU_MAX_CHEATS) return 0;
    if (s->pending[index] >= 0) return s->pending[index];
    return s->snapshots[index].enabled;
}

cheat_menu_snapshot cheat_menu_state_snapshot_at(const cheat_menu_state *s, size_t index)
{
    if (!s || index >= CHEAT_MENU_MAX_CHEATS) return unavailable();
    return s->snapshots[index];
}

/* Index-0 versions (single-cheat API). */
void cheat_menu_state_request(cheat_menu_state *s, int enabled)
{
    (void)cheat_menu_state_request_at(s, 0, enabled);
}

int cheat_menu_state_pending(const cheat_menu_state *s, int *enabled)
{
    return cheat_menu_state_pending_at(s, 0, enabled);
}

int cheat_menu_state_display_enabled(const cheat_menu_state *s)
{
    return cheat_menu_state_display_enabled_at(s, 0);
}

cheat_menu_snapshot cheat_menu_state_snapshot(const cheat_menu_state *s)
{
    return s ? s->snapshot : unavailable();
}

static int differs(const cheat_menu_snapshot *a, const cheat_menu_snapshot *b)
{
    return a->state != b->state || a->enabled != b->enabled ||
           a->last_status != b->last_status || a->failures != b->failures ||
           a->applied != b->applied;
}

int cheat_menu_state_sync(cheat_menu_state *s)
{
    cheat_menu_snapshot next[CHEAT_MENU_MAX_CHEATS];
    size_t i, count;
    int changed = 0;

    if (!s) return 0;
    count = ts2_cheat_engine_count();
    if (count > CHEAT_MENU_MAX_CHEATS) count = CHEAT_MENU_MAX_CHEATS;
    s->requested_mask = 0;
    s->applied_mask = 0;
    for (i = 0; i < CHEAT_MENU_MAX_CHEATS; ++i) {
        if (s->pending[i] < 0) continue;
        s->requested_mask |= 1u << i;
        /* Not ready / no such slot: dropped, the row shows the real state. */
        if (i < count && ts2_cheat_engine_set_enabled_at(i, s->pending[i]))
            s->applied_mask |= 1u << i;
        s->pending[i] = -1;
    }
    for (i = 0; i < CHEAT_MENU_MAX_CHEATS; ++i) {
        if (i < count) {
            const ts2_cheat_engine_stats st = ts2_cheat_engine_stats_at(i);
            next[i].state = ts2_cheat_engine_state_at(i);
            next[i].enabled = ts2_cheat_engine_enabled_at(i);
            next[i].last_status = st.last_status;
            next[i].last_status_name = psxcheat_status_name(st.last_status);
            next[i].failures = st.failures;
            next[i].applied = st.applied;
        } else {
            next[i] = unavailable();
        }
        if (differs(&next[i], &s->snapshots[i])) changed = 1;
    }
    if (changed) ++s->generation;
    for (i = 0; i < CHEAT_MENU_MAX_CHEATS; ++i) {
        next[i].generation = s->generation;
        s->snapshots[i] = next[i];
    }
    s->count = count;
    s->snapshot = s->snapshots[0];
    return s->applied_mask != 0u;
}
