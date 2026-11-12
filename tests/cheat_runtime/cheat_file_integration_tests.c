/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3C.3A: the optional external cheat file through the real plugin
 * activation. The launcher resource "cheat-file" is the plugin stub's
 * resource_path; everything else is real: loader, validator, production
 * policies, table lifecycle, engine, bridge, view and recomp-ui model. The
 * menu provider (C++/ImGui) is represented by what it does with the table:
 * a table reference from attach to detach and a view on the active table.
 * Diagnostics are checked by capturing stdout. Test files are created at
 * run time inside the build tree (TS2_CHEAT_FILE_TEST_DIR). POSIX only.
 *
 * Built twice: with the production plugin, and with the plugin compiled for
 * the developer runtime health test (TS2_CHEAT_RUNTIME_TEST_HEALTH), where
 * a selected file must be ignored and only the synthetic entry appears. */
#define _POSIX_C_SOURCE 200809L
#include "cheat_menu.h"
#include "cheat_menu_view.h"
#include "plugin_stub.h"
#include "psx_menu_host.h"
#include "psx_runtime_stub.h"
#include "recomp_runtime_ui.h"
#include "ts2_cheat_engine.h"
#include "ts2_cheat_external.h"
#include "ts2_cheat_file.h"
#include "ts2_cheat_memory.h"
#include "ts2_cheat_table.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_checks, g_failures;
static const char *g_test = "";

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define OFF(addr) ((addr) - 0x80000000u)
#define S(p) ((p) ? (p) : "")
#define HEADER "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00893\"\n"
#define HEALTH_ON "[[cheat]]\nid = \"health-14\"\nlabel = \"Full Health\"\n" \
                  "code = [\"800B221E 000E\"]\nenabled = true\n"
#define HEALTH_OFF "[[cheat]]\nid = \"health-14\"\nlabel = \"Full Health\"\n" \
                   "code = [\"800B221E 000E\"]\n"
/* A file label that must never appear in the output. */
#define SECRET "SECRET-LABEL-7f3a"

static char g_dir[512];

static const char *path_of(const char *name, char *buf, size_t size)
{
    snprintf(buf, size, "%s/%s", g_dir, name);
    return buf;
}

/* Writes `text` to <dir>/<name>; returns the path (static per name slot). */
static const char *write_text(const char *name, const char *text)
{
    static char paths[8][640];
    static int k;
    char *p = paths[k = (k + 1) % 8];
    FILE *f;
    path_of(name, p, sizeof paths[0]);
    unlink(p);
    f = fopen(p, "wb");
    if (!f) { printf("cannot create %s\n", p); exit(2); }
    if (fwrite(text, 1, strlen(text), f) != strlen(text)) { printf("cannot write %s\n", p); exit(2); }
    fclose(f);
    return p;
}

/* ---- stdout capture ------------------------------------------------------ */

static char g_out[16384];

