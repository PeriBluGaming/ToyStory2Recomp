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
 * Threading: build once on the main thread during activation, before the
 * engine and the menu are set up. The combined table is immutable afterwards
 * and lives for the rest of the process. */

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
    TS2_CHEAT_BUILD_ERR_INTERNAL        /* combined table failed re-validation: built-ins only */
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

/* Back to "no combined table" (tests). */
void ts2_cheat_combined_reset(void);

const char *ts2_cheat_ext_status_name(ts2_cheat_ext_status status);

#ifdef __cplusplus
}
#endif

#endif
