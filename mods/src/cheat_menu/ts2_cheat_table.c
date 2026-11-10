/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "ts2_cheat_table.h"

#include <stdio.h>
#include <string.h>

#include "ts2_cheat_engine.h"   /* TS2_CHEAT_MAX_COINS_CODE / _OPTION */
#include "ts2_cheat_memory.h"   /* policy: D1/D2 rules, explicit list */

/* ---- Production table ------------------------------------------------------
 * Every entry's ranges must lie inside the verified explicit list in
 * ts2_cheat_memory.c (which carries the provenance). No entry is added
 * without its own verification phase. */

static const psxcheat_psx_range k_max_coins_ranges[] = {
    { TS2_CHEAT_COINS_ADDR, TS2_CHEAT_COINS_ADDR + 2u },   /* int16 coin counter */
};

static const psxcheat_psx_range k_infinite_lives_ranges[] = {
    { TS2_CHEAT_LIVES_ADDR, TS2_CHEAT_LIVES_ADDR + 2u },   /* int16 lives counter */
};

static const ts2_cheat_def k_table[] = {
    /* Index 0: the legacy single-cheat API (ts2_cheat_engine_enabled, ...)
     * addresses this entry. Its id is the manifest option and the key
     * persisted in mods/state.toml: never rename. */
    { TS2_CHEAT_MAX_COINS_OPTION, "Max Coins", TS2_CHEAT_MAX_COINS_CODE,
      k_max_coins_ranges, sizeof k_max_coins_ranges / sizeof k_max_coins_ranges[0],
      "cheat_menu.max_coins",                    /* Phase 2C key: never rename */
      "code 800B2226 0063 holds coins at 99",
      "coins held at 99" },
    /* Index 1 (Phase 2E-D2): 9 is the game's own maximum (pickup cap). Its
     * id is the manifest option and the persisted key: never rename. */
    { "infinite-lives", "Infinite Lives", "800B2222 0009",
      k_infinite_lives_ranges,
      sizeof k_infinite_lives_ranges / sizeof k_infinite_lives_ranges[0],
      "cheat_menu.infinite_lives",
      "code 800B2222 0009 holds lives at 9",
      "lives held at 9" },
};

const ts2_cheat_def *ts2_cheat_table(size_t *count)
{
    if (count) *count = sizeof k_table / sizeof k_table[0];
    return k_table;
}

/* ---- Validation ------------------------------------------------------------ */

static int overlaps(psxcheat_psx_range a, psxcheat_psx_range b)
{
    return a.start < b.end && b.start < a.end;
}

static int inside(psxcheat_psx_range inner, psxcheat_psx_range outer)
{
    return inner.start >= outer.start && inner.end <= outer.end;
}

/* 1 when the string is non-NULL, non-empty and at most `max` bytes. */
static int bounded_text(const char *s, size_t max)
{
    size_t k;
    if (!s || !s[0]) return 0;
    for (k = 0; k <= max; ++k)
        if (s[k] == '\0') return 1;
    return 0;
}

static int key_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-';
}

int ts2_cheat_menu_key(const ts2_cheat_def *d, char *out, size_t n)
{
    static const char prefix[] = TS2_CHEAT_KEY_PREFIX;
    const size_t plen = sizeof prefix - 1u;
    size_t k, len;
    int written;

    if (!out || n == 0u) return 0;
    out[0] = '\0';
    if (!d) return 0;
    if (d->menu_key) {
        if (!bounded_text(d->menu_key, TS2_CHEAT_KEY_MAX)) return 0;
        written = snprintf(out, n, "%s", d->menu_key);
    } else {
        if (!bounded_text(d->id, TS2_CHEAT_ID_MAX)) return 0;
        written = snprintf(out, n, "%s%s", prefix, d->id);
    }
    /* Truncated (buffer or key too long) counts as invalid. */
    if (written < 0 || (size_t)written >= n || (size_t)written > TS2_CHEAT_KEY_MAX) {
        out[0] = '\0';
        return 0;
    }
    len = (size_t)written;
    if (len <= plen || strncmp(out, prefix, plen) != 0 ||
        strcmp(out, TS2_CHEAT_KEY_RESUME) == 0) {
        out[0] = '\0';
        return 0;
    }
    for (k = 0; k < len; ++k)
        if (!key_char(out[k])) {
            out[0] = '\0';
            return 0;
        }
    return 1;
}

