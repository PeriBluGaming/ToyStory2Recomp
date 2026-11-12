/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_EXTERNAL_H
#define TS2_CHEAT_EXTERNAL_H

/* Cheat Menu (Phase 3B): external (user-supplied) cheats -- validation and
 * the combined cheat table. No file I/O here: a loader (Phase 3C) turns one
 * TOML file into ts2_cheat_external_input records and hands them over.
 *
 * Trust boundary. An external entry is untrusted data. It carries an id, a
 * label, an optional description, its Action Replay lines and a start state
 * -- and NO memory ranges. The write spans are derived from the parsed code
 * lines only, and every span must lie inside one entry of the trusted
 * EXTERNAL policy list, which is TS2 source code (ts2_cheat_memory.h), never
 * file content. The cheat's adapter is configured with exactly those derived
 * spans (least privilege). The external policy must itself lie inside the
 * verified writable list; if it does not, no external entry is accepted.
 *
 * Isolation. Built-ins are validated as before (ts2_cheat_validate_table,
 * whose table errors are fatal). External entries never reach that fatal
 * tier unvalidated: each one is checked on its own and only accepted entries
 * are appended. Every problem rejects only the entry concerned:
 *   - invalid id/label/description/code, unsupported code type, a target
 *     outside the external policy                      -> that entry;
 *   - an id equal to a built-in id                      -> that entry;
 *   - an id shared by several external entries          -> all of them;
 *   - write spans overlapping a built-in cheat's ranges -> that entry
 *     (the built-in is never affected);
 *   - write spans overlapping another accepted external -> both entries;
 *   - beyond the slot capacity                          -> that entry.
 * Only entries that passed their own checks take part in the overlap and
 * duplicate-id comparisons between external entries. Order is deterministic:
 * built-ins first (table order), then accepted external entries in input
 * order.
 *
 * External cheats are session-only: their start state comes from the input
 * (the file), and the Cheat Menu never persists them (see
 * ts2_cheat_def_is_external).
 *
 * Threading: build on the main thread during activation, before the engine
 * and the menu are set up.
 *
 * Session lifecycle (Phase 3C.2A). psxrecomp can end a game session and
 * start another one in the same process (PSX_MENU_EXIT_TO_FRONTEND). The
 * combined table and everything it points at live in static storage (never
 * freed), but its CONTENT may only change while nothing uses it:
 *   - the engine (g_engine.defs) uses it from ts2_cheat_engine_activate()
 *     until ts2_cheat_engine_shutdown();
 *   - the menu (cheat_menu_view rows, recomp-ui item labels/keys) uses it
 *     from the provider's attach to its detach; the provider holds a table
 *     reference for exactly that time (ts2_cheat_table_ref_acquire/release).
 * ts2_cheat_session_begin() opens a new build window only when no table
 * reference is held; ts2_cheat_engine_activate() seals it. While sealed or
 * referenced, combined_build() refuses (TS2_CHEAT_BUILD_ERR_SEALED) and
 * leaves the table untouched. The plugin's activation runs
 *   engine shutdown -> session_begin -> build -> engine activate (seal)
 * on the main thread, after the previous session was torn down (provider
 * detached, no VBlank) and before the new provider attach.
 *
 * Borrowed pointers: the verified list passed to combined_build_with() is
 * kept by pointer (ts2_cheat_active_verified) and must outlive every later
 * engine activation -- static storage in production (ts2_cheat_memory.c)
 * and in the tests. Built-in definitions are copied by value; their strings
 * and range arrays must be static (they are, ts2_cheat_table.c). External
 * entries are copied into static storage; their inputs need only live for
 * the build call. */

#include <stddef.h>

#include "psxcheat_psxrecomp.h"
#include "ts2_cheat_table.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS2_CHEAT_EXTERNAL_MAX      16u   /* accepted external entries, at most */
#define TS2_CHEAT_EXTERNAL_INPUT_MAX 64u  /* input records looked at; more: rejected */
#define TS2_CHEAT_EXT_LINES_MAX     32u   /* code lines per entry */
#define TS2_CHEAT_EXT_LINE_MAX      32u   /* bytes per code line, excluding NUL */
#define TS2_CHEAT_EXT_LABEL_MAX     40u   /* bytes, printable ASCII */
#define TS2_CHEAT_EXT_DESC_MAX      64u   /* bytes, printable ASCII (= hint limit) */
#define TS2_CHEAT_EXT_KEY_PREFIX    "cheat_menu.ext."

/* One untrusted entry, as a loader parsed it. Strings need only live for
 * the build call: accepted entries are copied into static storage. */
typedef struct ts2_cheat_external_input {
    const char *id;                 /* [a-z0-9-], 1..TS2_CHEAT_ID_MAX, starts alnum */
    const char *label;              /* printable ASCII, 1..TS2_CHEAT_EXT_LABEL_MAX */
    const char *description;        /* optional (NULL): printable ASCII */
    const char *const *code_lines;  /* each exactly one code: "TTAAAAAA VVVV" */
    size_t code_line_count;         /* 1..TS2_CHEAT_EXT_LINES_MAX */
    int enabled;                    /* start state (session only) */
} ts2_cheat_external_input;

