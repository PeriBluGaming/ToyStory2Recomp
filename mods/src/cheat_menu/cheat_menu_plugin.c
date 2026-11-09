/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "cheat_menu.h"

#include "mod_plugins.h"
#include "psx_menu_host.h"

/* Cheat Menu (Phase 0B prototype): registration and activation.
 *
 * The in-game menu provider is registered unconditionally before main(), as
 * psx_menu_host.h asks, but it only attaches when the launcher's resolved mod
 * plan activated this plugin. Activation runs after the plan is committed and
 * before the game window (and so the provider's attach) exists. A disabled
 * feature therefore installs no input sink and no draw hook: every key and
 * button reaches the runtime exactly as without this file.
 *
 * TS2 sessions are offline only, so a plan never changes within a process
 * and the flag needs no reset. */

static volatile int cm_activated;

static void cheat_menu_activate(void)
{
    cm_activated = 1;
}

int ts2_cheat_menu_activated(void)
{
    return cm_activated;
}

PSX_MOD_CONSTRUCTOR(register_cheat_menu)
{
    (void)psx_mod_register_activation_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                             cheat_menu_activate);
    (void)psx_menu_set_provider(ts2_cheat_menu_provider());
}
