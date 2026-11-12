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
