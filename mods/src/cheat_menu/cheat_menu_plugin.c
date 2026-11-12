/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "cheat_menu.h"

#include <stdio.h>
#include <string.h>

#include "mod_plugins.h"
#include "psx_menu_host.h"
#include "ts2_cheat_engine.h"
#include "ts2_cheat_external.h"
#include "ts2_cheat_file.h"
#if defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
#include "ts2_cheat_memory.h"   /* TS2_CHEAT_HEALTH_RECOMMENDED */
#endif

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
 * Sessions (Phase 3C.2A). psxrecomp can end a session and start another in
 * the same process (PSX_MENU_EXIT_TO_FRONTEND), with a new plan that may no
 * longer contain this feature -- then activation does not run at all, so a
 * flag set by an earlier activation cannot prove that THIS session has the
 * feature. ts2_cheat_menu_activated() therefore also asks the runtime whether
 * this feature's plugin is in the currently committed plan
 * (psx_mod_option_live_capable, read-only, any thread). That query needs an
 * option-changed callback registered under the plugin id; the one below does
 * nothing and reports "not applied live", so psx_mod_set_option_live keeps
 * the previous value and nothing changes for any option.
 *
 * Activation order (main thread, after the previous session was torn down:
 * provider detached, no VBlank): engine shutdown (drops its table pointer),
 * ts2_cheat_session_begin (fresh table window; refused while the menu still
 * holds a table reference), at most ONE build, engine activation (seals the
 * table for the session). See ts2_cheat_external.h.
 *
 * External cheat file (Phase 3C.3A). The launcher resource "cheat-file" is
 * resolved here (psx_mod_current_resource_path answers only in this
 * context). With no file selected nothing is built and nothing is printed:
 * the built-in table is used exactly as before. With a file selected,
 * ts2_cheat_file_load() is the session's single build: it refuses (and
 * opens nothing) unless the window is fresh, unsealed and unreferenced, and
 * otherwise builds once -- built-ins plus the accepted entries, or built-ins
 * only on any file-level error. A one-line summary and one line per
 * rejected entry go to stdout, from fixed texts and numbers only (never
 * file content or the path). External cheats start in the file's `enabled`
 * state and stay session-only: the menu never persists them and nothing
 * writes to the file. The file is read once per activation; there is no
 * reload while the game runs.
 *
 * Runtime health test build (-DTS2_CHEAT_RUNTIME_TEST_HEALTH=ON, developer
 * only, default OFF): the synthetic "TEST Full Health" entry is the
 * session's build and the external cheat file is NOT loaded (a selected
 * file is reported as ignored), so that test always runs on exactly one
 * known external entry.
 *
 * "remember-changes" is read here, in the plugin's activation context (the
 * only place psx_mod_current_option_value answers for this feature). Only
 * the exact value "true" turns it on. */

static volatile int cm_activated;
static volatile int cm_remember;
/* Last activation's cheat file result; cm_file_loaded = 0: none. */
static ts2_cheat_file_report cm_file;
static int cm_file_loaded;
/* Selected path (UTF-8). A path longer than this reads as "none selected"
 * (psx_mod_current_resource_path cannot tell the two apart). */
static char cm_file_path[4096];

#if defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
/* Phase 3B.3 controlled runtime test entry (ts2_cheat_external.h). Only in
 * builds configured with -DTS2_CHEAT_RUNTIME_TEST_HEALTH=ON. */
ts2_cheat_external_input ts2_cheat_runtime_test_health_input(void)
{
    static const char *const lines[] = { TS2_CHEAT_HEALTH_RECOMMENDED };
    ts2_cheat_external_input in;
    memset(&in, 0, sizeof in);
    in.id = "test-health";
    in.label = "TEST Full Health";
    in.description = "runtime test: holds health at 14";
    in.code_lines = lines;
    in.code_line_count = 1;
    in.enabled = 0;   /* switched on from the menu only */
    return in;
}
#endif

/* 1 when the launcher committed a cheat file path for this activation. */
static int cheat_file_selected(void)
{
    return psx_mod_current_resource_path(TS2_CHEAT_FILE_RESOURCE_ID, cm_file_path,
                                         (uint32_t)sizeof cm_file_path) &&
           cm_file_path[0] != '\0';
}

