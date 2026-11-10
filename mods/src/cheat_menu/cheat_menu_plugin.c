/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "cheat_menu.h"

#include <string.h>

#include "mod_plugins.h"
#include "psx_menu_host.h"
#include "ts2_cheat_engine.h"

/* Cheat Menu: registration and activation.
 *
 * The in-game menu provider is registered unconditionally before main(), as
 * psx_menu_host.h asks, but it only attaches when the launcher's resolved mod
 * plan activated this plugin. Activation runs after the plan is committed and
 * before the game window (and so the provider's attach) exists. A disabled
 * feature therefore installs no input sink and no draw hook: every key and
 * button reaches the runtime exactly as without this file.
 *
 * The cheat engine (ts2_cheat_engine.c) shares the plugin id: one
 * activation, one VBlank and one savestate callback, each registered once
 * here (the runtime keeps one callback of each kind per id and refuses a
 * second). psxrecomp only invokes them while the plan contains this feature,
 * on the emulation thread; VBlank callbacks do not run while the guest is
 * frozen (host pause, F7, Rewind).
 *
 * TS2 sessions are offline only, so a plan never changes within a process
 * and the flags need no reset.
 *
 * "remember-changes" is read here, in the plugin's activation context (the
 * only place psx_mod_current_option_value answers for this feature). Only
 * the exact value "true" turns it on. */

static volatile int cm_activated;
static volatile int cm_remember;

static void cheat_menu_activate(void)
{
    char value[16];
    cm_remember =
        psx_mod_current_option_value(TS2_CHEAT_REMEMBER_OPTION, value, sizeof value) &&
        strcmp(value, "true") == 0;
    cm_activated = 1;
    ts2_cheat_engine_activate();
}

static void cheat_menu_vblank(void)
{
    ts2_cheat_engine_vblank();
}

static void cheat_menu_savestate(void)
{
    ts2_cheat_engine_savestate();
}

int ts2_cheat_menu_activated(void)
{
    return cm_activated;
}

int ts2_cheat_menu_remember_changes(void)
{
    return cm_remember;
}

PSX_MOD_CONSTRUCTOR(register_cheat_menu)
{
    (void)psx_mod_register_activation_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                             cheat_menu_activate);
    (void)psx_mod_register_vblank_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                         cheat_menu_vblank);
    (void)psx_mod_register_savestate_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                            cheat_menu_savestate);
    (void)psx_menu_set_provider(ts2_cheat_menu_provider());
}
