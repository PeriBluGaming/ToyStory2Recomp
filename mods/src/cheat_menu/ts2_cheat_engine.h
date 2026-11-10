/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_ENGINE_H
#define TS2_CHEAT_ENGINE_H

/* Cheat Menu (Phase 1C-C2a): the developer-only runtime engine for ONE
 * verified test cheat, "Max Coins" (800B2226 0063).
 *
 * Lifecycle (psxrecomp plugin "toystory2.cheat-menu", cheat_menu_plugin.c):
 *   activation  -> ts2_cheat_engine_activate(): parse the code, configure the
 *                  memory adapter with the TS2 policy, read the package option
 *                  "max-coins" (default false). Off unless explicitly opted in.
 *   VBlank      -> ts2_cheat_engine_vblank(): when enabled and the game has
 *                  started, open the adapter context, run the program once,
 *                  close the context. A failure disables the cheat and marks
 *                  an error that stays until an explicit set_enabled().
 *   savestate   -> ts2_cheat_engine_savestate(): no guest access; the enabled
 *                  state is host state and survives a restore, so the next
 *                  VBlank writes again.
 *
 * States (ts2_cheat_engine_state):
 *   UNAVAILABLE  not set up (no activation yet, or setup failed)
 *   OFF          disabled, no pending error
 *   ARMED        enabled, no run completed yet since enabling / restoring
 *                (no VBlank yet, guest paused, or game not started)
 *   ACTIVE       enabled, the last eligible VBlank applied the program
 *   ERROR        disabled by a failed run; only an explicit set_enabled()
 *                leaves it (enable re-arms, disable switches off)
 * None of these run while the guest is frozen (host pause, F7, Rewind), and
 * none run at all when the feature is not in the mod plan (release builds
 * filter the developer feature out).
 *
 * Threading: every function here is for the emulation (main) thread only:
 * the plugin callbacks above, or main-thread code such as SDL event
 * handling. Never call it from a render/ImGui callback. It takes no lock
 * and never touches the overlay's UI mutex. */

#include <stdint.h>

#include "psxcheat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS2_CHEAT_MAX_COINS_CODE   "800B2226 0063"
#define TS2_CHEAT_MAX_COINS_OPTION "max-coins"

typedef enum ts2_cheat_state {
    TS2_CHEAT_UNAVAILABLE = 0,
    TS2_CHEAT_OFF,
    TS2_CHEAT_ARMED,
    TS2_CHEAT_ACTIVE,
    TS2_CHEAT_ERROR
} ts2_cheat_state;

typedef struct ts2_cheat_engine_stats {
    uint32_t vblanks;          /* VBlank callbacks seen */
    uint32_t applied;          /* program runs that completed */
    uint32_t not_started;      /* skipped: game not started */
    uint32_t busy;             /* skipped: adapter context already open */
    uint32_t failures;         /* runs that failed (cheat then disabled) */
    uint32_t savestates;       /* savestate callbacks seen */
    psxcheat_status last_status;
} ts2_cheat_engine_stats;

/* Activation: set up, then enable only if the package option "max-coins"
 * reads "true" (any other value, or no value, leaves it disabled). */
void ts2_cheat_engine_activate(void);
void ts2_cheat_engine_vblank(void);
void ts2_cheat_engine_savestate(void);

/* Set up with an explicit initial state (activation uses the option).
 * Returns 1 when the engine is ready; when setup fails the cheat stays off. */
int ts2_cheat_engine_init(int enabled);
int ts2_cheat_engine_ready(void);
/* Explicit opt-in/opt-out; ignored (returns 0) when not ready. Clears an
 * error (the explicit choice acknowledges it). Disabling does not restore
 * the previous value. */
int ts2_cheat_engine_set_enabled(int enabled);
int ts2_cheat_engine_enabled(void);
ts2_cheat_engine_stats ts2_cheat_engine_get_stats(void);
/* Read-only state; no side effects. */
ts2_cheat_state ts2_cheat_engine_state(void);
/* Static English name of a state ("off", "armed", ...). */
const char *ts2_cheat_state_name(ts2_cheat_state state);

#ifdef __cplusplus
}
#endif

#endif