static void capture_begin(int *saved)
{
    char p[640];
    int fd;
    fflush(stdout);
    *saved = dup(1);
    fd = open(path_of("stdout.txt", p, sizeof p), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (*saved < 0 || fd < 0) { printf("cannot capture stdout\n"); exit(2); }
    dup2(fd, 1);
    close(fd);
}

static const char *capture_end(int saved)
{
    char p[640];
    FILE *f;
    size_t n = 0;
    fflush(stdout);
    dup2(saved, 1);
    close(saved);
    f = fopen(path_of("stdout.txt", p, sizeof p), "rb");
    if (f) {
        n = fread(g_out, 1, sizeof g_out - 1, f);
        fclose(f);
    }
    g_out[n] = '\0';
    return g_out;
}

/* ---- the runtime activating the plugin ----------------------------------- */

/* Launcher option "advanced-cheats" for the next activations (NULL: none). */
static const char *g_adv;

/* One activation for a session whose plan contains the feature, with the
 * launcher's options and resource; returns what it printed. */
static const char *activate(const char *path, const char *coins, const char *lives,
                            const char *remember)
{
    int saved;
    g_plugin.not_in_plan = 0;
    g_plugin.option_value = coins;
    g_plugin.lives_value = lives;
    g_plugin.remember_value = remember;
    g_plugin.advanced_value = g_adv;
    g_plugin.resource_path = path;
    capture_begin(&saved);
    g_plugin.activation();
    return capture_end(saved);
}

static int starts(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

static size_t active_count(void)
{
    size_t n = 0;
    (void)ts2_cheat_active_table(&n);
    return n;
}

static const char *id_at(size_t k)
{
    size_t n = 0;
    const ts2_cheat_def *t = ts2_cheat_active_table(&n);
    return t && k < n ? S(t[k].id) : "";
}

static int has_id(const char *id)
{
    size_t n = 0, k;
    const ts2_cheat_def *t = ts2_cheat_active_table(&n);
    for (k = 0; t && k < n; ++k)
        if (t[k].id && strcmp(t[k].id, id) == 0) return 1;
    return 0;
}

/* Built-ins first, unchanged. */
static int builtins_intact(void)
{
    size_t n = 0, nb = 0;
    const ts2_cheat_def *t = ts2_cheat_active_table(&n);
    const ts2_cheat_def *b = ts2_cheat_table(&nb);
    return t && b && nb == 2u && n >= 2u && strcmp(S(t[0].id), "max-coins") == 0 &&
           strcmp(S(t[1].id), "infinite-lives") == 0 && t[0].code == b[0].code &&
           t[1].code == b[1].code && t[0].ranges == b[0].ranges && t[1].ranges == b[1].ranges;
}

/* ------------------------------------------------------------------------- */
#if !defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)

/* ---- menu provider stand-in (fake host for persistence) ------------------ */

typedef struct call { char o[40], v[8]; } call;
static call g_calls[16];
static int g_ncalls;
static PsxMenuHost g_host;
static cheat_menu_view g_view;
static RecompRuntimeUi *g_ui;
static int g_attached;

static int fake_mod_set(const char *p, const char *f, const char *o, const char *v)
{
    (void)p; (void)f;
    if (g_ncalls < 16) {
        snprintf(g_calls[g_ncalls].o, sizeof g_calls[0].o, "%s", o ? o : "(null)");
        snprintf(g_calls[g_ncalls].v, sizeof g_calls[0].v, "%s", v ? v : "(null)");
    }
    ++g_ncalls;
    return 0;
}

static void cb_visibility(void *ctx, int open)
{
    (void)ctx;
    if (open) recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
}

/* Provider attach: table reference, view and menu model on the active table
 * (as cheat_menu_provider.cpp does). */
static int attach(void)
{
    RecompRuntimeUiConfig cfg;
    if (!ts2_cheat_menu_activated()) return 0;
    ts2_cheat_table_ref_acquire();
    g_attached = 1;
    memset(&g_host, 0, sizeof g_host);
    g_host.abi_version = PSX_MENU_ABI_VERSION;
    g_host.mod_set = fake_mod_set;
    memset(g_calls, 0, sizeof g_calls);
    g_ncalls = 0;
    cheat_menu_view_init(&g_view);
    cheat_menu_view_set_persist(&g_view, ts2_cheat_menu_remember_changes(),
                                cheat_menu_persist_via_host, &g_host);
    (void)cheat_menu_view_sync(&g_view);
    memset(&cfg, 0, sizeof cfg);
    cfg.title = "Cheat Menu";
    cfg.items = cheat_menu_view_items(&g_view, &cfg.item_count);
    cheat_menu_view_callbacks(&g_view, &cfg.callbacks);
    cfg.callbacks.visibility_changed = cb_visibility;
    g_ui = recomp_runtime_ui_create(&cfg);
    return g_ui != NULL;
}

static void detach(void)
{
    if (g_ui) { recomp_runtime_ui_destroy(g_ui); g_ui = NULL; }
    if (g_attached) ts2_cheat_table_ref_release();
    g_attached = 0;
}

static void in(RecompRuntimeUiInput i) { recomp_runtime_ui_handle_input(g_ui, i, 1, 0); }

static const ts2_cheat_file_report *report(void) { return ts2_cheat_menu_file_report(); }

/* Each test starts like a fresh process: no provider, clean table. */
static void begin(const char *name)
{
    g_test = name;
    g_adv = NULL;
    detach();
    ts2_cheat_engine_shutdown();
    ts2_cheat_combined_reset();
    stub_reset();
}

/* ---- tests --------------------------------------------------------------- */

static void test_no_resource(void)
{
    const char *out;
    begin("no resource");
    g_plugin.resource_reads = 0;
    out = activate(NULL, NULL, NULL, NULL);
    CHECK(g_plugin.resource_reads == 1);
    CHECK(out[0] == '\0');                      /* nothing printed */
    CHECK(report() == NULL);
    CHECK(builtins_intact() && active_count() == 2u);
    CHECK(ts2_cheat_active_external_count() == 0u);
    CHECK(ts2_cheat_session_sealed() == 1);     /* engine activation sealed */
    /* An empty path is "none" as well. */
    out = activate("", NULL, NULL, NULL);
    CHECK(out[0] == '\0' && report() == NULL && active_count() == 2u);
    /* A path longer than the plugin's buffer reads as "none selected". */
    {
        static char longp[5000];
        memset(longp, 'a', sizeof longp - 1);
        longp[0] = '/';
        out = activate(longp, NULL, NULL, NULL);
        CHECK(out[0] == '\0' && report() == NULL && active_count() == 2u);
    }
    CHECK(strstr(out, "RUNTIME TEST") == NULL && !has_id("test-health"));
}

static void test_valid_file(void)
{
    const char *p = write_text("valid.toml", HEADER HEALTH_ON);
    const char *out;
    const ts2_cheat_file_report *r;
    begin("valid file");
    out = activate(p, "true", NULL, NULL);
    r = report();
    CHECK(r != NULL);
    if (!r) return;
    CHECK(r->file == TS2_CHEAT_FILE_OK && r->build == TS2_CHEAT_BUILD_OK);
    CHECK(r->entries == 1u && r->accepted == 1u);
    CHECK(strcmp(out, "cheat-menu: cheat file: ok, 1 entries, 1 accepted, 0 rejected\n") == 0);
    CHECK(strstr(out, p) == NULL);              /* path never printed */
    CHECK(builtins_intact() && active_count() == 3u);
    CHECK(strcmp(id_at(2), "health-14") == 0);
    CHECK(ts2_cheat_active_external_count() == 1u && ts2_cheat_active_external_enabled(2) == 1);
    CHECK(ts2_cheat_session_sealed() == 1);
    CHECK(!has_id("test-health"));
    /* Start states: max-coins from its launcher option, health from the
     * file; both apply on the next VBlank. Lives stays off. */
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ACTIVE);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_OFF);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ACTIVE);
    CHECK(stub_u16(OFF(0x800B2226u)) == 99 && stub_u16(OFF(0x800B221Eu)) == 14);
    CHECK(stub_u16(OFF(0x800B2222u)) == 0 && g_stub.bad_addresses == 0);
    /* Sealed after activation: no further build, not even by the loader. */
    {
        size_t n = 0;
        const ts2_cheat_def *t = ts2_cheat_active_table(&n);
        ts2_cheat_file_report again;
        CHECK(ts2_cheat_file_load(p, &again) == TS2_CHEAT_FILE_ERR_SEALED);
        CHECK(ts2_cheat_combined_build(NULL, 0, NULL) == TS2_CHEAT_BUILD_ERR_SEALED);
        CHECK(ts2_cheat_active_table(&n) == t && n == 3u);
    }
}

static void test_malformed_file(void)
{
    const char *p = write_text("broken.toml", HEADER "[[cheat]]\nid = \"x\nlabel = \"" SECRET "\"\n");
    const char *out;
    begin("malformed file");
    out = activate(p, NULL, NULL, NULL);
    CHECK(report() != NULL && report()->file == TS2_CHEAT_FILE_ERR_SYNTAX);
    CHECK(report() && report()->build == TS2_CHEAT_BUILD_OK);
    CHECK(starts(out, "cheat-menu: cheat file: TOML syntax error (line "));
    CHECK(strstr(out, " - built-in cheats only\n") != NULL);
    CHECK(strstr(out, SECRET) == NULL);
    CHECK(builtins_intact() && active_count() == 2u && ts2_cheat_active_external_count() == 0u);
    /* Document-level errors: same fallback. */
    out = activate(write_text("wrong-game.toml",
                              "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00000\"\n"
                              HEALTH_ON), NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_ERR_GAME);
    CHECK(strcmp(out, "cheat-menu: cheat file: wrong or missing game - built-in cheats only\n") == 0);
    CHECK(active_count() == 2u);
    /* Two BOMs: encoding error, built-ins only. */
    out = activate(write_text("bom2.toml", "\xEF\xBB\xBF\xEF\xBB\xBF" HEADER HEALTH_ON), NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_ERR_ENCODING && active_count() == 2u);
    /* Dotted-key nesting attack: refused before toml11, built-ins only. */
    out = activate(write_text("dots.toml", HEADER "a.b.c.d.e.f.g = 1\n"), NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_ERR_NESTING && active_count() == 2u);
    CHECK(strstr(out, "nesting too deep - built-in cheats only") != NULL);
}

