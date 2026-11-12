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
/* Launcher resource (manifest [[resource]], format "file"): the optional
 * external cheat file (ts2_cheat_file.h), read once per activation. */
#define TS2_CHEAT_FILE_RESOURCE_ID "cheat-file"
/* Launcher option (Phase 3C.4B-2): load the cheat file in Advanced mode
 * (ts2_cheat_external.h). Read once per activation; never persisted by the
 * in-game menu, never applied live. */
#define TS2_CHEAT_ADVANCED_OPTION "advanced-cheats"

struct PsxMenuProvider;
struct ts2_cheat_file_report;

/* 1 when the Cheat Menu plugin was activated AND its feature is in the
 * currently committed mod plan (a later session in the same process may
 * have dropped it; see cheat_menu_plugin.c). Any thread. */
int ts2_cheat_menu_activated(void);

/* 1 when the launcher option "remember-changes" read exactly "true" at
 * activation; 0 otherwise (unset, any other value, not activated). */
int ts2_cheat_menu_remember_changes(void);

/* The external cheat file result of the last activation, or NULL when that
 * activation loaded no file (none selected, or a runtime-test build that
 * ignores it). Written only during activation (main thread); read-only
 * afterwards. Holds statuses and counts only, never file content. */
const struct ts2_cheat_file_report *ts2_cheat_menu_file_report(void);

/* 1 when the last activation selected Advanced mode ("advanced-cheats" read
 * exactly "true"); 0 otherwise, and always 0 in a runtime health test build
 * (which loads no cheat file). Main thread after activation. */
int ts2_cheat_menu_advanced_mode(void);

/* The in-game menu provider (cheat_menu_provider.cpp). */
const struct PsxMenuProvider *ts2_cheat_menu_provider(void);

#ifdef __cplusplus
}
#endif

#endif
