/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "ts2_cheat_external.h"

#include <stdio.h>
#include <string.h>

#include "ts2_cheat_memory.h"

/* Slot masks in the menu bridge are 32-bit. */
typedef char ts2_cheat_max_fits_masks[(TS2_CHEAT_MAX <= 32u) ? 1 : -1];
typedef char ts2_cheat_ext_fits_slots[(TS2_CHEAT_EXTERNAL_MAX < TS2_CHEAT_MAX) ? 1 : -1];
/* "cheat_menu.ext." + the longest id must be a valid menu key. */
typedef char ts2_cheat_ext_key_fits[
    (sizeof TS2_CHEAT_EXT_KEY_PREFIX - 1u + TS2_CHEAT_ID_MAX <= TS2_CHEAT_KEY_MAX) ? 1 : -1];

/* ---- Storage (static, process lifetime) ----------------------------------- */

typedef struct ext_store {
    char id[TS2_CHEAT_ID_MAX + 1u];
    char label[TS2_CHEAT_EXT_LABEL_MAX + 1u];
    char desc[TS2_CHEAT_EXT_DESC_MAX + 1u];
    char key[TS2_CHEAT_KEY_MAX + 1u];
    char code[TS2_CHEAT_EXT_LINES_MAX * (TS2_CHEAT_EXT_LINE_MAX + 1u) + 1u];
    psxcheat_psx_range ranges[TS2_CHEAT_EXT_LINES_MAX];
    size_t range_count;
    int enabled;
    ts2_cheat_trust trust;          /* assigned by the build, never by the input */
} ext_store;

static struct {
    int built;
    ts2_cheat_def table[TS2_CHEAT_MAX];
    size_t count;
    size_t builtin_count;
    size_t external_count;
    const psxcheat_psx_range *verified;
    size_t verified_count;
    ext_store ext[TS2_CHEAT_EXTERNAL_MAX];
} g_comb;

/* Session lifecycle (see the header). */
static struct {
    int sealed;
    unsigned refs;
} g_life;

/* Scratch for one build: each input's verdict and derived write spans. */
static struct {
    ts2_cheat_ext_status status[TS2_CHEAT_EXTERNAL_INPUT_MAX];
    psxcheat_psx_range ranges[TS2_CHEAT_EXTERNAL_INPUT_MAX][TS2_CHEAT_EXT_LINES_MAX];
    size_t range_count[TS2_CHEAT_EXTERNAL_INPUT_MAX];
    ts2_cheat_trust trust[TS2_CHEAT_EXTERNAL_INPUT_MAX];
} g_scratch;

/* ---- Helpers ----------------------------------------------------------------- */

static int overlaps(psxcheat_psx_range a, psxcheat_psx_range b)
{
    return a.start < b.end && b.start < a.end;
}

static int inside(psxcheat_psx_range inner, psxcheat_psx_range outer)
{
    return inner.start >= outer.start && inner.end <= outer.end;
}

/* Length of s when it is 1..max bytes long, else 0 (also for NULL). */
static size_t bounded_len(const char *s, size_t max)
{
    size_t k;
    if (!s || !s[0]) return 0;
    for (k = 0; k <= max; ++k)
        if (s[k] == '\0') return k;
    return 0;
}

static int printable(char c) { return c >= 0x20 && c <= 0x7E; }

static int valid_id(const char *id)
{
    const size_t len = bounded_len(id, TS2_CHEAT_ID_MAX);
    size_t k;
    if (!len) return 0;
    for (k = 0; k < len; ++k) {
        const char c = id[k];
        const int alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (!alnum && !(c == '-' && k > 0)) return 0;
    }
    return 1;
}

static int valid_text(const char *s, size_t max)
{
    const size_t len = bounded_len(s, max);
    size_t k;
    if (!len || s[0] == ' ') return 0;
    for (k = 0; k < len; ++k)
        if (!printable(s[k])) return 0;
    return 1;
}

static int valid_line_text(const char *s)
{
    const size_t len = bounded_len(s, TS2_CHEAT_EXT_LINE_MAX);
    size_t k;
    if (!len) return 0;
    for (k = 0; k < len; ++k)
        if (!printable(s[k]) && s[k] != '\t') return 0;
    return 1;
}