static void test_invalid_beside_valid(void)
{
    const char *p = write_text("mixed.toml",
        HEADER
        "[[cheat]]\nid = \"coins-ext\"\nlabel = \"" SECRET "\"\ncode = [\"800B2226 0063\"]\n"
        HEALTH_OFF
        "[[cheat]]\nid = \"odd\"\nlabel = \"Odd\"\ncode = [\"800B221E 000E\"]\ncolor = 1\n");
    const char *out;
    const ts2_cheat_file_report *r;
    begin("invalid beside valid");
    out = activate(p, NULL, NULL, NULL);
    r = report();
    CHECK(r != NULL);
    if (!r) return;
    CHECK(r->file == TS2_CHEAT_FILE_OK && r->entries == 3u && r->accepted == 1u);
    CHECK(r->verdict[0] != TS2_CHEAT_EXT_OK);               /* outside the external policy */
    CHECK(r->shape[1] == TS2_CHEAT_ENTRY_OK && r->verdict[1] == TS2_CHEAT_EXT_OK);
    CHECK(r->shape[2] == TS2_CHEAT_ENTRY_UNKNOWN_KEY);
    CHECK(starts(out, "cheat-menu: cheat file: ok, 3 entries, 1 accepted, 2 rejected\n"
                     "cheat-menu:   cheat entry 1: "));
    CHECK(strstr(out, "cheat-menu:   cheat entry 3: unknown key\n") != NULL);
    CHECK(strstr(out, "cheat entry 2") == NULL);            /* accepted: no line */
    CHECK(strstr(out, SECRET) == NULL && strstr(out, "coins-ext") == NULL);
    CHECK(builtins_intact() && active_count() == 3u && strcmp(id_at(2), "health-14") == 0);
    CHECK(ts2_cheat_active_external_enabled(2) == 0);       /* `enabled` absent: off */
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B221Eu)) == 0 && stub_u16(OFF(0x800B2226u)) == 0);
    /* More entries than the input limit: one summary line for the rest. */
    {
        static char big[65536];
        size_t i, len;
        len = (size_t)snprintf(big, sizeof big, "%s", HEADER);
        for (i = 0; i < TS2_CHEAT_EXTERNAL_INPUT_MAX + 3u; ++i)
            len += (size_t)snprintf(big + len, sizeof big - len,
                                    "[[cheat]]\nid = \"c%u\"\nlabel = \"C\"\ncode = [\"800B221E 000E\"]\n",
                                    (unsigned)i);
        out = activate(write_text("many.toml", big), NULL, NULL, NULL);
        CHECK(report() && report()->file == TS2_CHEAT_FILE_OK && report()->entries == 67u);
        CHECK(strstr(out, "cheat-menu:   3 further entries not examined (limit 64)\n") != NULL);
        CHECK(builtins_intact());
    }
}

static void test_unreadable_file(void)
{
    char p[640];
    const char *out;
    begin("unreadable file");
    out = activate(path_of("does-not-exist.toml", p, sizeof p), NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_ERR_OPEN);
    CHECK(strcmp(out, "cheat-menu: cheat file: cannot open - built-in cheats only\n") == 0);
    CHECK(builtins_intact() && active_count() == 2u);
    path_of("a-directory.toml", p, sizeof p);
    mkdir(p, 0755);
    out = activate(p, NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_ERR_NOT_REGULAR);
    CHECK(strcmp(out, "cheat-menu: cheat file: not a regular file - built-in cheats only\n") == 0);
    CHECK(active_count() == 2u);
    if (geteuid() != 0) {                       /* not meaningful as root */
        const char *lp = write_text("locked.toml", HEADER HEALTH_ON);
        chmod(lp, 0000);
        out = activate(lp, NULL, NULL, NULL);
        chmod(lp, 0644);
        CHECK(report() && report()->file == TS2_CHEAT_FILE_ERR_OPEN);
        CHECK(builtins_intact() && active_count() == 2u);
    }
}

static void test_repeated_activation(void)
{
    const char *p = write_text("repeat.toml", HEADER HEALTH_ON);
    begin("repeated activation");
    (void)activate(p, NULL, NULL, NULL);
    CHECK(active_count() == 3u);
    CHECK(attach());
    detach();                                   /* back to the frontend */
    (void)activate(p, NULL, NULL, NULL);        /* same file: no duplicates */
    CHECK(active_count() == 3u && strcmp(id_at(2), "health-14") == 0);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_OK);
    /* Next session without a file: back to built-ins only, no output. */
    CHECK(strcmp(activate(NULL, NULL, NULL, NULL), "") == 0);
    CHECK(active_count() == 2u && report() == NULL);
    /* The file changed on disk: only the next activation sees it. */
    (void)activate(p, NULL, NULL, NULL);
    write_text("repeat.toml", HEADER "[[cheat]]\nid = \"other\"\nlabel = \"Other\"\n"
                              "code = [\"800B221E 0005\"]\n");
    ts2_cheat_engine_vblank();
    CHECK(has_id("health-14") && !has_id("other"));          /* no hot reload */
    (void)activate(p, NULL, NULL, NULL);
    CHECK(has_id("other") && !has_id("health-14") && active_count() == 3u);
}

static void test_referenced_table(void)
{
    const char *a = write_text("ref-a.toml", HEADER HEALTH_ON);
    const char *b = write_text("ref-b.toml", HEADER "[[cheat]]\nid = \"other\"\nlabel = \"Other\"\n"
                                                    "code = [\"800B221E 0005\"]\n");
    const ts2_cheat_def *t;
    size_t n = 0;
    const char *out;
    begin("referenced table");
    (void)activate(a, NULL, NULL, NULL);
    CHECK(attach());                            /* provider holds a reference */
    t = ts2_cheat_active_table(&n);
    CHECK(n == 3u);
    /* Reactivation while referenced: refused, table and view data intact. */
    out = activate(b, NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(report() && report()->build == TS2_CHEAT_BUILD_ERR_SEALED);
    CHECK(strcmp(out, "cheat-menu: cheat file: cheat table in use (not loaded)\n") == 0);
    {
        size_t n2 = 0;
        CHECK(ts2_cheat_active_table(&n2) == t && n2 == 3u);
    }
    CHECK(has_id("health-14") && !has_id("other"));
    CHECK(strcmp(S(g_view.items[3].key), "cheat_menu.ext.health-14") == 0);
    CHECK(strcmp(S(g_view.items[3].label), "Full Health") == 0);
    /* No file selected while referenced: nothing printed, nothing built. */
    CHECK(strcmp(activate(NULL, NULL, NULL, NULL), "") == 0);
    CHECK(ts2_cheat_active_table(&n) == t && n == 3u);
    /* The engine still runs the referenced table. */
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B221Eu)) == 14);
    /* After detach the next activation loads normally. */
    detach();
    (void)activate(b, NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_OK && has_id("other") && !has_id("health-14"));
}

