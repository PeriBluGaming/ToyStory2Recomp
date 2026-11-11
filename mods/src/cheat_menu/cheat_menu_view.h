/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef CHEAT_MENU_VIEW_H
#define CHEAT_MENU_VIEW_H

/* Cheat Menu: the menu rows and their recomp-ui model callbacks.
 *
 * Rows, in order (Phase 2E-C: generated from the cheat table):
 *   0      Resume Game  (ACTION; the provider handles it)
 *   1..N   one BOOL row per cheat table entry, in table order; row 1 + i is
 *          engine slot i. Production: N = 1, Max Coins (800B2226 0063).
 *
 * A row is bound to its engine slot only while the engine runs the very
 * same table entry (ts2_cheat_engine_def(i) == &defs[i]); an unbound row
 * shows Unavailable and its requests are dropped, so a view/engine table
 * mismatch can never switch the wrong cheat.
 *
 * Every cheat row goes through cheat_menu_state (the mailbox/snapshot
 * bridge), never through the engine directly:
 *   - get_value / is_enabled read the snapshot (and a pending request);
 *   - set_value only records a request in the mailbox;
 *   - cheat_menu_view_sync hands the request to the engine and refreshes
 *     the snapshot and the row's description text.
 * The callbacks are therefore safe on any thread, including the render
 * thread (recomp-ui calls get_value / is_enabled while drawing).
 *
 * Persistence (Phase 2D, launcher option "remember-changes"): when enabled,
 * cheat_menu_view_sync passes each request the engine actually accepted to
 * the persist hook, once per cheat, with that cheat's own option id (its
 * table id) and the engine's resulting value. Nothing else
 * is ever persisted: not a dropped request (UNAVAILABLE), not the engine's
 * own fail-closed switch-off (ERROR), not a savestate/rewind restore, not
 * the launcher's initial value. The hook only hands the value over (the
 * provider uses PsxMenuHost.mod_set); whether it reached the disk is not
 * known here, and the status text never claims it.
 *
 * Locking: like cheat_menu_state, the view has no lock of its own. Every
 * function, and every recomp-ui call that may reach the callbacks
 * (handle_input, render_imgui), must run with the overlay's UI mutex held.
 * cheat_menu_view_sync must additionally run on the main (emulation) thread.
 * This module never touches guest memory. */

#include <stddef.h>

#include "cheat_menu_state.h"
#include "recomp_runtime_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHEAT_MENU_SECTION        "Cheat Menu"
#define CHEAT_MENU_KEY_RESUME     TS2_CHEAT_KEY_RESUME     /* "cheat_menu.resume" */
#define CHEAT_MENU_KEY_MAX_COINS  "cheat_menu.max_coins"   /* production row 1 */
#define CHEAT_MENU_MAX_ROWS       (1u + CHEAT_MENU_MAX_CHEATS)

/* Hand one option value over for the next start. option_id is never NULL;
 * value is "true" or "false". Returns 1 when it was handed over, 0 when it
 * could not be (no host callback, refused). Main thread, UI mutex held. */
typedef int (*cheat_menu_persist_fn)(void *ctx, const char *option_id, const char *value);

/* Outcome of the last sync, for the status text. */
typedef enum cheat_menu_persist_result {
    CHEAT_MENU_PERSIST_NONE = 0,   /* nothing to persist (or remember off) */
    CHEAT_MENU_PERSIST_QUEUED,     /* handed to the host; disk not confirmed */
    CHEAT_MENU_PERSIST_FAILED,     /* remember on, but could not hand over */
    CHEAT_MENU_PERSIST_SESSION     /* remember on, but only external (session-only) rows */
} cheat_menu_persist_result;

typedef struct cheat_menu_view {
    cheat_menu_state state;
    const ts2_cheat_def *defs;          /* table the rows were built from */
    size_t cheat_count;                 /* cheat rows (0..CHEAT_MENU_MAX_CHEATS) */
    int bound[CHEAT_MENU_MAX_CHEATS];   /* row i is engine slot i (last sync) */
    int remember;                       /* launcher "remember-changes" */
    cheat_menu_persist_fn persist;
    void *persist_ctx;
    cheat_menu_persist_result last_persist;
    uint32_t persist_attempts;
    /* Per cheat row: description and (generated) key. The items point at
     * these buffers, so the instance must not move once a menu was created
     * from it. */
    char row_text[CHEAT_MENU_MAX_CHEATS][96];
    char row_key[CHEAT_MENU_MAX_CHEATS][TS2_CHEAT_KEY_MAX + 1u];
    char status_text[128];
    RecompRuntimeUiItem items[CHEAT_MENU_MAX_ROWS];
} cheat_menu_view;

/* Rows from the active table (ts2_cheat_active_table: built-ins, plus
 * accepted external cheats), UNAVAILABLE snapshots; no engine access.
 * External rows are session-only: never persisted. */
void cheat_menu_view_init(cheat_menu_view *v);
/* Rows from `defs` (tests; must be the table the engine runs for the rows
 * to bind). NULL, more than CHEAT_MENU_MAX_CHEATS entries, or any invalid
 * or duplicate effective menu key (ts2_cheat_validate_menu_keys): Resume
 * only, so no two rows can ever share a key. */
void cheat_menu_view_init_table(cheat_menu_view *v, const ts2_cheat_def *defs, size_t n);

/* Cheat row of a recomp-ui item: its slot index, or -1 (Resume, unknown). */
int cheat_menu_view_row_index(const cheat_menu_view *v, const RecompRuntimeUiItem *item);

const RecompRuntimeUiItem *cheat_menu_view_items(const cheat_menu_view *v, size_t *count);

/* Fills context, get_value, set_value and is_enabled. The caller adds
 * run_action (Resume Game) and visibility_changed. */
void cheat_menu_view_callbacks(cheat_menu_view *v, RecompRuntimeUiCallbacks *cb);

/* Configure persistence (default: off, no hook). remember != 0 enables it;
 * a NULL hook with remember on makes every accepted toggle report FAILED. */
void cheat_menu_view_set_persist(cheat_menu_view *v, int remember,
                                 cheat_menu_persist_fn fn, void *ctx);

/* The provider's persist hook; ctx is the attached `const PsxMenuHost *`.
 * Calls host->mod_set(TS2_CHEAT_MENU_PACKAGE_ID, TS2_CHEAT_MENU_FEATURE_ID,
 * option_id, value) and returns 1. Returns 0 without calling anything when
 * option_id/value/host/mod_set is NULL or netplay is active (mod_set would
 * refuse and save nothing). mod_set's return value only says whether a
 * plugin applied the option live (this feature registers none), so it is
 * not a persistence result and is ignored. */
int cheat_menu_persist_via_host(void *ctx, const char *option_id, const char *value);

/* MAIN THREAD ONLY. Apply a pending request, refresh snapshot and text, and
 * (remember on) persist an accepted request. Returns 1 when a request was
 * handed to the engine. */
int cheat_menu_view_sync(cheat_menu_view *v);

/* Footer text for the last sync's request ("Max Coins: armed, applies after
 * resume", ...): the first requested row (" (+k more)" when several), or
 * row 1 when nothing was requested. Valid until the next call. */
const char *cheat_menu_view_status(cheat_menu_view *v);

/* The model callbacks, exposed for tests. */
int cheat_menu_view_get_value(void *ctx, const RecompRuntimeUiItem *item, int *value);
int cheat_menu_view_set_value(void *ctx, const RecompRuntimeUiItem *item, int value);
int cheat_menu_view_is_enabled(void *ctx, const RecompRuntimeUiItem *item);

#ifdef __cplusplus
}
#endif

#endif