static int builtin_ranges_overlap(const ts2_cheat_def *b, size_t nb, psxcheat_psx_range r)
{
    size_t i, k;
    for (i = 0; i < nb; ++i)
        for (k = 0; k < b[i].range_count; ++k)
            if (overlaps(b[i].ranges[k], r)) return 1;
    return 0;
}

/* The external policy: valid D1/D2 entries, pairwise disjoint, each inside
 * one verified entry. An empty policy is valid (nothing is writable). */
static int external_policy_valid(const psxcheat_psx_range *verified, size_t nv,
                                 const psxcheat_psx_range *policy, size_t np)
{
    size_t i, j;
    if (np == 0u) return 1;
    if (!policy || !verified) return 0;
    for (i = 0; i < np; ++i) {
        int covered = 0;
        if (!ts2_cheat_range_permitted(policy[i])) return 0;
        for (j = 0; j < i; ++j)
            if (overlaps(policy[i], policy[j])) return 0;
        for (j = 0; j < nv && !covered; ++j) covered = inside(policy[i], verified[j]);
        if (!covered) return 0;
    }
    return 1;
}

/* Index of the policy entry holding span r, or -1. */
static long policy_index(const psxcheat_psx_range *policy, size_t np, psxcheat_psx_range r)
{
    size_t j;
    for (j = 0; j < np; ++j)
        if (inside(r, policy[j])) return (long)j;
    return -1;
}

/* Validate one input on its own; fills the scratch spans for slot i. */
static ts2_cheat_ext_status check_input(const ts2_cheat_external_input *in, size_t i,
                                        const ts2_cheat_def *builtins, size_t nb,
                                        const psxcheat_psx_range *policy, size_t np,
                                        ts2_cheat_ext_mode mode)
{
    psxcheat_psx_range spans[TS2_CHEAT_EXT_LINES_MAX];
    long owner[TS2_CHEAT_EXT_LINES_MAX];
    size_t k, j, count = 0, merged = 0;
    int safe = 1, advanced = 1;     /* every span inside policy / region */

    g_scratch.range_count[i] = 0;
    g_scratch.trust[i] = TS2_CHEAT_TRUST_SAFE;
    if (!valid_id(in->id)) return TS2_CHEAT_EXT_ERR_ID;
    for (k = 0; k < nb; ++k)
        if (builtins[k].id && strcmp(builtins[k].id, in->id) == 0)
            return TS2_CHEAT_EXT_ERR_ID_BUILTIN;
    if (!valid_text(in->label, TS2_CHEAT_EXT_LABEL_MAX)) return TS2_CHEAT_EXT_ERR_LABEL;
    if (in->description && !valid_text(in->description, TS2_CHEAT_EXT_DESC_MAX))
        return TS2_CHEAT_EXT_ERR_DESCRIPTION;
    if (!in->code_lines || in->code_line_count == 0u ||
        in->code_line_count > TS2_CHEAT_EXT_LINES_MAX)
        return TS2_CHEAT_EXT_ERR_CODE;
    for (k = 0; k < in->code_line_count; ++k)
        if (!valid_line_text(in->code_lines[k])) return TS2_CHEAT_EXT_ERR_CODE;

    /* Each line on its own: exactly one supported code. */
    for (k = 0; k < in->code_line_count; ++k) {
        static psxcheat_program one;   /* main thread only; keeps 3 KiB off the stack */
        psxcheat_error err;
        uint32_t width;
        psxcheat_psx_range span;
        const char *line = in->code_lines[k];
        if (psxcheat_parse(line, strlen(line), &one, &err) != PSXCHEAT_OK || one.count != 1u)
            return TS2_CHEAT_EXT_ERR_PARSE;
        width = one.lines[0].op == PSXCHEAT_OP_WRITE8 ? 1u :
                one.lines[0].op == PSXCHEAT_OP_WRITE16 ? 2u : 0u;
        if (width == 0u) return TS2_CHEAT_EXT_ERR_PARSE;
        span.start = psxcheat_psx_guest_address(one.lines[0].offset);
        if (span.start == 0u) return TS2_CHEAT_EXT_ERR_TARGET;
        span.end = span.start + width;
        owner[count] = policy_index(policy, np, span);
        if (owner[count] < 0) {
            /* Safe mode: rejected at the first such line, as always. */
            if (mode != TS2_CHEAT_EXT_MODE_ADVANCED || !ts2_cheat_advanced_range_permitted(span))
                return TS2_CHEAT_EXT_ERR_TARGET;
            safe = 0;
        }
        if (!ts2_cheat_advanced_range_permitted(span)) advanced = 0;
        spans[count++] = span;
    }
    if (!safe) {
        /* Outside the external policy: only Advanced mode may accept it,
         * and only when every span lies inside the fixed Advanced region
         * (then one owner: spans merge within the region). */
        if (mode != TS2_CHEAT_EXT_MODE_ADVANCED || !advanced) return TS2_CHEAT_EXT_ERR_TARGET;
        for (k = 0; k < count; ++k) owner[k] = 0;
        g_scratch.trust[i] = TS2_CHEAT_TRUST_ADVANCED;
    }
    /* Sort by start (insertion sort, <= 32 entries), then merge spans that
     * touch or overlap within the same policy entry. */
    for (k = 1; k < count; ++k) {
        const psxcheat_psx_range s = spans[k];
        const long o = owner[k];
        j = k;
        while (j > 0 && spans[j - 1].start > s.start) {
            spans[j] = spans[j - 1];
            owner[j] = owner[j - 1];
            --j;
        }
        spans[j] = s;
        owner[j] = o;
    }
    for (k = 0; k < count; ++k) {
        if (merged > 0 && owner[k] == owner[k - 1] &&
            spans[k].start <= g_scratch.ranges[i][merged - 1].end) {
            if (spans[k].end > g_scratch.ranges[i][merged - 1].end)
                g_scratch.ranges[i][merged - 1].end = spans[k].end;
            continue;
        }
        g_scratch.ranges[i][merged++] = spans[k];
    }
    g_scratch.range_count[i] = merged;
    for (k = 0; k < merged; ++k)
        if (builtin_ranges_overlap(builtins, nb, g_scratch.ranges[i][k]))
            return TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN;
    return TS2_CHEAT_EXT_OK;
}