static void test_session_only(void)
{
    const char *p = write_text("session.toml", HEADER HEALTH_OFF);
    char before[256], after[256];
    struct stat s1, s2;
    FILE *f;
    size_t n1, n2;
    begin("session only");
    f = fopen(p, "rb");
    n1 = f ? fread(before, 1, sizeof before, f) : 0;
    if (f) fclose(f);
    CHECK(stat(p, &s1) == 0);
    (void)activate(p, "false", "false", "true");    /* remember-changes on */
    CHECK(ts2_cheat_menu_remember_changes() == 1);
    CHECK(attach());
    {
        size_t items = 0;
        (void)cheat_menu_view_items(&g_view, &items);
        CHECK(items == 4u);                         /* Resume + 2 built-ins + file entry */
    }
    /* Toggle the file entry (last row): session only, nothing persisted. */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_UP);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ARMED);
    CHECK(g_ncalls == 0 && g_view.last_persist == CHEAT_MENU_PERSIST_SESSION);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B221Eu)) == 14);
    /* A built-in toggle is still remembered through the launcher option. */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);               /* Max Coins */
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(g_ncalls == 1 && strcmp(g_calls[0].o, "max-coins") == 0 && strcmp(g_calls[0].v, "true") == 0);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_QUEUED);
    /* The file is never written. */
    f = fopen(p, "rb");
    n2 = f ? fread(after, 1, sizeof after, f) : 0;
    if (f) fclose(f);
    CHECK(stat(p, &s2) == 0);
    CHECK(n1 == n2 && memcmp(before, after, n1) == 0);
    CHECK(s1.st_size == s2.st_size && s1.st_mtime == s2.st_mtime);
    /* Next session: the file's start state again (off), not the toggle. */
    detach();
    (void)activate(p, "false", "false", "true");
    CHECK(ts2_cheat_engine_enabled_at(2) == 0 && ts2_cheat_active_external_enabled(2) == 0);
    /* remember-changes off: built-in toggles are session-only too. */
    (void)activate(p, "false", "false", NULL);
    CHECK(ts2_cheat_menu_remember_changes() == 0);
}

static void test_builtins_unchanged(void)
{
    const char *p = write_text("builtins.toml", HEADER HEALTH_OFF);
    begin("built-ins");
    /* Same built-in behaviour with and without a file. */
    (void)activate(NULL, "true", "true", NULL);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99 && stub_u16(OFF(0x800B2222u)) == 9);
    stub_reset();
    (void)activate(p, "true", "true", NULL);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99 && stub_u16(OFF(0x800B2222u)) == 9);
    CHECK(stub_u16(OFF(0x800B221Eu)) == 0);         /* file entry off */
    CHECK(builtins_intact());
    /* A file entry reusing a built-in id or overlapping its memory is
     * rejected; the built-in is untouched. */
    stub_reset();
    (void)activate(write_text("clash.toml",
                              HEADER "[[cheat]]\nid = \"max-coins\"\nlabel = \"Fake\"\n"
                              "code = [\"800B221E 000E\"]\nenabled = true\n"),
                   "true", NULL, NULL);
    CHECK(report() && report()->accepted == 0u && report()->verdict[0] == TS2_CHEAT_EXT_ERR_ID_BUILTIN);
    CHECK(builtins_intact() && active_count() == 2u);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99 && stub_u16(OFF(0x800B221Eu)) == 0);
}

static void test_provider_lifecycle(void)
{
    const char *p = write_text("provider.toml", HEADER HEALTH_ON);
    begin("provider lifecycle");
    (void)activate(p, NULL, NULL, NULL);
    CHECK(attach());
    CHECK(ts2_cheat_table_refs() == 1u);
    CHECK(strcmp(S(g_view.items[3].key), "cheat_menu.ext.health-14") == 0);
    detach();
    CHECK(ts2_cheat_table_refs() == 0u);
    /* Feature dropped from the next plan: activation does not run and the
     * provider does not attach; the old table stays as it was. */
    g_plugin.not_in_plan = 1;
    CHECK(!attach());
    CHECK(has_id("health-14"));
    g_plugin.not_in_plan = 0;
    /* Re-enabled: activation runs again and the provider attaches. */
    (void)activate(p, NULL, NULL, NULL);
    CHECK(attach() && ts2_cheat_table_refs() == 1u);
    detach();
}


/* ---- Phase 3C.4B-2: Advanced mode ----------------------------------------- */

#define TOKENS_ON "[[cheat]]\nid = \"tokens\"\nlabel = \"All Tokens\"\ncode = [\"800C1618 1F1F\", " \
                  "\"800C161A 1F1F\", \"800C161C 1F1F\", \"800C161E 1F1F\", \"800C1620 1F1F\", " \
                  "\"800C1622 1F1F\", \"800C1624 1F1F\"]\nenabled = true\n"
#define TOKENS_DUP "[[cheat]]\nid = \"tokens-dup\"\nlabel = \"Tokens (as submitted)\"\ncode = [" \
                   "\"800C1618 1F1F\", \"800C161A 1F1F\", \"800C161C 1F1F\", \"800C161E 1F1F\", " \
                   "\"800C1620 1F1F\", \"800C1620 1F1F\", \"800C1624 1F1F\"]\n"
#define ADV_LINE "cheat-menu: advanced mode: unverified Action Replay codes allowed " \
                 "(verified data region only, start off)\n"

static int index_of(const char *id)
{
    size_t n = 0, k;
    const ts2_cheat_def *t = ts2_cheat_active_table(&n);
    for (k = 0; t && k < n; ++k)
        if (t[k].id && strcmp(t[k].id, id) == 0) return (int)k;
    return -1;
}

