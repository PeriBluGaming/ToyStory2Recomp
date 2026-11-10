/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef CHEAT_MENU_VIEW_H
#define CHEAT_MENU_VIEW_H

/* Cheat Menu (Phase 2C): the menu rows and their recomp-ui model callbacks.
 *
 * Rows, in order:
 *   0  Resume Game  (ACTION; the provider handles it)
 *   1  Max Coins    (BOOL; 800B2226 0063)
 *
 * The Max Coins row goes through cheat_menu_state (the mailbox/snapshot
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
 * the persist hook, once, with the engine's resulting value. Nothing else
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
#define CHEAT_MENU_KEY_RESUME     "cheat_menu.resume"
#define CHEAT_MENU_KEY_MAX_COINS  "cheat_menu.max_coins"
#define CHEAT_MENU_ROW_COUNT      2u

/* Hand one option value over for the next start. option_id is never NULL;
 * value is "true" or "false". Returns 1 when it was handed over, 0 when it
 * could not be (no host callback, refused). Main thread, UI mutex held. */
typedef int (*cheat_menu_persist_fn)(void *ctx, const char *option_id, const char *value);

/* Outcome of the last sync, for the status text. */
typedef enum cheat_menu_persist_result {
    CHEAT_MENU_PERSIST_NONE = 0,   /* nothing to persist (or remember off) */
    CHEAT_MENU_PERSIST_QUEUED,     /* handed to the host; disk not confirmed */
    CHEAT_MENU_PERSIST_FAILED      /* remember on, but could not hand over */
} cheat_menu_persist_result;

typedef struct cheat_menu_view {
    cheat_menu_state state;
    int remember;                       /* launcher "remember-changes" */
    cheat_menu_persist_fn persist;
    void *persist_ctx;
    cheat_menu_persist_result last_persist;
    uint32_t persist_attempts;
    /* Description of the Max Coins row; the item points at this buffer, so
     * the instance must not move once a menu was created from it. */
    char coins_text[96];
    char status_text[96];
    RecompRuntimeUiItem items[CHEAT_MENU_ROW_COUNT];
} cheat_menu_view;

/* Rows and an UNAVAILABLE snapshot; no engine access. */
void cheat_menu_view_init(cheat_menu_view *v);

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

/* Footer text describing the current state after a toggle ("Max Coins:
 * armed, applies after resume", ...). Valid until the next sync. */
const char *cheat_menu_view_status(cheat_menu_view *v);

/* The model callbacks, exposed for tests. */
int cheat_menu_view_get_value(void *ctx, const RecompRuntimeUiItem *item, int *value);
int cheat_menu_view_set_value(void *ctx, const RecompRuntimeUiItem *item, int value);
int cheat_menu_view_is_enabled(void *ctx, const RecompRuntimeUiItem *item);

#ifdef __cplusplus
}
#endif

#endif
