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

typedef struct cheat_menu_view {
    cheat_menu_state state;
    /* Description of the Max Coins row; the item points at this buffer, so
     * the instance must not move once a menu was created from it. */
    char coins_text[96];
    char status_text[64];
    RecompRuntimeUiItem items[CHEAT_MENU_ROW_COUNT];
} cheat_menu_view;

/* Rows and an UNAVAILABLE snapshot; no engine access. */
void cheat_menu_view_init(cheat_menu_view *v);

const RecompRuntimeUiItem *cheat_menu_view_items(const cheat_menu_view *v, size_t *count);

/* Fills context, get_value, set_value and is_enabled. The caller adds
 * run_action (Resume Game) and visibility_changed. */
void cheat_menu_view_callbacks(cheat_menu_view *v, RecompRuntimeUiCallbacks *cb);

/* MAIN THREAD ONLY. Apply a pending request, refresh snapshot and text.
 * Returns 1 when a request was handed to the engine. */
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