static void test_advanced_option_values(void)
{
    static const char *const off_values[] = { NULL, "false", "TRUE", "True", "1", "true ", "",
                                              "yes" };
    const char *p = write_text("adv-options.toml", HEADER TOKENS_ON HEALTH_ON);
    const char *out;
    size_t i;
    begin("advanced option values");
    for (i = 0; i < sizeof off_values / sizeof off_values[0]; ++i) {
        g_adv = off_values[i];
        out = activate(p, NULL, NULL, NULL);
        CHECK(ts2_cheat_menu_advanced_mode() == 0);
        CHECK(strcmp(out, "cheat-menu: cheat file: ok, 2 entries, 1 accepted, 1 rejected\n"
                          "cheat-menu:   cheat entry 1: address not permitted\n") == 0);
        CHECK(report() && report()->mode == TS2_CHEAT_EXT_MODE_SAFE && report()->unverified == 0u);
        CHECK(index_of("tokens") < 0 && index_of("health-14") == 2);
        CHECK(ts2_cheat_active_external_enabled(2) == 1);   /* Safe: file start state */
    }
    g_adv = "true";
    out = activate(p, NULL, NULL, NULL);
    CHECK(ts2_cheat_menu_advanced_mode() == 1);
    CHECK(strcmp(out, ADV_LINE
                      "cheat-menu: cheat file: ok, 2 entries, 2 accepted (1 unverified), 0 rejected\n"
                      "cheat-menu:   cheat entry 1: ok, unverified, enabled ignored, starts off\n") == 0);
    CHECK(strstr(out, p) == NULL);
    CHECK(report() && report()->mode == TS2_CHEAT_EXT_MODE_ADVANCED);
    CHECK(report() && report()->unverified == 1u && report()->enabled_ignored == 1u);
    CHECK(report() && report()->trust[0] == TS2_CHEAT_TRUST_ADVANCED &&
          report()->trust[1] == TS2_CHEAT_TRUST_SAFE);
    CHECK(index_of("tokens") == 2 && index_of("health-14") == 3);
    CHECK(ts2_cheat_active_trust(2) == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(ts2_cheat_engine_enabled_at(2) == 0);             /* enabled = true ignored */
    CHECK(ts2_cheat_engine_enabled_at(3) == 1);             /* Safe keeps it */
    CHECK(ts2_cheat_session_sealed() == 1);
}

static void test_advanced_no_file(void)
{
    begin("advanced, no file");
    g_adv = "true";
    CHECK(strcmp(activate(NULL, NULL, NULL, NULL), "") == 0);
    CHECK(ts2_cheat_menu_advanced_mode() == 1 && report() == NULL);
    CHECK(builtins_intact() && active_count() == 2u);
}

static void test_advanced_valid_multiline(void)
{
    const char *p = write_text("adv-tokens.toml", HEADER TOKENS_ON);
    char before[1024], after[1024];
    struct stat s1, s2;
    size_t n1, n2;
    FILE *f;
    uint32_t a;
    begin("advanced valid multi-line");
    f = fopen(p, "rb");
    n1 = f ? fread(before, 1, sizeof before, f) : 0;
    if (f) fclose(f);
    CHECK(stat(p, &s1) == 0);
    g_adv = "true";
    (void)activate(p, "true", NULL, NULL);
    CHECK(index_of("tokens") == 2 && active_count() == 3u);
    {
        size_t n = 0;
        const ts2_cheat_def *t = ts2_cheat_active_table(&n);
        CHECK(t && n == 3u && t[2].range_count == 1u && t[2].ranges[0].start == 0x800C1618u &&
              t[2].ranges[0].end == 0x800C1626u);
    }
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_OFF);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99);                /* built-in unchanged */
    CHECK(stub_u16(OFF(0x800C1618u)) == 0);                 /* unverified: still off */
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);      /* as the menu would */
    ts2_cheat_engine_vblank();
    for (a = 0x800C1618u; a < 0x800C1626u; ++a) CHECK(stub_u8(OFF(a)) == 0x1F);
    CHECK(stub_u8(OFF(0x800C1626u)) == 0 && stub_u8(OFF(0x800C1617u)) == 0);
    CHECK(g_stub.bad_addresses == 0u);
    /* The file is never written. */
    f = fopen(p, "rb");
    n2 = f ? fread(after, 1, sizeof after, f) : 0;
    if (f) fclose(f);
    CHECK(stat(p, &s2) == 0);
    CHECK(n1 == n2 && memcmp(before, after, n1) == 0);
    CHECK(s1.st_size == s2.st_size && s1.st_mtime == s2.st_mtime);
    /* Duplicate lines merge (submitted variant: 0x800C1622 stays untouched). */
    p = write_text("adv-dup.toml", HEADER TOKENS_DUP);
    stub_reset();
    (void)activate(p, NULL, NULL, NULL);
    CHECK(report() && report()->accepted == 1u && report()->enabled_ignored == 0u);
    {
        size_t n = 0;
        const ts2_cheat_def *t = ts2_cheat_active_table(&n);
        CHECK(t && n == 3u && t[2].range_count == 2u);
    }
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800C1620u)) == 0x1F1F && stub_u16(OFF(0x800C1622u)) == 0);
}

static void test_advanced_invalid_and_mixed(void)
{
    const char *out;
    begin("advanced invalid / mixed");
    g_adv = "true";
    /* A broken file: built-ins only, also in Advanced mode. */
    out = activate(write_text("adv-broken.toml", HEADER "[[cheat]]\nid = \"x\nlabel = \"" SECRET "\"\n"),
                   NULL, NULL, NULL);
    CHECK(starts(out, ADV_LINE "cheat-menu: cheat file: TOML syntax error (line "));
    CHECK(strstr(out, " - built-in cheats only\n") != NULL && strstr(out, SECRET) == NULL);
    CHECK(builtins_intact() && active_count() == 2u);
    /* Mixed: Safe (on), Advanced (on -> off), built-in overlap, unsupported
     * conditional, outside the region (overlay), Xploder line. */
    out = activate(write_text("adv-mixed.toml",
        HEADER HEALTH_ON TOKENS_ON
        "[[cheat]]\nid = \"coins-ext\"\nlabel = \"" SECRET "\"\ncode = [\"800B2226 0063\"]\n"
        "[[cheat]]\nid = \"cond\"\nlabel = \"Cond\"\ncode = [\"D00C1618 0001\", \"800C1700 0001\"]\n"
        "[[cheat]]\nid = \"overlay\"\nlabel = \"Overlay\"\ncode = [\"800D12C0 0001\"]\nenabled = true\n"
        "[[cheat]]\nid = \"xploder\"\nlabel = \"X\"\ncode = [\"810C1700 0001\"]\n"),
        NULL, NULL, NULL);
    CHECK(report() && report()->file == TS2_CHEAT_FILE_OK && report()->entries == 6u);
    CHECK(report() && report()->accepted == 2u && report()->unverified == 1u &&
          report()->enabled_ignored == 1u);
    CHECK(report() && report()->verdict[2] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN &&
          report()->verdict[3] == TS2_CHEAT_EXT_ERR_PARSE &&
          report()->verdict[4] == TS2_CHEAT_EXT_ERR_TARGET &&
          report()->verdict[5] == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(starts(out, ADV_LINE "cheat-menu: cheat file: ok, 6 entries, 2 accepted (1 unverified), "
                      "4 rejected\n"));
    CHECK(strstr(out, "cheat entry 1") == NULL);                    /* Safe, accepted: quiet */
    CHECK(strstr(out, "cheat-menu:   cheat entry 2: ok, unverified, enabled ignored, starts off\n") != NULL);
    CHECK(strstr(out, "cheat-menu:   cheat entry 3: ") != NULL && strstr(out, "cheat entry 6: ") != NULL);
    CHECK(strstr(out, SECRET) == NULL && strstr(out, "coins-ext") == NULL);
    CHECK(index_of("health-14") == 2 && index_of("tokens") == 3 && active_count() == 4u);
    CHECK(ts2_cheat_engine_enabled_at(2) == 1 && ts2_cheat_engine_enabled_at(3) == 0);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B221Eu)) == 14 && stub_u16(OFF(0x800C1618u)) == 0);
    CHECK(stub_u16(OFF(0x800D12C0u)) == 0 && stub_u16(OFF(0x800C1700u)) == 0);
    CHECK(stub_u16(OFF(0x800B2226u)) == 0);                         /* coins option off */
}

