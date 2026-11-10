/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef PLUGIN_STUB_H
#define PLUGIN_STUB_H

/* Test-only stand-ins for the psxrecomp plugin registry, package options and
 * the in-game menu slot, as used by cheat_menu_plugin.c. Like the runtime
 * (mod_packages.cpp), the registry keeps one callback of each kind per id
 * and refuses a second one. */

typedef void (*stub_callback)(void);

typedef struct plugin_stub {
    /* Registrations, counted per kind for the Cheat Menu id. */
    int activation_regs, vblank_regs, savestate_regs;
    int refused_regs;          /* duplicate or foreign-id registrations */
    stub_callback activation, vblank, savestate;
    int provider_sets;
    const void *provider;
    /* Package option "max-coins": NULL means "no value". */
    const char *option_value;
    int option_reads;
} plugin_stub;

extern plugin_stub g_plugin;

#endif
