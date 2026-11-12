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
    int option_changed_regs;
    int (*option_changed)(const char *option_id, const char *value);
    /* psx_mod_option_live_capable for this feature: "in the committed plan"
     * once an option-changed callback is registered (runtime semantics).
     * not_in_plan = 1 simulates a later session without the feature. */
    int not_in_plan;
    int live_capable_queries;
    int provider_sets;
    const void *provider;
    /* Package option "max-coins": NULL means "no value". */
    const char *option_value;
    /* Package option "infinite-lives": NULL means "no value". */
    const char *lives_value;
    /* Package option "remember-changes": NULL means "no value". */
    const char *remember_value;
    int option_reads;
    /* Resource "cheat-file": the committed path, NULL means "none selected"
     * (psx_mod_current_resource_path; too long for the buffer also reads 0,
     * as in the runtime). */
    const char *resource_path;
    int resource_reads;
} plugin_stub;

extern plugin_stub g_plugin;

#endif