static void test_advanced_builtin_overlap(void)
{
    const char *out;
    begin("advanced built-in overlap");
    g_adv = "true";
    out = activate(write_text("adv-builtins.toml",
        HEADER "[[cheat]]\nid = \"coins\"\nlabel = \"C\"\ncode = [\"800B2226 0001\"]\nenabled = true\n"
               "[[cheat]]\nid = \"lives\"\nlabel = \"L\"\ncode = [\"300B2223 0000\"]\nenabled = true\n"),
        "true", "true", NULL);
    CHECK(report() && report()->accepted == 0u &&
          report()->verdict[0] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN &&
          report()->verdict[1] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    CHECK(strstr(out, "2 rejected") != NULL);
    CHECK(builtins_intact() && active_count() == 2u);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99 && stub_u16(OFF(0x800B2222u)) == 9);
}

static void test_advanced_session_only(void)
{
    const char *p = write_text("adv-session.toml", HEADER TOKENS_ON);
    int k;
    begin("advanced session only");
    g_adv = "true";
    (void)activate(p, "false", "false", "true");               /* remember-changes on */
    CHECK(attach());
    /* Toggle the unverified row (last): session only, nothing persisted. */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_UP);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(ts2_cheat_engine_enabled_at(2) == 1 && g_ncalls == 0);
    CHECK(g_view.last_persist == CHEAT_MENU_PERSIST_SESSION);
    /* A built-in toggle is persisted -- and never the advanced option. */
    recomp_runtime_ui_open(g_ui);
    in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
    CHECK(g_ncalls == 1 && strcmp(g_calls[0].o, "max-coins") == 0);
    for (k = 0; k < g_ncalls && k < 16; ++k) CHECK(strcmp(g_calls[k].o, "advanced-cheats") != 0);
    /* Live change refused; the mode stays as activated. */
    CHECK(g_plugin.option_changed && g_plugin.option_changed("advanced-cheats", "false") == 0);
    CHECK(ts2_cheat_menu_advanced_mode() == 1 && ts2_cheat_active_trust(2) == TS2_CHEAT_TRUST_ADVANCED);
    detach();
}

static void test_advanced_sealed_and_referenced(void)
{
    const char *a = write_text("adv-ref-a.toml", HEADER TOKENS_ON);
    const char *b = write_text("adv-ref-b.toml", HEADER HEALTH_ON);
    const ts2_cheat_def *t;
    size_t n = 0;
    const char *out;
    ts2_cheat_file_report again;
    begin("advanced sealed / referenced");
    g_adv = "true";
    (void)activate(a, NULL, NULL, NULL);
    /* Sealed after activation: no load in either mode. */
    CHECK(ts2_cheat_file_load_mode(b, TS2_CHEAT_EXT_MODE_ADVANCED, &again) == TS2_CHEAT_FILE_ERR_SEALED);
    CHECK(again.mode == TS2_CHEAT_EXT_MODE_ADVANCED && again.build == TS2_CHEAT_BUILD_ERR_SEALED);
    CHECK(attach());
    t = ts2_cheat_active_table(&n);
    CHECK(n == 3u);
    /* Reactivation while referenced (now Safe, other file): refused, the
     * referenced table keeps its entry and tier; it starts off again. */
    g_adv = NULL;
    out = activate(b, NULL, NULL, NULL);
    CHECK(strcmp(out, "cheat-menu: cheat file: cheat table in use (not loaded)\n") == 0);
    CHECK(ts2_cheat_menu_advanced_mode() == 0);
    {
        size_t n2 = 0;
        CHECK(ts2_cheat_active_table(&n2) == t && n2 == 3u);
    }
    CHECK(index_of("tokens") == 2 && ts2_cheat_active_trust(2) == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(ts2_cheat_engine_enabled_at(2) == 0);
    detach();
    (void)activate(b, NULL, NULL, NULL);
    CHECK(index_of("tokens") < 0 && index_of("health-14") == 2);
}

static void test_advanced_restart(void)
{
    const char *p = write_text("adv-restart.toml", HEADER TOKENS_ON HEALTH_ON);
    begin("advanced restart");
    g_adv = "true";
    (void)activate(p, NULL, NULL, NULL);
    CHECK(active_count() == 4u && index_of("tokens") == 2);
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);         /* switched on in session 1 */
    /* Session 2, option off: the unverified entry is gone. */
    g_adv = "false";
    (void)activate(p, NULL, NULL, NULL);
    CHECK(ts2_cheat_menu_advanced_mode() == 0 && active_count() == 3u && index_of("tokens") < 0);
    CHECK(report() && report()->verdict[0] == TS2_CHEAT_EXT_ERR_TARGET);
    /* Session 3, on again: back, and off again (no carry-over). */
    g_adv = "true";
    (void)activate(p, NULL, NULL, NULL);
    CHECK(ts2_cheat_menu_advanced_mode() == 1 && index_of("tokens") == 2);
    CHECK(ts2_cheat_engine_enabled_at(2) == 0);
    /* Session 4 without a file: built-ins only, mode still reported. */
    CHECK(strcmp(activate(NULL, NULL, NULL, NULL), "") == 0);
    CHECK(active_count() == 2u && ts2_cheat_menu_advanced_mode() == 1);
}

/* ---- Phase 3C.4B-3: Advanced rows in the menu ----------------------------- */

#define LONG40 "0123456789012345678901234567890123456789"
#define SECOND_ADV "[[cheat]]\nid = \"second\"\nlabel = \"Second\"\ncode = [\"800C1700 0001\"]\nenabled = true\n"
#define LONG_ADV "[[cheat]]\nid = \"long\"\nlabel = \"" LONG40 "\"\ncode = [\"300C1710 0002\"]\n"

