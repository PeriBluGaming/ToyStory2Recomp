/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef CHEAT_MENU_STATE_H
#define CHEAT_MENU_STATE_H

/* Cheat Menu (Phase 2B): the bridge between the overlay and the cheat engine.
 *
 * The engine (ts2_cheat_engine) is not thread-safe and may only be used on
 * the emulation/main thread. The overlay renders on the GL/render thread.
 * This module keeps the two apart:
 *
 *   - a mailbox: the overlay records a requested enable/disable
 *     (cheat_menu_state_request); the main thread hands it to the engine
 *     (cheat_menu_state_sync);
 *   - a snapshot: cheat_menu_state_sync copies the engine's state into the
 *     instance; the render thread reads only that copy.
 *
 * Locking: the instance has no lock of its own. Every function must be
 * called with the overlay's UI mutex held. cheat_menu_state_sync must
 * additionally run on the main thread (provider is_open/event callbacks);
 * every other function may run on any thread. This module never touches
 * guest memory; sync only calls the engine's state functions.
 *
 * Slots (Phase 2E-C): one mailbox entry and one snapshot per engine slot
 * (cheat table index), independent of each other; the latest request wins
 * per slot. The single-cheat functions are the index-0 versions, and
 * `snapshot` mirrors snapshots[0] for them.
 *
 * Pause: requests made while the guest is frozen (the overlay holds a host
 * pause) reach the engine at the next sync, but the engine writes only at
 * the next VBlank, i.e. after the game resumes. */

#include <stdint.h>

#include "psxcheat.h"
#include "ts2_cheat_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cheat_menu_snapshot {
    ts2_cheat_state state;
    int enabled;                    /* engine's effective enabled flag */
    psxcheat_status last_status;    /* last run's status */
    const char *last_status_name;   /* static string, safe on any thread */
    uint32_t failures;
    uint32_t applied;
    uint32_t generation;            /* bumped whenever the copy changes */
} cheat_menu_snapshot;

#define CHEAT_MENU_MAX_CHEATS TS2_CHEAT_MAX

typedef struct cheat_menu_state {
    size_t count;                   /* engine slots at the last sync */
    int pending[CHEAT_MENU_MAX_CHEATS];   /* -1 none, 0 disable, 1 enable */
    cheat_menu_snapshot snapshots[CHEAT_MENU_MAX_CHEATS];
    cheat_menu_snapshot snapshot;   /* read-only mirror of snapshots[0] */
    uint32_t generation;            /* bumped when any snapshot changes */
    uint32_t requested_mask;        /* last sync: slots that had a request */
    uint32_t applied_mask;          /* last sync: requests the engine took */
} cheat_menu_state;

/* No pending request; snapshot UNAVAILABLE until the first sync. */
void cheat_menu_state_init(cheat_menu_state *s);

/* Record a request (the latest one wins). Any thread. */
void cheat_menu_state_request(cheat_menu_state *s, int enabled);

/* 1 and *enabled when a request is waiting. Any thread. */
int cheat_menu_state_pending(const cheat_menu_state *s, int *enabled);

/* The value the overlay shows: a pending request, else the snapshot's
 * effective state. Any thread. */
int cheat_menu_state_display_enabled(const cheat_menu_state *s);

/* MAIN THREAD ONLY. Hand every pending request to its engine slot (dropped
 * when that slot is not ready or does not exist), then refresh every
 * snapshot. Returns 1 when at least one request was applied; requested_mask
 * and applied_mask say which. */
int cheat_menu_state_sync(cheat_menu_state *s);

/* Copy of the snapshot. Any thread. */
cheat_menu_snapshot cheat_menu_state_snapshot(const cheat_menu_state *s);

/* ---- Per slot. Any thread unless noted. An index at or beyond
 * CHEAT_MENU_MAX_CHEATS is invalid: request_at returns 0 and records
 * nothing, pending_at returns 0, display_enabled_at returns 0 and
 * snapshot_at returns an UNAVAILABLE snapshot. A valid index the engine
 * does not have (beyond its count) is dropped at the next sync. */
int cheat_menu_state_request_at(cheat_menu_state *s, size_t index, int enabled);
int cheat_menu_state_pending_at(const cheat_menu_state *s, size_t index, int *enabled);
int cheat_menu_state_any_pending(const cheat_menu_state *s);
int cheat_menu_state_display_enabled_at(const cheat_menu_state *s, size_t index);
cheat_menu_snapshot cheat_menu_state_snapshot_at(const cheat_menu_state *s, size_t index);
/* Drop a pending request without handing it to the engine. */
void cheat_menu_state_discard_at(cheat_menu_state *s, size_t index);

#ifdef __cplusplus
}
#endif

#endif