static int scratch_overlap(size_t a, size_t b)
{
    size_t x, y;
    for (x = 0; x < g_scratch.range_count[a]; ++x)
        for (y = 0; y < g_scratch.range_count[b]; ++y)
            if (overlaps(g_scratch.ranges[a][x], g_scratch.ranges[b][y])) return 1;
    return 0;
}

static void publish_builtins(const ts2_cheat_def *builtins, size_t nb,
                             const psxcheat_psx_range *verified, size_t nv)
{
    size_t i;
    memset(&g_comb, 0, sizeof g_comb);
    g_comb.verified = verified;
    g_comb.verified_count = nv;
    if (nb > TS2_CHEAT_MAX) nb = TS2_CHEAT_MAX;
    for (i = 0; i < nb && builtins; ++i) g_comb.table[i] = builtins[i];
    g_comb.count = builtins ? nb : 0u;
    g_comb.builtin_count = g_comb.count;
    g_comb.built = 1;
}

/* Copy accepted input i into store slot e and its table entry. */
static void store_external(const ts2_cheat_external_input *in, size_t i, size_t e)
{
    ext_store *s = &g_comb.ext[e];
    ts2_cheat_def *d = &g_comb.table[g_comb.count];
    size_t k, used = 0;

    memset(s, 0, sizeof *s);
    snprintf(s->id, sizeof s->id, "%s", in->id);
    snprintf(s->label, sizeof s->label, "%s", in->label);
    if (in->description) snprintf(s->desc, sizeof s->desc, "%s", in->description);
    snprintf(s->key, sizeof s->key, "%s%s", TS2_CHEAT_EXT_KEY_PREFIX, in->id);
    for (k = 0; k < in->code_line_count; ++k) {
        const int w = snprintf(s->code + used, sizeof s->code - used, "%s\n", in->code_lines[k]);
        if (w > 0) used += (size_t)w;
    }
    for (k = 0; k < g_scratch.range_count[i]; ++k) s->ranges[k] = g_scratch.ranges[i][k];
    s->range_count = g_scratch.range_count[i];
    s->trust = g_scratch.trust[i];
    /* Advanced (unverified) entries never start on (the engine enforces it
     * again on its own). */
    s->enabled = s->trust == TS2_CHEAT_TRUST_SAFE && in->enabled != 0;

    memset(d, 0, sizeof *d);
    d->id = s->id;
    d->label = s->label;
    d->code = s->code;
    d->ranges = s->ranges;
    d->range_count = s->range_count;
    d->menu_key = s->key;
    d->off_hint = s->desc[0] ? s->desc : NULL;
    d->active_hint = s->desc[0] ? s->desc : NULL;
    ++g_comb.count;
    ++g_comb.external_count;
}

