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

typedef struct cheat_menu_state {
    int pending;                    /* -1 none, 0 disable, 1 enable */
    cheat_menu_snapshot snapshot;
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

/* MAIN THREAD ONLY. Hand a pending request to the engine (dropped when the
 * engine is not ready), then refresh the snapshot. Returns 1 when a request
 * was applied. */
int cheat_menu_state_sync(cheat_menu_state *s);

/* Copy of the snapshot. Any thread. */
cheat_menu_snapshot cheat_menu_state_snapshot(const cheat_menu_state *s);

#ifdef __cplusplus
}
#endif

#endif
