/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_MENU_H
#define TS2_CHEAT_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Manifest [[plugin]] id of the Cheat Menu feature. */
#define TS2_CHEAT_MENU_PLUGIN_ID "toystory2.cheat-menu"

struct PsxMenuProvider;

/* 1 once the resolved mod plan activated the Cheat Menu plugin. */
int ts2_cheat_menu_activated(void);

/* The in-game menu provider (cheat_menu_provider.cpp). */
const struct PsxMenuProvider *ts2_cheat_menu_provider(void);

#ifdef __cplusplus
}
#endif

#endif