static void copy_verdicts(ts2_cheat_ext_status *status, size_t looked)
{
    size_t i;
    if (status)
        for (i = 0; i < looked; ++i) status[i] = g_scratch.status[i];
}

static void copy_trust(ts2_cheat_trust *trust, size_t looked)
{
    size_t i;
    if (trust)
        for (i = 0; i < looked; ++i)
            trust[i] = g_scratch.status[i] == TS2_CHEAT_EXT_OK ? g_scratch.trust[i]
                                                               : TS2_CHEAT_TRUST_SAFE;
}

/* Tiers of the current combined table (built-ins SAFE). */
static void table_trust(ts2_cheat_trust *out)
{
    size_t i;
    for (i = 0; i < TS2_CHEAT_MAX; ++i) out[i] = TS2_CHEAT_TRUST_SAFE;
    for (i = g_comb.builtin_count; i < g_comb.count && i < TS2_CHEAT_MAX; ++i)
        out[i] = g_comb.ext[i - g_comb.builtin_count].trust;
}

ts2_cheat_build_status ts2_cheat_combined_build_with(
    const ts2_cheat_def *builtins, size_t nb,
    const psxcheat_psx_range *verified, size_t nv,
    const psxcheat_psx_range *policy, size_t np,
    const ts2_cheat_external_input *inputs, size_t n,
    ts2_cheat_ext_status *status)
{
    return ts2_cheat_combined_build_with_mode(builtins, nb, verified, nv, policy, np,
                                              TS2_CHEAT_EXT_MODE_SAFE, inputs, n, status, NULL);
}