#if !defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
/* Load the selected cheat file: the session's single build (the loader
 * itself refuses outside a fresh, unsealed, unreferenced window). Sanitized
 * diagnostics only. */
static void load_cheat_file(void)
{
    char line[160];
    size_t i, examined;
    (void)ts2_cheat_file_load(cm_file_path, &cm_file);
    cm_file_loaded = 1;
    (void)ts2_cheat_file_describe(&cm_file, line, sizeof line);
    printf("cheat-menu: %s%s\n", line,
           cm_file.file == TS2_CHEAT_FILE_OK || cm_file.file == TS2_CHEAT_FILE_ERR_SEALED
               ? "" : " - built-in cheats only");
    examined = cm_file.entries < TS2_CHEAT_EXTERNAL_INPUT_MAX ? cm_file.entries
                                                              : TS2_CHEAT_EXTERNAL_INPUT_MAX;
    for (i = 0; i < examined; ++i) {
        if (cm_file.shape[i] == TS2_CHEAT_ENTRY_OK && cm_file.verdict[i] == TS2_CHEAT_EXT_OK)
            continue;
        (void)ts2_cheat_file_describe_entry(&cm_file, i, line, sizeof line);
        printf("cheat-menu:   %s\n", line);
    }
    if (cm_file.entries > examined)
        printf("cheat-menu:   %u further entries not examined (limit %u)\n",
               (unsigned)(cm_file.entries - examined), (unsigned)TS2_CHEAT_EXTERNAL_INPUT_MAX);
    fflush(stdout);
}
#endif

static void cheat_menu_activate(void)
{
    char value[16];
    int fresh;
    cm_remember =
        psx_mod_current_option_value(TS2_CHEAT_REMEMBER_OPTION, value, sizeof value) &&
        strcmp(value, "true") == 0;
    cm_activated = 1;
    cm_file_loaded = 0;
    /* New session: the engine lets go of the previous table first; a table
     * still referenced by an attached menu is kept (no rebuild below). */
    ts2_cheat_engine_shutdown();
    fresh = ts2_cheat_session_begin();
#if !defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
    (void)fresh;    /* the loader checks the window itself */
    if (cheat_file_selected()) load_cheat_file();
#else
    if (cheat_file_selected())
        printf("cheat-menu: RUNTIME TEST build: external cheat file ignored\n");
    /* Phase 3B.3 controlled runtime test (opt-in CMake option, default
     * OFF): one synthetic external entry through the normal external
     * validation, before the engine is set up. No file, no persistence. */
    {
        const ts2_cheat_external_input in = ts2_cheat_runtime_test_health_input();
        ts2_cheat_ext_status st = TS2_CHEAT_EXT_ERR_NOT_CHECKED;
        const ts2_cheat_build_status b = fresh ? ts2_cheat_combined_build(&in, 1, &st)
                                               : TS2_CHEAT_BUILD_ERR_SEALED;
        printf("cheat-menu: RUNTIME TEST entry '%s': %s (build %d)\n", in.id,
               ts2_cheat_ext_status_name(st), (int)b);
        fflush(stdout);
    }
#endif
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

/* Plan-membership probe only (see the header comment): never applies a
 * value live. */
static int cheat_menu_option_changed(const char *option_id, const char *value)
{
    (void)option_id;
    (void)value;
    return 0;
}

int ts2_cheat_menu_activated(void)
{
    return cm_activated &&
           psx_mod_option_live_capable(TS2_CHEAT_MENU_PACKAGE_ID, TS2_CHEAT_MENU_FEATURE_ID) == 1;
}

int ts2_cheat_menu_remember_changes(void)
{
    return cm_remember;
}

const struct ts2_cheat_file_report *ts2_cheat_menu_file_report(void)
{
    return cm_file_loaded ? &cm_file : NULL;
}

PSX_MOD_CONSTRUCTOR(register_cheat_menu)
{
    (void)psx_mod_register_activation_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                             cheat_menu_activate);
    (void)psx_mod_register_vblank_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                         cheat_menu_vblank);
    (void)psx_mod_register_savestate_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                            cheat_menu_savestate);
    (void)psx_mod_register_option_changed_plugin(TS2_CHEAT_MENU_PLUGIN_ID,
                                                 cheat_menu_option_changed);
    (void)psx_menu_set_provider(ts2_cheat_menu_provider());
}
