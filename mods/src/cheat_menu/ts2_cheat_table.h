/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_TABLE_H
#define TS2_CHEAT_TABLE_H

/* Cheat Menu (Phase 2E-B): the static Toy Story 2 cheat table and its
 * validation. Each entry is one independently switchable cheat with its own
 * Action Replay program (one or more lines) and its OWN writable allowlist.
 *
 * Validation (ts2_cheat_validate_table) is pure: no guest access, no engine
 * state. Two kinds of failure:
 *
 *   Table errors (fatal, ts2_cheat_table_status): the table as a whole is
 *   unusable and the engine sets up NO cheat. These are the structural
 *   problems that make the per-cheat mapping ambiguous or the policy itself
 *   untrustworthy: no table, no or too many entries, a missing/empty or
 *   duplicate id, an invalid global RAM policy.
 *
 *   Cheat errors (ts2_cheat_def_status, one per entry): only that cheat is
 *   UNAVAILABLE; every other cheat is set up normally. A missing label or
 *   code, a program that does not parse, no/too many/invalid ranges, a range
 *   not covered by the global policy, a range overlapping another cheat's
 *   (both cheats are isolated: the overlap is symmetric), or a program line
 *   whose write target (width, alignment, span) is not inside the cheat's
 *   own ranges.
 *
 * The global RAM policy (ts2_cheat_memory.h: D1/D2 rules, explicit verified
 * list) stays the single authority: a cheat range must be permitted by
 * ts2_cheat_range_permitted AND lie inside one entry of the policy list. */

#include <stddef.h>

#include "psxcheat.h"
#include "psxcheat_psxrecomp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS2_CHEAT_MAX 8u            /* entries per table (static engine slots) */
#define TS2_CHEAT_ID_MAX 32u        /* bytes of an id, excluding NUL */
#define TS2_CHEAT_KEY_MAX 48u       /* bytes of a menu key, excluding NUL */
#define TS2_CHEAT_HINT_MAX 64u      /* bytes of a hint, excluding NUL */

typedef struct ts2_cheat_def {
    const char *id;                 /* stable key = manifest option id */
    const char *label;              /* menu label */
    const char *code;               /* Action Replay text; lines separated by '\n' */
    const psxcheat_psx_range *ranges;  /* this cheat's writable allowlist (KSEG0, half-open) */
    size_t range_count;
    /* Optional menu presentation (Phase 2E-C; NULL = generic). Not
     * validated beyond length: they never reach the engine. */
    const char *menu_key;           /* recomp-ui item key; NULL: "cheat_menu.<id>" */
    const char *off_hint;           /* row text when off: "Off - <off_hint>" */
    const char *active_hint;        /* row text when active: "Active - <active_hint>" */
} ts2_cheat_def;

typedef enum ts2_cheat_table_status {
    TS2_CHEAT_TABLE_OK = 0,
    TS2_CHEAT_TABLE_ERR_NULL,       /* no table */
    TS2_CHEAT_TABLE_ERR_EMPTY,      /* zero entries */
    TS2_CHEAT_TABLE_ERR_TOO_MANY,   /* more than TS2_CHEAT_MAX entries */
    TS2_CHEAT_TABLE_ERR_ID,         /* an id is NULL, empty or too long */
    TS2_CHEAT_TABLE_ERR_DUPLICATE_ID,
    TS2_CHEAT_TABLE_ERR_POLICY      /* the global policy list is invalid */
} ts2_cheat_table_status;

typedef enum ts2_cheat_def_status {
    TS2_CHEAT_DEF_OK = 0,
    TS2_CHEAT_DEF_ERR_NOT_CHECKED,  /* table error: entry never validated */
    TS2_CHEAT_DEF_ERR_LABEL,        /* NULL or empty label; over-long label, key or hint */
    TS2_CHEAT_DEF_ERR_CODE,         /* NULL or empty code */
    TS2_CHEAT_DEF_ERR_PARSE,        /* program does not parse, or has no line */
    TS2_CHEAT_DEF_ERR_RANGES,       /* no ranges, too many, or an empty/inverted one */
    TS2_CHEAT_DEF_ERR_POLICY,       /* a range not permitted / not inside the policy list */
    TS2_CHEAT_DEF_ERR_OVERLAP,      /* ranges overlap (own or another cheat's) */
    TS2_CHEAT_DEF_ERR_TARGET        /* a line's write target is not inside own ranges */
} ts2_cheat_def_status;

/* The production table (count via *count). Entry 0 is Max Coins: the legacy
 * single-cheat engine API addresses index 0. */
const ts2_cheat_def *ts2_cheat_table(size_t *count);

/* Validate `defs` against the policy list. Fills per_def[0..n) (when n is
 * within TS2_CHEAT_MAX; may be NULL) with each entry's status; on a table
 * error every entry reads TS2_CHEAT_DEF_ERR_NOT_CHECKED. Pure. */
ts2_cheat_table_status ts2_cheat_validate_table(const ts2_cheat_def *defs, size_t n,
                                                const psxcheat_psx_range *policy,
                                                size_t policy_n,
                                                ts2_cheat_def_status *per_def);

/* Validate one parsed program's write targets against an adapter configured
 * with the cheat's ranges: known width, 16-bit aligned, inside 2 MiB RAM,
 * and the whole span inside one configured range. 1 when all lines pass. */
int ts2_cheat_program_targets_valid(const psxcheat_program *program,
                                    const psxcheat_psx_adapter *adapter);

const char *ts2_cheat_table_status_name(ts2_cheat_table_status status);
const char *ts2_cheat_def_status_name(ts2_cheat_def_status status);

#ifdef __cplusplus
}
#endif

#endif