ts2_cheat_build_status ts2_cheat_combined_build_with_mode(
    const ts2_cheat_def *builtins, size_t nb,
    const psxcheat_psx_range *verified, size_t nv,
    const psxcheat_psx_range *policy, size_t np,
    ts2_cheat_ext_mode mode,
    const ts2_cheat_external_input *inputs, size_t n,
    ts2_cheat_ext_status *status, ts2_cheat_trust *trust)
{
    ts2_cheat_def_status per[TS2_CHEAT_MAX];
    ts2_cheat_trust tiers[TS2_CHEAT_MAX];
    size_t i, j, room;
    const size_t looked = !inputs ? 0u :
        n < TS2_CHEAT_EXTERNAL_INPUT_MAX ? n : TS2_CHEAT_EXTERNAL_INPUT_MAX;
    ts2_cheat_ext_status *st = g_scratch.status;   /* the first `looked` verdicts */

    if (status)
        for (i = 0; i < n; ++i) status[i] = TS2_CHEAT_EXT_ERR_NOT_CHECKED;
    if (trust)
        for (i = 0; i < n; ++i) trust[i] = TS2_CHEAT_TRUST_SAFE;
    /* The engine or the menu may hold pointers into the table: refuse and
     * change nothing (not even the scratch verdicts). */
    if (g_life.sealed || g_life.refs) return TS2_CHEAT_BUILD_ERR_SEALED;
    for (i = 0; i < TS2_CHEAT_EXTERNAL_INPUT_MAX; ++i) st[i] = TS2_CHEAT_EXT_ERR_NOT_CHECKED;
    publish_builtins(builtins, nb, verified, nv);

    /* Built-ins: as trusted as before; a broken built-in table is the
     * engine's fatal case, and no external entry is looked at. */
    if (!builtins || nb == 0u || nb > TS2_CHEAT_MAX ||
        ts2_cheat_validate_table(builtins, nb, verified, nv, per) != TS2_CHEAT_TABLE_OK)
        return TS2_CHEAT_BUILD_ERR_BUILTINS;
    for (i = 0; i < nb; ++i) {
        char key[TS2_CHEAT_KEY_MAX + 1u];
        if (!ts2_cheat_menu_key(&builtins[i], key, sizeof key) ||
            strncmp(key, TS2_CHEAT_EXT_KEY_PREFIX, sizeof TS2_CHEAT_EXT_KEY_PREFIX - 1u) == 0)
            return TS2_CHEAT_BUILD_ERR_POLICY;   /* external key namespace is reserved */
    }
    if (!external_policy_valid(verified, nv, policy, np)) return TS2_CHEAT_BUILD_ERR_POLICY;
    if (mode != TS2_CHEAT_EXT_MODE_SAFE && mode != TS2_CHEAT_EXT_MODE_ADVANCED)
        return TS2_CHEAT_BUILD_ERR_POLICY;
    if (looked == 0u) return TS2_CHEAT_BUILD_OK;   /* zero external cheats */

    /* 1. Each entry on its own; entries beyond the input limit: capacity. */
    for (i = 0; i < looked; ++i)
        st[i] = check_input(&inputs[i], i, builtins, nb, policy, np, mode);
    if (status)
        for (i = looked; i < n; ++i) status[i] = TS2_CHEAT_EXT_ERR_CAPACITY;
    /* 2. Ids shared by several otherwise valid entries: reject all of them. */
    for (i = 0; i < looked; ++i) {
        int shared = 0;
        if (st[i] != TS2_CHEAT_EXT_OK && st[i] != TS2_CHEAT_EXT_ERR_ID_DUPLICATE)
            continue;
        for (j = 0; j < looked; ++j)
            if (j != i && (st[j] == TS2_CHEAT_EXT_OK ||
                           st[j] == TS2_CHEAT_EXT_ERR_ID_DUPLICATE) &&
                strcmp(inputs[i].id, inputs[j].id) == 0)
                shared = 1;
        if (shared) st[i] = TS2_CHEAT_EXT_ERR_ID_DUPLICATE;
    }
    /* 3. Overlapping write spans between remaining entries: reject both. */
    for (i = 0; i < looked; ++i) {
        if (st[i] != TS2_CHEAT_EXT_OK && st[i] != TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL)
            continue;
        for (j = i + 1u; j < looked; ++j) {
            if (st[j] != TS2_CHEAT_EXT_OK && st[j] != TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL)
                continue;
            if (scratch_overlap(i, j)) {
                st[i] = TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL;
                st[j] = TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL;
            }
        }
    }
    /* 4. Capacity, in input order; 5. publish. */
    room = TS2_CHEAT_MAX - nb;
    if (room > TS2_CHEAT_EXTERNAL_MAX) room = TS2_CHEAT_EXTERNAL_MAX;
    for (i = 0; i < looked; ++i) {
        if (st[i] != TS2_CHEAT_EXT_OK) continue;
        if (g_comb.external_count >= room) {
            st[i] = TS2_CHEAT_EXT_ERR_CAPACITY;
            continue;
        }
        store_external(&inputs[i], i, g_comb.external_count);
    }
    /* 6. Defence in depth: the combined table must pass the trusted table
     * validation entry by entry, each against its own tier; otherwise fall
     * back to the built-ins. */
    table_trust(tiers);
    if (ts2_cheat_validate_table_tiered(g_comb.table, g_comb.count, verified, nv, tiers, per) !=
        TS2_CHEAT_TABLE_OK) {
        for (i = 0; i < looked; ++i)
            if (st[i] == TS2_CHEAT_EXT_OK) st[i] = TS2_CHEAT_EXT_ERR_NOT_CHECKED;
        publish_builtins(builtins, nb, verified, nv);
        copy_verdicts(status, looked);
        return TS2_CHEAT_BUILD_ERR_INTERNAL;
    }
    for (i = 0; i < g_comb.count; ++i) {
        if (per[i] == TS2_CHEAT_DEF_OK) continue;
        for (j = 0; j < looked; ++j)
            if (st[j] == TS2_CHEAT_EXT_OK) st[j] = TS2_CHEAT_EXT_ERR_NOT_CHECKED;
        publish_builtins(builtins, nb, verified, nv);
        copy_verdicts(status, looked);
        return TS2_CHEAT_BUILD_ERR_INTERNAL;
    }
    copy_verdicts(status, looked);
    copy_trust(trust, looked);
    return TS2_CHEAT_BUILD_OK;
}

ts2_cheat_build_status ts2_cheat_combined_build(const ts2_cheat_external_input *inputs,
                                                size_t n, ts2_cheat_ext_status *status)
{
    size_t nb = 0, nv = 0, np = 0;
    const ts2_cheat_def *b = ts2_cheat_table(&nb);
    const psxcheat_psx_range *v = ts2_cheat_writable_ranges(&nv);
    const psxcheat_psx_range *p = ts2_cheat_external_policy(&np);
    return ts2_cheat_combined_build_with(b, nb, v, nv, p, np, inputs, n, status);
}