ts2_cheat_table_status ts2_cheat_validate_menu_keys(const ts2_cheat_def *defs, size_t n)
{
    char keys[TS2_CHEAT_MAX][TS2_CHEAT_KEY_MAX + 1u];
    size_t i, j;
    if (!defs) return TS2_CHEAT_TABLE_ERR_NULL;
    if (n == 0u) return TS2_CHEAT_TABLE_ERR_EMPTY;
    if (n > TS2_CHEAT_MAX) return TS2_CHEAT_TABLE_ERR_TOO_MANY;
    for (i = 0; i < n; ++i) {
        if (!ts2_cheat_menu_key(&defs[i], keys[i], sizeof keys[i]))
            return TS2_CHEAT_TABLE_ERR_MENU_KEY;
        for (j = 0; j < i; ++j)
            if (strcmp(keys[i], keys[j]) == 0) return TS2_CHEAT_TABLE_ERR_MENU_KEY;
    }
    return TS2_CHEAT_TABLE_OK;
}

static int policy_valid(const psxcheat_psx_range *policy, size_t n)
{
    size_t i, j;
    if (!policy || n == 0u) return 0;
    for (i = 0; i < n; ++i) {
        if (!ts2_cheat_range_permitted(policy[i])) return 0;
        for (j = 0; j < i; ++j)
            if (overlaps(policy[i], policy[j])) return 0;
    }
    return 1;
}

/* The entry's range array is structurally usable (for overlap checks). */
static int ranges_shaped(const ts2_cheat_def *d)
{
    size_t k;
    if (!d->ranges || d->range_count == 0u || d->range_count > PSXCHEAT_PSX_MAX_RANGES)
        return 0;
    for (k = 0; k < d->range_count; ++k)
        if (d->ranges[k].start >= d->ranges[k].end) return 0;
    return 1;
}

int ts2_cheat_program_targets_valid(const psxcheat_program *program,
                                    const psxcheat_psx_adapter *adapter)
{
    uint32_t k;
    if (!program || !adapter || program->count == 0u ||
        program->count > PSXCHEAT_MAX_CODES)
        return 0;
    for (k = 0; k < program->count; ++k) {
        const psxcheat_line *l = &program->lines[k];
        const uint32_t width = l->op == PSXCHEAT_OP_WRITE8 ? 1u :
                               l->op == PSXCHEAT_OP_WRITE16 ? 2u : 0u;
        if (width == 0u) return 0;
        if (l->offset > PSXCHEAT_RAM_SIZE - width) return 0;
        if (width == 2u && (l->offset & 1u) != 0u) return 0;
        if (!psxcheat_psx_span_writable(adapter, l->offset, width)) return 0;
    }
    return 1;
}

static ts2_cheat_def_status check_def(const ts2_cheat_def *d,
                                      const psxcheat_psx_range *policy, size_t policy_n)
{
    /* Main thread only (activation); static to keep ~3 KiB off the stack. */
    static psxcheat_program program;
    static psxcheat_psx_adapter adapter;
    psxcheat_psx_config cfg;
    psxcheat_error err;
    size_t k, j;

    if (!bounded_text(d->label, 64u)) return TS2_CHEAT_DEF_ERR_LABEL;
    if ((d->off_hint && !bounded_text(d->off_hint, TS2_CHEAT_HINT_MAX)) ||
        (d->active_hint && !bounded_text(d->active_hint, TS2_CHEAT_HINT_MAX)))
        return TS2_CHEAT_DEF_ERR_LABEL;
    if (!d->code || !d->code[0]) return TS2_CHEAT_DEF_ERR_CODE;
    if (!ranges_shaped(d)) return TS2_CHEAT_DEF_ERR_RANGES;
    for (k = 0; k < d->range_count; ++k) {
        int covered = 0;
        if (!ts2_cheat_range_permitted(d->ranges[k])) return TS2_CHEAT_DEF_ERR_POLICY;
        for (j = 0; j < policy_n && !covered; ++j)
            covered = inside(d->ranges[k], policy[j]);
        if (!covered) return TS2_CHEAT_DEF_ERR_POLICY;
        for (j = 0; j < k; ++j)
            if (overlaps(d->ranges[k], d->ranges[j])) return TS2_CHEAT_DEF_ERR_OVERLAP;
    }
    memset(&program, 0, sizeof program);
    if (psxcheat_parse(d->code, strlen(d->code), &program, &err) != PSXCHEAT_OK ||
        program.count == 0u)
        return TS2_CHEAT_DEF_ERR_PARSE;
    cfg.writable = d->ranges;
    cfg.writable_count = d->range_count;
    cfg.verify_readback = 1;
    if (psxcheat_psx_init(&adapter, &cfg) != PSXCHEAT_PSX_OK) return TS2_CHEAT_DEF_ERR_RANGES;
    if (!ts2_cheat_program_targets_valid(&program, &adapter)) return TS2_CHEAT_DEF_ERR_TARGET;
    return TS2_CHEAT_DEF_OK;
}

