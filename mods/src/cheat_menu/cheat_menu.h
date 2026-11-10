/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_MENU_H
#define TS2_CHEAT_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Manifest [[plugin]] id of the Cheat Menu feature. */
#define TS2_CHEAT_MENU_PLUGIN_ID "toystory2.cheat-menu"
/* Manifest package and feature ids (mods/preloaded/packages/...). The
 * in-game menu persists through PsxMenuHost.mod_set with exactly these. */
#define TS2_CHEAT_MENU_PACKAGE_ID "toystory2.enhancement.cheat-menu"
#define TS2_CHEAT_MENU_FEATURE_ID "cheat-menu"
/* Launcher option: remember in-game toggles for the next start. */
#define TS2_CHEAT_REMEMBER_OPTION "remember-changes"

struct PsxMenuProvider;

/* 1 once the resolved mod plan activated the Cheat Menu plugin. */
int ts2_cheat_menu_activated(void);

/* 1 when the launcher option "remember-changes" read exactly "true" at
 * activation; 0 otherwise (unset, any other value, not activated). */
int ts2_cheat_menu_remember_changes(void);

/* The in-game menu provider (cheat_menu_provider.cpp). */
const struct PsxMenuProvider *ts2_cheat_menu_provider(void);

#ifdef __cplusplus
}
#endif

#endif