ts2_cheat_build_status ts2_cheat_combined_build_mode(const ts2_cheat_external_input *inputs,
                                                     size_t n, ts2_cheat_ext_mode mode,
                                                     ts2_cheat_ext_status *status,
                                                     ts2_cheat_trust *trust)
{
    size_t nb = 0, nv = 0, np = 0;
    const ts2_cheat_def *b = ts2_cheat_table(&nb);
    const psxcheat_psx_range *v = ts2_cheat_writable_ranges(&nv);
    const psxcheat_psx_range *p = ts2_cheat_external_policy(&np);
    return ts2_cheat_combined_build_with_mode(b, nb, v, nv, p, np, mode, inputs, n, status,
                                              trust);
}

ts2_cheat_trust ts2_cheat_active_trust(size_t index)
{
    if (!g_comb.built || index < g_comb.builtin_count || index >= g_comb.count)
        return TS2_CHEAT_TRUST_SAFE;
    return g_comb.ext[index - g_comb.builtin_count].trust;
}

const psxcheat_psx_range *ts2_cheat_active_verified(size_t *count)
{
    if (!g_comb.built) return ts2_cheat_writable_ranges(count);
    if (count) *count = g_comb.verified_count;
    return g_comb.verified;
}

const ts2_cheat_def *ts2_cheat_active_table(size_t *count)
{
    if (!g_comb.built) return ts2_cheat_table(count);
    if (count) *count = g_comb.count;
    return g_comb.table;
}

size_t ts2_cheat_active_builtin_count(void)
{
    size_t n = 0;
    if (!g_comb.built) {
        (void)ts2_cheat_table(&n);
        return n;
    }
    return g_comb.builtin_count;
}

size_t ts2_cheat_active_external_count(void) { return g_comb.built ? g_comb.external_count : 0u; }

int ts2_cheat_active_external_enabled(size_t index)
{
    if (!g_comb.built || index < g_comb.builtin_count || index >= g_comb.count) return 0;
    return g_comb.ext[index - g_comb.builtin_count].enabled;
}

int ts2_cheat_def_is_external(const ts2_cheat_def *d)
{
    return g_comb.built && d && d >= &g_comb.table[g_comb.builtin_count] &&
           d < &g_comb.table[g_comb.count];
}

int ts2_cheat_session_begin(void)
{
    if (g_life.refs) return 0;
    memset(&g_comb, 0, sizeof g_comb);   /* back to built-ins only */
    g_life.sealed = 0;
    return 1;
}

void ts2_cheat_session_seal(void) { g_life.sealed = 1; }
int ts2_cheat_session_sealed(void) { return g_life.sealed; }

void ts2_cheat_table_ref_acquire(void) { ++g_life.refs; }

void ts2_cheat_table_ref_release(void)
{
    if (g_life.refs) --g_life.refs;
}

unsigned ts2_cheat_table_refs(void) { return g_life.refs; }

void ts2_cheat_combined_reset(void)
{
    memset(&g_comb, 0, sizeof g_comb);
    memset(&g_life, 0, sizeof g_life);
}

const char *ts2_cheat_ext_status_name(ts2_cheat_ext_status status)
{
    switch (status) {
    case TS2_CHEAT_EXT_OK: return "ok";
    case TS2_CHEAT_EXT_ERR_NOT_CHECKED: return "not checked";
    case TS2_CHEAT_EXT_ERR_ID: return "invalid id";
    case TS2_CHEAT_EXT_ERR_ID_BUILTIN: return "id of a built-in cheat";
    case TS2_CHEAT_EXT_ERR_ID_DUPLICATE: return "duplicate id";
    case TS2_CHEAT_EXT_ERR_LABEL: return "invalid label";
    case TS2_CHEAT_EXT_ERR_DESCRIPTION: return "invalid description";
    case TS2_CHEAT_EXT_ERR_CODE: return "invalid code lines";
    case TS2_CHEAT_EXT_ERR_PARSE: return "unsupported or invalid code";
    case TS2_CHEAT_EXT_ERR_TARGET: return "address not permitted";
    case TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN: return "overlaps a built-in cheat";
    case TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL: return "overlaps another external cheat";
    case TS2_CHEAT_EXT_ERR_CAPACITY: return "no free cheat slot";
    }
    return "unknown";
}
