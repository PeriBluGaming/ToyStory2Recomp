/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "cheat_menu_state.h"

#include <string.h>

void cheat_menu_state_init(cheat_menu_state *s)
{
    if (!s) return;
    memset(s, 0, sizeof *s);
    s->pending = -1;
    s->snapshot.state = TS2_CHEAT_UNAVAILABLE;
    s->snapshot.last_status = PSXCHEAT_OK;
    s->snapshot.last_status_name = psxcheat_status_name(PSXCHEAT_OK);
}

void cheat_menu_state_request(cheat_menu_state *s, int enabled)
{
    if (s) s->pending = enabled ? 1 : 0;
}

int cheat_menu_state_pending(const cheat_menu_state *s, int *enabled)
{
    if (!s || s->pending < 0) return 0;
    if (enabled) *enabled = s->pending;
    return 1;
}

int cheat_menu_state_display_enabled(const cheat_menu_state *s)
{
    if (!s) return 0;
    if (s->pending >= 0) return s->pending;
    return s->snapshot.enabled;
}

int cheat_menu_state_sync(cheat_menu_state *s)
{
    cheat_menu_snapshot next;
    ts2_cheat_engine_stats stats;
    int applied = 0;

    if (!s) return 0;
    if (s->pending >= 0) {
        /* Not ready: the request is dropped, the row shows the real state. */
        applied = ts2_cheat_engine_set_enabled(s->pending);
        s->pending = -1;
    }
    stats = ts2_cheat_engine_get_stats();
    next.state = ts2_cheat_engine_state();
    next.enabled = ts2_cheat_engine_enabled();
    next.last_status = stats.last_status;
    next.last_status_name = psxcheat_status_name(stats.last_status);
    next.failures = stats.failures;
    next.applied = stats.applied;
    next.generation = s->snapshot.generation;
    if (next.state != s->snapshot.state || next.enabled != s->snapshot.enabled ||
        next.last_status != s->snapshot.last_status ||
        next.failures != s->snapshot.failures || next.applied != s->snapshot.applied)
        ++next.generation;
    s->snapshot = next;
    return applied;
}

cheat_menu_snapshot cheat_menu_state_snapshot(const cheat_menu_state *s)
{
    cheat_menu_snapshot empty;
    if (s) return s->snapshot;
    memset(&empty, 0, sizeof empty);
    empty.state = TS2_CHEAT_UNAVAILABLE;
    empty.last_status_name = psxcheat_status_name(PSXCHEAT_OK);
    return empty;
}
