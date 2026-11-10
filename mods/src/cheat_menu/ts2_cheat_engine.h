/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_ENGINE_H
#define TS2_CHEAT_ENGINE_H

/* Cheat Menu: the developer-only runtime engine. Since Phase 2E-B it runs
 * every entry of the static cheat table (ts2_cheat_table.h) in its own,
 * independent slot: own program, own memory adapter configured with only
 * that cheat's ranges, own state, error and statistics. A cheat that fails
 * validation is UNAVAILABLE on its own; a failed run disables only its own
 * slot. The table holds two verified test cheats: "Max Coins"
 * (800B2226 0063) at index 0 and "Infinite Lives" (800B2222 0009) at
 * index 1. The single-cheat functions below are the
 * index-0 wrappers of the *_at() functions and keep their behaviour.
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
#include "ts2_cheat_table.h"

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

/* Activation: set up every table entry, then enable each one only if the
 * package option named by its id (Max Coins: "max-coins") reads exactly
 * "true" (any other value, or no value, leaves it disabled). */
void ts2_cheat_engine_activate(void);
void ts2_cheat_engine_vblank(void);
void ts2_cheat_engine_savestate(void);

/* Index 0 (Max Coins): set up the production table with an explicit initial
 * state for index 0 (other entries off). Returns 1 when index 0 is ready;
 * when its setup fails it stays off. */
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

/* ---- Multi-cheat API (Phase 2E-B). Same thread rules as above. -------- */

/* Set up from an explicit table and policy list (activation and the legacy
 * init use the production ones). enabled[i] (NULL: all off) is the initial
 * state of each slot; a slot that fails validation stays UNAVAILABLE and
 * ignores it. Returns the table status; on a table error no slot is set up.
 * The table must outlive the engine (it is not copied). */
ts2_cheat_table_status ts2_cheat_engine_init_table(const ts2_cheat_def *defs, size_t n,
                                                   const psxcheat_psx_range *policy,
                                                   size_t policy_n,
                                                   const int *enabled);
ts2_cheat_table_status ts2_cheat_engine_table_status(void);
/* Number of slots (table entries; 0 before setup or after a table error). */
size_t ts2_cheat_engine_count(void);
/* The slot's definition; NULL when out of range. */
const ts2_cheat_def *ts2_cheat_engine_def(size_t index);
/* Why a slot is UNAVAILABLE (TS2_CHEAT_DEF_OK when it is set up). */
ts2_cheat_def_status ts2_cheat_engine_setup_status(size_t index);
/* Out-of-range index: not ready, disabled, UNAVAILABLE, zero stats, and
 * set_enabled_at returns 0. */
int ts2_cheat_engine_ready_at(size_t index);
int ts2_cheat_engine_set_enabled_at(size_t index, int enabled);
int ts2_cheat_engine_enabled_at(size_t index);
ts2_cheat_state ts2_cheat_engine_state_at(size_t index);
ts2_cheat_engine_stats ts2_cheat_engine_stats_at(size_t index);

#ifdef __cplusplus
}
#endif

#endif