ts2_cheat_table_status ts2_cheat_validate_table(const ts2_cheat_def *defs, size_t n,
                                                const psxcheat_psx_range *policy,
                                                size_t policy_n,
                                                ts2_cheat_def_status *per_def)
{
    size_t i, j, a, b;
    ts2_cheat_table_status st = TS2_CHEAT_TABLE_OK;

    if (per_def && n <= TS2_CHEAT_MAX)
        for (i = 0; i < n; ++i) per_def[i] = TS2_CHEAT_DEF_ERR_NOT_CHECKED;

    if (!defs) return TS2_CHEAT_TABLE_ERR_NULL;
    if (n == 0u) return TS2_CHEAT_TABLE_ERR_EMPTY;
    if (n > TS2_CHEAT_MAX) return TS2_CHEAT_TABLE_ERR_TOO_MANY;
    if (!policy_valid(policy, policy_n)) return TS2_CHEAT_TABLE_ERR_POLICY;
    for (i = 0; i < n && st == TS2_CHEAT_TABLE_OK; ++i) {
        if (!bounded_text(defs[i].id, TS2_CHEAT_ID_MAX)) st = TS2_CHEAT_TABLE_ERR_ID;
        for (j = 0; j < i && st == TS2_CHEAT_TABLE_OK; ++j)
            if (strcmp(defs[i].id, defs[j].id) == 0) st = TS2_CHEAT_TABLE_ERR_DUPLICATE_ID;
    }
    if (st != TS2_CHEAT_TABLE_OK) return st;
    st = ts2_cheat_validate_menu_keys(defs, n);
    if (st != TS2_CHEAT_TABLE_OK) return st;
    if (!per_def) return TS2_CHEAT_TABLE_OK;

    for (i = 0; i < n; ++i) per_def[i] = check_def(&defs[i], policy, policy_n);

    /* Ranges shared between two cheats: isolate both, whatever else is
     * wrong with either (an invalid entry's ranges are not trusted to make
     * the other one safe). */
    for (i = 0; i < n; ++i) {
        if (!ranges_shaped(&defs[i])) continue;
        for (j = 0; j < i; ++j) {
            int hit = 0;
            if (!ranges_shaped(&defs[j])) continue;
            for (a = 0; a < defs[i].range_count && !hit; ++a)
                for (b = 0; b < defs[j].range_count && !hit; ++b)
                    hit = overlaps(defs[i].ranges[a], defs[j].ranges[b]);
            if (!hit) continue;
            if (per_def[i] == TS2_CHEAT_DEF_OK) per_def[i] = TS2_CHEAT_DEF_ERR_OVERLAP;
            if (per_def[j] == TS2_CHEAT_DEF_OK) per_def[j] = TS2_CHEAT_DEF_ERR_OVERLAP;
        }
    }
    return TS2_CHEAT_TABLE_OK;
}

const char *ts2_cheat_table_status_name(ts2_cheat_table_status status)
{
    switch (status) {
    case TS2_CHEAT_TABLE_OK: return "ok";
    case TS2_CHEAT_TABLE_ERR_NULL: return "no table";
    case TS2_CHEAT_TABLE_ERR_EMPTY: return "empty table";
    case TS2_CHEAT_TABLE_ERR_TOO_MANY: return "too many cheats";
    case TS2_CHEAT_TABLE_ERR_ID: return "invalid id";
    case TS2_CHEAT_TABLE_ERR_DUPLICATE_ID: return "duplicate id";
    case TS2_CHEAT_TABLE_ERR_POLICY: return "invalid RAM policy";
    case TS2_CHEAT_TABLE_ERR_MENU_KEY: return "invalid or duplicate menu key";
    }
    return "unknown";
}

const char *ts2_cheat_def_status_name(ts2_cheat_def_status status)
{
    switch (status) {
    case TS2_CHEAT_DEF_OK: return "ok";
    case TS2_CHEAT_DEF_ERR_NOT_CHECKED: return "not checked";
    case TS2_CHEAT_DEF_ERR_LABEL: return "invalid label";
    case TS2_CHEAT_DEF_ERR_CODE: return "no code";
    case TS2_CHEAT_DEF_ERR_PARSE: return "code does not parse";
    case TS2_CHEAT_DEF_ERR_RANGES: return "invalid ranges";
    case TS2_CHEAT_DEF_ERR_POLICY: return "range outside RAM policy";
    case TS2_CHEAT_DEF_ERR_OVERLAP: return "overlapping ranges";
    case TS2_CHEAT_DEF_ERR_TARGET: return "write outside own ranges";
    }
    return "unknown";
}