static void toggle_row(size_t row)
{
    size_t k;
    recomp_runtime_ui_open(g_ui);
    for (k = 0; k < row; ++k) in(RECOMP_RUNTIME_UI_INPUT_DOWN);
    in(RECOMP_RUNTIME_UI_INPUT_ACCEPT);
    (void)cheat_menu_view_sync(&g_view);
    recomp_runtime_ui_close(g_ui);
}

static void test_advanced_menu_rows(void)
{
    const char *p = write_text("adv-menu.toml", HEADER HEALTH_ON TOKENS_ON SECOND_ADV LONG_ADV);
    size_t items = 0, nb = 0;
    const ts2_cheat_def *b = ts2_cheat_table(&nb);
    begin("advanced menu rows");
    g_adv = "true";
    (void)activate(p, NULL, NULL, "true");                  /* remember-changes on */
    CHECK(attach());
    (void)cheat_menu_view_items(&g_view, &items);
    CHECK(items == 7u);         /* Resume, 2 built-ins, health, 7-line tokens as ONE row, 2 more */
    /* Built-in and Safe rows: presentation unchanged (the table's own label). */
    CHECK(g_view.items[1].label == b[0].label && g_view.items[2].label == b[1].label);
    CHECK(strcmp(S(g_view.items[3].label), "Full Health") == 0);
    {
        size_t n = 0;
        const ts2_cheat_def *t = ts2_cheat_active_table(&n);
        CHECK(t && n == 6u && g_view.items[3].label == t[2].label);
    }
    CHECK(g_view.trust[0] == TS2_CHEAT_TRUST_SAFE && g_view.trust[2] == TS2_CHEAT_TRUST_SAFE);
    CHECK(ts2_cheat_engine_enabled_at(2) == 1);             /* Safe enabled = true kept */
    CHECK(strncmp(g_view.row_text[2], "Armed", 5) == 0);    /* on, before the first VBlank */
    /* Advanced rows: marked from the build's tier, start off with the warning. */
    CHECK(strcmp(S(g_view.items[4].label), "All Tokens (unverified)") == 0);
    CHECK(strcmp(S(g_view.items[5].label), "Second (unverified)") == 0);
    CHECK(strcmp(S(g_view.items[6].label), LONG40 " (unverified)") == 0);      /* no truncation */
    CHECK(strlen(S(g_view.items[6].label)) == 53u);
    CHECK(strcmp(S(g_view.items[4].key), "cheat_menu.ext.tokens") == 0);     /* keys unchanged */
    CHECK(g_view.trust[3] == TS2_CHEAT_TRUST_ADVANCED && g_view.trust[4] == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(strcmp(g_view.row_text[3], CHEAT_MENU_UNVERIFIED_OFF) == 0);
    CHECK(strcmp(g_view.row_text[5], CHEAT_MENU_UNVERIFIED_OFF) == 0);
    CHECK(ts2_cheat_engine_enabled_at(3) == 0 && ts2_cheat_engine_enabled_at(4) == 0 &&
          ts2_cheat_engine_enabled_at(5) == 0);
    {
        int v = -1;
        CHECK(cheat_menu_view_get_value(&g_view, &g_view.items[4], &v) == 1 && v == 0);
    }
    /* Manual toggle ON of one unverified row: only that one, session only. */
    toggle_row(4);
    CHECK(ts2_cheat_engine_enabled_at(3) == 1);
    CHECK(ts2_cheat_engine_enabled_at(4) == 0 && ts2_cheat_engine_enabled_at(5) == 0);
    CHECK(g_ncalls == 0 && g_view.last_persist == CHEAT_MENU_PERSIST_SESSION);
    CHECK(strcmp(cheat_menu_view_status(&g_view),
                 "All Tokens (unverified): armed, applies after resume - session only") == 0);
    ts2_cheat_engine_vblank();
    (void)cheat_menu_view_sync(&g_view);
    CHECK(stub_u16(OFF(0x800C1618u)) == 0x1F1F && stub_u16(OFF(0x800C1624u)) == 0x1F1F);
    CHECK(stub_u16(OFF(0x800C1700u)) == 0 && stub_u8(OFF(0x800C1710u)) == 0);
    CHECK(strncmp(g_view.row_text[3], "Active", 6) == 0);
    /* Toggle OFF again: the writes stop. */
    toggle_row(4);
    CHECK(ts2_cheat_engine_enabled_at(3) == 0 && g_ncalls == 0);
    stub_reset();
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800C1618u)) == 0 && stub_u16(OFF(0x800B221Eu)) == 14);
    (void)cheat_menu_view_sync(&g_view);
    CHECK(strcmp(g_view.row_text[3], CHEAT_MENU_UNVERIFIED_OFF) == 0);
    /* The second unverified row toggles on its own. */
    toggle_row(5);
    CHECK(ts2_cheat_engine_enabled_at(4) == 1 && ts2_cheat_engine_enabled_at(3) == 0);
    /* A built-in toggle is still persisted, under its own id only. */
    toggle_row(1);
    CHECK(g_ncalls == 1 && strcmp(g_calls[0].o, "max-coins") == 0);
    detach();
    /* Session restart: every unverified row is off again and still marked. */
    (void)activate(p, NULL, NULL, "true");
    CHECK(attach());
    CHECK(ts2_cheat_engine_enabled_at(3) == 0 && ts2_cheat_engine_enabled_at(4) == 0);
    CHECK(strcmp(S(g_view.items[4].label), "All Tokens (unverified)") == 0);
    CHECK(strcmp(g_view.row_text[4], CHEAT_MENU_UNVERIFIED_OFF) == 0);
    CHECK(ts2_cheat_table_refs() == 1u);
    detach();
    CHECK(ts2_cheat_table_refs() == 0u);
    /* Safe mode, same file: no unverified rows, Safe row unchanged. */
    g_adv = NULL;
    (void)activate(p, NULL, NULL, NULL);
    CHECK(attach());
    (void)cheat_menu_view_items(&g_view, &items);
    CHECK(items == 4u && strcmp(S(g_view.items[3].label), "Full Health") == 0);
    CHECK(strstr(S(g_view.items[3].label), "unverified") == NULL);
    detach();
}