typedef enum ts2_cheat_ext_status {
    TS2_CHEAT_EXT_OK = 0,
    TS2_CHEAT_EXT_ERR_NOT_CHECKED,      /* build refused as a whole (see below) */
    TS2_CHEAT_EXT_ERR_ID,               /* missing, too long or invalid characters */
    TS2_CHEAT_EXT_ERR_ID_BUILTIN,       /* same id as a built-in cheat */
    TS2_CHEAT_EXT_ERR_ID_DUPLICATE,     /* id shared by several external entries */
    TS2_CHEAT_EXT_ERR_LABEL,
    TS2_CHEAT_EXT_ERR_DESCRIPTION,
    TS2_CHEAT_EXT_ERR_CODE,             /* no/too many lines, bad line text */
    TS2_CHEAT_EXT_ERR_PARSE,            /* a line is not exactly one valid code */
    TS2_CHEAT_EXT_ERR_TARGET,           /* a write outside the external policy */
    TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN,  /* writes into a built-in cheat's ranges */
    TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL, /* writes overlap another external entry */
    TS2_CHEAT_EXT_ERR_CAPACITY          /* no slot left / beyond the input limit */
} ts2_cheat_ext_status;

typedef enum ts2_cheat_build_status {
    TS2_CHEAT_BUILD_OK = 0,             /* built-ins + accepted external entries */
    TS2_CHEAT_BUILD_ERR_BUILTINS,       /* built-in table invalid: table = built-ins as given,
                                           no external entry accepted (engine decides) */
    TS2_CHEAT_BUILD_ERR_POLICY,         /* external policy invalid / not inside the
                                           verified list: built-ins only */
    TS2_CHEAT_BUILD_ERR_INTERNAL,       /* combined table failed re-validation: built-ins only */
    TS2_CHEAT_BUILD_ERR_SEALED          /* refused: table sealed or referenced; unchanged */
} ts2_cheat_build_status;

/* Build the combined table from explicit inputs (tests; the production
 * wrapper below passes the real tables). status[0..n) (may be NULL)
 * receives each input's verdict; inputs beyond TS2_CHEAT_EXTERNAL_INPUT_MAX
 * are not looked at and read TS2_CHEAT_EXT_ERR_CAPACITY. Replaces any previous combined table.
 * MAIN THREAD, activation only. */
ts2_cheat_build_status ts2_cheat_combined_build_with(
    const ts2_cheat_def *builtins, size_t builtin_count,
    const psxcheat_psx_range *verified, size_t verified_count,
    const psxcheat_psx_range *external_policy, size_t external_policy_count,
    const ts2_cheat_external_input *inputs, size_t n,
    ts2_cheat_ext_status *status);

/* Production: built-in table, verified list, external policy. */
ts2_cheat_build_status ts2_cheat_combined_build(const ts2_cheat_external_input *inputs,
                                                size_t n, ts2_cheat_ext_status *status);

/* The table the engine and the menu use: the combined table once built,
 * else the built-in table. Immutable after activation. */
const ts2_cheat_def *ts2_cheat_active_table(size_t *count);

/* The verified writable list the active table was built against (the engine
 * validates the active table with it): the production list unless a
 * combined table was built from another one (tests). */
const psxcheat_psx_range *ts2_cheat_active_verified(size_t *count);

/* Start state of active-table entry `index` for an external entry (its
 * input's `enabled`); 0 for built-ins and out of range (built-ins take
 * their launcher option). */
int ts2_cheat_active_external_enabled(size_t index);

/* Number of built-in / accepted external entries in the active table. */
size_t ts2_cheat_active_builtin_count(void);
size_t ts2_cheat_active_external_count(void);

/* 1 when `d` points at an accepted external entry of the combined table
 * (session-only, never persisted). */
int ts2_cheat_def_is_external(const ts2_cheat_def *d);

#if defined(TS2_CHEAT_RUNTIME_TEST_HEALTH)
/* Phase 3B.3 controlled runtime test: one synthetic external entry,
 * "test-health" / "TEST Full Health" / TS2_CHEAT_HEALTH_RECOMMENDED,
 * off at start. Pure data; it goes through the normal external validation
 * like any file entry. Defined in cheat_menu_plugin.c and compiled only
 * into a developer build configured with -DTS2_CHEAT_RUNTIME_TEST_HEALTH=ON
 * (default OFF; see the root CMakeLists.txt): normal builds contain neither
 * the function nor its data. The plugin builds it only while the developer
 * feature is active. */
ts2_cheat_external_input ts2_cheat_runtime_test_health_input(void);
#endif

/* ---- Session lifecycle (Phase 3C.2A), main thread ---------------------- */

/* Open a new session's build window: drops the previous combined table
 * (back to built-ins only) and unseals. Refused (returns 0, nothing
 * changes) while a table reference is held. Returns 1 when opened. */
int ts2_cheat_session_begin(void);
/* Seal the active table for the session (ts2_cheat_engine_activate). */
void ts2_cheat_session_seal(void);
int ts2_cheat_session_sealed(void);
/* The menu provider holds one reference from attach to detach. */
void ts2_cheat_table_ref_acquire(void);
void ts2_cheat_table_ref_release(void);
unsigned ts2_cheat_table_refs(void);

/* Back to "no combined table", unsealed, no references (tests). */
void ts2_cheat_combined_reset(void);

const char *ts2_cheat_ext_status_name(ts2_cheat_ext_status status);

#ifdef __cplusplus
}
#endif

#endif
