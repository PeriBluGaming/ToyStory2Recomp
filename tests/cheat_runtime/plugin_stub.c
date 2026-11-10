/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "plugin_stub.h"

#include <string.h>

#include "cheat_menu.h"     /* TS2_CHEAT_MENU_PLUGIN_ID, ts2_cheat_menu_provider */
#include "mod_plugins.h"    /* real declarations: signatures must match */
#include "psx_menu_host.h"

/* Zero-initialised before the constructor in cheat_menu_plugin.c runs. */
plugin_stub g_plugin;

static int reg(const char *id, stub_callback cb, stub_callback *slot, int *count)
{
    if (!id || !cb || strcmp(id, TS2_CHEAT_MENU_PLUGIN_ID) != 0 || *slot) {
        ++g_plugin.refused_regs;
        return 0;
    }
    *slot = cb;
    ++*count;
    return 1;
}

int psx_mod_register_activation_plugin(const char *id, PSXModActivationCallback cb)
{
    return reg(id, cb, &g_plugin.activation, &g_plugin.activation_regs);
}

int psx_mod_register_vblank_plugin(const char *id, PSXModVBlankCallback cb)
{
    return reg(id, cb, &g_plugin.vblank, &g_plugin.vblank_regs);
}

int psx_mod_register_savestate_plugin(const char *id, PSXModActivationCallback cb)
{
    return reg(id, cb, &g_plugin.savestate, &g_plugin.savestate_regs);
}

int psx_mod_current_option_value(const char *option_id, char *out, uint32_t out_size)
{
    const char *value = NULL;
    ++g_plugin.option_reads;
    if (out && out_size) out[0] = '\0';
    if (!option_id || !out || out_size == 0) return 0;
    if (strcmp(option_id, "max-coins") == 0) value = g_plugin.option_value;
    else if (strcmp(option_id, "infinite-lives") == 0) value = g_plugin.lives_value;
    else if (strcmp(option_id, "remember-changes") == 0) value = g_plugin.remember_value;
    if (!value || strlen(value) + 1u > out_size) return 0;
    strcpy(out, value);
    return 1;
}

int psx_menu_set_provider(const PsxMenuProvider *provider)
{
    ++g_plugin.provider_sets;
    g_plugin.provider = provider;
    return 1;
}

/* The real provider lives in cheat_menu_provider.cpp (ImGui/SDL); the plugin
 * only passes its address on. */
static const PsxMenuProvider k_dummy_provider = { PSX_MENU_ABI_VERSION, "test", 0,
                                                   0, 0, 0, 0, 0, 0 };
const struct PsxMenuProvider *ts2_cheat_menu_provider(void) { return &k_dummy_provider; }