static void test_view_tier_independent(void)
{
    /* Synthetic (non-external) rows: the tier alone marks a row and keeps it
     * session-only; a label that merely says "unverified" changes nothing. */
    static const psxcheat_psx_range r_a[] = { { 0x800C1640u, 0x800C1642u } };
    static const psxcheat_psx_range r_b[] = { { 0x800C1650u, 0x800C1652u } };
    static const psxcheat_psx_range r_h[] = { { 0x800B221Eu, 0x800B2220u } };
    static const ts2_cheat_def defs[] = {
        { "adv-a", "Adv A", "800C1640 0001\n", r_a, 1u, NULL, NULL, NULL },
        { "odd-tier", "Odd", "800C1650 0001\n", r_b, 1u, NULL, NULL, NULL },
        { "named", "Looks (unverified)", "800B221E 000E\n", r_h, 1u, NULL, NULL, NULL },
    };
    const ts2_cheat_trust trust[] = { TS2_CHEAT_TRUST_ADVANCED, (ts2_cheat_trust)7,
                                      TS2_CHEAT_TRUST_SAFE };
    const ts2_cheat_trust engine_trust[] = { TS2_CHEAT_TRUST_ADVANCED, TS2_CHEAT_TRUST_ADVANCED,
                                             TS2_CHEAT_TRUST_SAFE };
    size_t nv = 0;
    const psxcheat_psx_range *v = ts2_cheat_writable_ranges(&nv);
    int calls = 0;
    begin("view tier independent");
    CHECK(ts2_cheat_engine_init_table_tiered(defs, 3, v, nv, engine_trust, NULL) == TS2_CHEAT_TABLE_OK);
    memset(&g_host, 0, sizeof g_host);
    g_host.abi_version = PSX_MENU_ABI_VERSION;
    g_host.mod_set = fake_mod_set;
    g_ncalls = 0;
    cheat_menu_view_init_table_tiered(&g_view, defs, 3, trust);
    cheat_menu_view_set_persist(&g_view, 1, cheat_menu_persist_via_host, &g_host);
    (void)cheat_menu_view_sync(&g_view);
    CHECK(strcmp(S(g_view.items[1].label), "Adv A (unverified)") == 0);
    CHECK(strcmp(S(g_view.items[2].label), "Odd (unverified)") == 0);     /* unknown: fail closed */
    CHECK(g_view.items[3].label == defs[2].label);                       /* label text ignored */
    CHECK(g_view.trust[2] == TS2_CHEAT_TRUST_SAFE);
    CHECK(cheat_menu_view_set_value(&g_view, &g_view.items[1], 1) == 1);
    CHECK(cheat_menu_view_set_value(&g_view, &g_view.items[2], 1) == 1);
    (void)cheat_menu_view_sync(&g_view);
    calls = g_ncalls;
    CHECK(calls == 0 && g_view.last_persist == CHEAT_MENU_PERSIST_SESSION);
    CHECK(cheat_menu_view_set_value(&g_view, &g_view.items[3], 1) == 1);
    (void)cheat_menu_view_sync(&g_view);
    CHECK(g_ncalls == 1 && strcmp(g_calls[0].o, "named") == 0);          /* Safe row persists */
    /* NULL tiers: exactly the old presentation. */
    cheat_menu_view_init_table(&g_view, defs, 3);
    CHECK(g_view.items[1].label == defs[0].label && g_view.items[2].label == defs[1].label);
    ts2_cheat_engine_shutdown();
}

#else  /* TS2_CHEAT_RUNTIME_TEST_HEALTH */

static void test_runtime_health_ignores_file(void)
{
    const char *p = write_text("valid.toml", HEADER HEALTH_ON);
    const char *out;
    g_test = "runtime test build";
    ts2_cheat_engine_shutdown();
    ts2_cheat_combined_reset();
    stub_reset();
    /* No file: only the synthetic entry, no "ignored" line. */
    g_plugin.resource_reads = 0;
    out = activate(NULL, NULL, NULL, NULL);
    CHECK(g_plugin.resource_reads == 1);
    CHECK(strstr(out, "ignored") == NULL);
    CHECK(strstr(out, "cheat-menu: RUNTIME TEST entry 'test-health': ok") != NULL);
    CHECK(builtins_intact() && active_count() == 3u && strcmp(id_at(2), "test-health") == 0);
    CHECK(ts2_cheat_menu_file_report() == NULL);
    /* A selected file is ignored: reported, not loaded. */
    out = activate(p, NULL, NULL, NULL);
    CHECK(starts(out, "cheat-menu: RUNTIME TEST build: external cheat file ignored\n"));
    CHECK(strstr(out, "cheat file:") == NULL && strstr(out, p) == NULL);
    CHECK(ts2_cheat_menu_file_report() == NULL);
    CHECK(active_count() == 3u && strcmp(id_at(2), "test-health") == 0 && !has_id("health-14"));
    CHECK(ts2_cheat_active_external_count() == 1u && ts2_cheat_active_external_enabled(2) == 0);
    /* Synthetic entry off at start: health untouched. */
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B221Eu)) == 0);
    /* Advanced option on: still ignored -- no file, no Advanced mode. */
    g_adv = "true";
    out = activate(write_text("adv-tokens.toml",
                              HEADER "[[cheat]]\nid = \"tokens\"\nlabel = \"T\"\n"
                                     "code = [\"800C1618 1F1F\"]\nenabled = true\n"),
                   NULL, NULL, NULL);
    CHECK(starts(out, "cheat-menu: RUNTIME TEST build: external cheat file ignored\n"));
    CHECK(strstr(out, "advanced mode") == NULL && strstr(out, "cheat file:") == NULL);
    CHECK(ts2_cheat_menu_advanced_mode() == 0 && ts2_cheat_menu_file_report() == NULL);
    CHECK(active_count() == 3u && strcmp(id_at(2), "test-health") == 0 && !has_id("tokens"));
    CHECK(ts2_cheat_active_trust(2) == TS2_CHEAT_TRUST_SAFE);
    g_adv = NULL;
}

#endif

int main(void)
{
    snprintf(g_dir, sizeof g_dir, "%s", TS2_CHEAT_FILE_TEST_DIR);
    mkdir(g_dir, 0755);
    CHECK(g_plugin.activation != NULL && g_plugin.activation_regs == 1);
    CHECK(g_plugin.refused_regs == 0);
#if !defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
    test_no_resource();
    test_valid_file();
    test_malformed_file();
    test_invalid_beside_valid();
    test_unreadable_file();
    test_repeated_activation();
    test_referenced_table();
    test_session_only();
    test_builtins_unchanged();
    test_provider_lifecycle();
    test_advanced_option_values();
    test_advanced_no_file();
    test_advanced_valid_multiline();
    test_advanced_invalid_and_mixed();
    test_advanced_builtin_overlap();
    test_advanced_session_only();
    test_advanced_sealed_and_referenced();
    test_advanced_restart();
    test_advanced_menu_rows();
    test_view_tier_independent();
    detach();
    printf("cheat file integration: %d checks, %d failures\n", g_checks, g_failures);
#else
    test_runtime_health_ignores_file();
    printf("cheat file integration (runtime health test build): %d checks, %d failures\n",
           g_checks, g_failures);
#endif
    ts2_cheat_engine_shutdown();
    ts2_cheat_combined_reset();
    return g_failures ? 1 : 0;
}
