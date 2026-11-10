/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 2E-B: the TS2 cheat table, its validation, and independent engine
 * slots. Synthetic cheat definitions and synthetic policy lists (inside the
 * real D1/D2 rules) exercise multi-line programs and isolation; the
 * production table is checked as it is. Real engine, adapter, TS2 policy
 * rules and Action Replay core; psxrecomp services are stubs. No guest
 * memory. */
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
#include "ts2_cheat_engine.h"
#include "ts2_cheat_memory.h"
#include "ts2_cheat_table.h"

#include <stdio.h>
#include <string.h>

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

#define N_OF(a) (sizeof (a) / sizeof (a)[0])
#define OFF(addr) ((addr) - 0x80000000u)

/* ---- Synthetic policy: D1 spans (any size allowed there) plus the real
 * narrow D2 coin span. ---------------------------------------------------- */
static const psxcheat_psx_range k_policy[] = {
    { 0x800A0500u, 0x800A0510u },
    { 0x800A0600u, 0x800A0604u },
    { 0x800A0700u, 0x800A0708u },
    { 0x800B2226u, 0x800B2228u },
};

static const psxcheat_psx_range r_a[] = { { 0x800A0500u, 0x800A0502u } };
static const psxcheat_psx_range r_b[] = { { 0x800A0600u, 0x800A0604u } };
static const psxcheat_psx_range r_c[] = { { 0x800A0700u, 0x800A0702u },
                                          { 0x800A0704u, 0x800A0706u } };

/* A: one 16-bit write. B: three lines, mixed widths. C: two lines in two
 * separate ranges. */
static const ts2_cheat_def k_abc[] = {
    { "a", "Cheat A", "800A0500 1234", r_a, 1 },
    { "b", "Cheat B", "300A0600 0011\n300A0601 0022\n800A0602 3344", r_b, 1 },
    { "c", "Cheat C", "800A0700 0001\r\n800A0704 0002\n", r_c, 2 },
};

static ts2_cheat_def_status g_st[TS2_CHEAT_MAX];

static ts2_cheat_table_status validate(const ts2_cheat_def *defs, size_t n)
{
    return ts2_cheat_validate_table(defs, n, k_policy, N_OF(k_policy), g_st);
}

static ts2_cheat_table_status init(const ts2_cheat_def *defs, size_t n, const int *enabled)
{
    return ts2_cheat_engine_init_table(defs, n, k_policy, N_OF(k_policy), enabled);
}

/* ------------------------------------------------------------------------- */

static void test_production_table(void)
{
    size_t n = 0, pn = 0;
    const ts2_cheat_def *t = ts2_cheat_table(&n);
    const psxcheat_psx_range *p = ts2_cheat_writable_ranges(&pn);
    g_test = "production table";
    stub_reset();
    CHECK(t != NULL && n == 1u);   /* no new production cheats */
    CHECK(strcmp(t[0].id, "max-coins") == 0 && strcmp(t[0].id, TS2_CHEAT_MAX_COINS_OPTION) == 0);
    CHECK(strcmp(t[0].label, "Max Coins") == 0);
    CHECK(strcmp(t[0].code, "800B2226 0063") == 0);
    CHECK(t[0].range_count == 1u && t[0].ranges[0].start == 0x800B2226u &&
          t[0].ranges[0].end == 0x800B2228u);
    CHECK(ts2_cheat_validate_table(t, n, p, pn, g_st) == TS2_CHEAT_TABLE_OK);
    CHECK(g_st[0] == TS2_CHEAT_DEF_OK);
    CHECK(ts2_cheat_engine_init(0) == 1);
    CHECK(ts2_cheat_engine_count() == 1u && ts2_cheat_engine_def(0) == &t[0]);
    CHECK(ts2_cheat_engine_table_status() == TS2_CHEAT_TABLE_OK);
    CHECK(ts2_cheat_engine_setup_status(0) == TS2_CHEAT_DEF_OK);
    CHECK(ts2_cheat_engine_state() == TS2_CHEAT_OFF);
    CHECK(g_stub.write_calls == 0 && g_stub.read_calls == 0 && g_stub.started_calls == 0);
}

static void test_fatal_table_errors(void)
{
    ts2_cheat_def many[TS2_CHEAT_MAX + 1u];
    ts2_cheat_def bad[2];
    static const psxcheat_psx_range p_overlap[] = { { 0x800A0500u, 0x800A0510u },
                                                    { 0x800A050Eu, 0x800A0520u } };
    static const psxcheat_psx_range p_kernel[] = { { 0x80000100u, 0x80000104u } };
    static const psxcheat_psx_range p_wide_d2[] = { { 0x800B2224u, 0x800B222Au } };  /* 6 bytes */
    static const psxcheat_psx_range p_code[] = { { 0x80010000u, 0x80010004u } };
    size_t i;
    g_test = "fatal table";
    stub_reset();

    CHECK(validate(NULL, 1) == TS2_CHEAT_TABLE_ERR_NULL);
    CHECK(validate(k_abc, 0) == TS2_CHEAT_TABLE_ERR_EMPTY);
    for (i = 0; i < N_OF(many); ++i) many[i] = k_abc[0];
    CHECK(validate(many, N_OF(many)) == TS2_CHEAT_TABLE_ERR_TOO_MANY);

    memcpy(bad, k_abc, sizeof bad);
    bad[1].id = NULL;
    CHECK(validate(bad, 2) == TS2_CHEAT_TABLE_ERR_ID);
    CHECK(g_st[0] == TS2_CHEAT_DEF_ERR_NOT_CHECKED && g_st[1] == TS2_CHEAT_DEF_ERR_NOT_CHECKED);
    bad[1].id = "";
    CHECK(validate(bad, 2) == TS2_CHEAT_TABLE_ERR_ID);
    bad[1].id = "an-identifier-that-is-33-bytes-xx";       /* 33 bytes */
    CHECK(strlen(bad[1].id) == TS2_CHEAT_ID_MAX + 1u);
    CHECK(validate(bad, 2) == TS2_CHEAT_TABLE_ERR_ID);
    bad[1].id = "an-identifier-that-is-32-bytes-x";        /* exactly the limit */
    CHECK(validate(bad, 2) == TS2_CHEAT_TABLE_OK);
    bad[1].id = "a";
    CHECK(validate(bad, 2) == TS2_CHEAT_TABLE_ERR_DUPLICATE_ID);

    CHECK(ts2_cheat_validate_table(k_abc, 3, NULL, 0, g_st) == TS2_CHEAT_TABLE_ERR_POLICY);
    CHECK(ts2_cheat_validate_table(k_abc, 3, k_policy, 0, g_st) == TS2_CHEAT_TABLE_ERR_POLICY);
    CHECK(ts2_cheat_validate_table(k_abc, 3, p_overlap, 2, g_st) == TS2_CHEAT_TABLE_ERR_POLICY);
    CHECK(ts2_cheat_validate_table(k_abc, 3, p_kernel, 1, g_st) == TS2_CHEAT_TABLE_ERR_POLICY);
    CHECK(ts2_cheat_validate_table(k_abc, 3, p_wide_d2, 1, g_st) == TS2_CHEAT_TABLE_ERR_POLICY);
    CHECK(ts2_cheat_validate_table(k_abc, 3, p_code, 1, g_st) == TS2_CHEAT_TABLE_ERR_POLICY);
    CHECK(ts2_cheat_validate_table(k_abc, 3, k_policy, N_OF(k_policy), NULL) == TS2_CHEAT_TABLE_OK);

    /* A fatal table sets up no slot at all: nothing can run. */
    {
        static const int all_on[3] = { 1, 1, 1 };
        bad[1].id = "a";
        CHECK(init(bad, 2, all_on) == TS2_CHEAT_TABLE_ERR_DUPLICATE_ID);
        CHECK(ts2_cheat_engine_table_status() == TS2_CHEAT_TABLE_ERR_DUPLICATE_ID);
        CHECK(ts2_cheat_engine_count() == 0u);
        CHECK(ts2_cheat_engine_state() == TS2_CHEAT_UNAVAILABLE);
        CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_UNAVAILABLE);
        CHECK(ts2_cheat_engine_set_enabled(1) == 0);
        ts2_cheat_engine_vblank();
        ts2_cheat_engine_savestate();
        CHECK(g_stub.write_calls == 0 && g_stub.started_calls == 0);
        CHECK(ts2_cheat_engine_init_table(k_abc, 3, p_kernel, 1, all_on) ==
              TS2_CHEAT_TABLE_ERR_POLICY);
        CHECK(ts2_cheat_engine_count() == 0u);
        ts2_cheat_engine_vblank();
        CHECK(g_stub.write_calls == 0);
    }
}

static void test_per_cheat_errors(void)
{
    static const psxcheat_psx_range r_kernel[] = { { 0x80000100u, 0x80000102u } };
    static const psxcheat_psx_range r_d1_unlisted[] = { { 0x800A0800u, 0x800A0802u } };
    static const psxcheat_psx_range r_own_overlap[] = { { 0x800A0500u, 0x800A0504u },
                                                        { 0x800A0502u, 0x800A0506u } };
    static const psxcheat_psx_range r_inverted[] = { { 0x800A0504u, 0x800A0500u } };
    static const psxcheat_psx_range r_tail[] = { { 0x800A0500u, 0x800A0503u } };
    static const psxcheat_psx_range r_d2_too_wide[] = { { 0x800B2226u, 0x800B222Cu } };
    psxcheat_psx_range r_too_many[PSXCHEAT_PSX_MAX_RANGES + 1u];
    ts2_cheat_def d[2];
    size_t i;
    g_test = "per-cheat errors";

    for (i = 0; i < N_OF(r_too_many); ++i) {
        r_too_many[i].start = 0x800A0500u + (uint32_t)i * 2u;
        r_too_many[i].end = r_too_many[i].start + 1u;
    }
#define ONE(field_stmt, expect)                                   \
    do {                                                          \
        memcpy(d, k_abc, sizeof d);                               \
        field_stmt;                                               \
        CHECK(validate(d, 2) == TS2_CHEAT_TABLE_OK);              \
        CHECK(g_st[0] == (expect));                               \
        CHECK(g_st[1] == TS2_CHEAT_DEF_OK);                       \
    } while (0)

    ONE(d[0].label = NULL, TS2_CHEAT_DEF_ERR_LABEL);
    ONE(d[0].label = "", TS2_CHEAT_DEF_ERR_LABEL);
    ONE(d[0].code = NULL, TS2_CHEAT_DEF_ERR_CODE);
    ONE(d[0].code = "", TS2_CHEAT_DEF_ERR_CODE);
    ONE(d[0].code = "800A0500", TS2_CHEAT_DEF_ERR_PARSE);
    ONE(d[0].code = "800A0500 12345", TS2_CHEAT_DEF_ERR_PARSE);
    ONE(d[0].code = "300A0500 0100", TS2_CHEAT_DEF_ERR_PARSE);      /* 8-bit value too wide */
    ONE(d[0].code = "D00A0500 0001", TS2_CHEAT_DEF_ERR_PARSE);      /* unsupported type */
    ONE(d[0].code = "800A0501 0001", TS2_CHEAT_DEF_ERR_PARSE);      /* odd 16-bit offset */
    ONE(d[0].code = "\n\n", TS2_CHEAT_DEF_ERR_PARSE);               /* no code line */
    ONE(d[0].code = "800A0500 0001\nXYZ", TS2_CHEAT_DEF_ERR_PARSE); /* one bad line */
    ONE(d[0].ranges = NULL, TS2_CHEAT_DEF_ERR_RANGES);
    ONE(d[0].range_count = 0, TS2_CHEAT_DEF_ERR_RANGES);
    ONE((d[0].ranges = r_inverted, d[0].range_count = 1), TS2_CHEAT_DEF_ERR_RANGES);
    ONE((d[0].ranges = r_too_many, d[0].range_count = N_OF(r_too_many)), TS2_CHEAT_DEF_ERR_RANGES);
    ONE((d[0].ranges = r_kernel, d[0].range_count = 1), TS2_CHEAT_DEF_ERR_POLICY);
    ONE((d[0].ranges = r_d1_unlisted, d[0].range_count = 1), TS2_CHEAT_DEF_ERR_POLICY);
    ONE((d[0].ranges = r_d2_too_wide, d[0].range_count = 1), TS2_CHEAT_DEF_ERR_POLICY);
    ONE((d[0].ranges = r_own_overlap, d[0].range_count = 2), TS2_CHEAT_DEF_ERR_OVERLAP);
    /* Write targets: outside own ranges (still inside the policy), and a
     * 16-bit write whose span runs past the end of the range. */
    ONE(d[0].code = "800A0508 0001", TS2_CHEAT_DEF_ERR_TARGET);
    ONE(d[0].code = "800A0500 0001\n300A0502 0001", TS2_CHEAT_DEF_ERR_TARGET);
    ONE((d[0].ranges = r_tail, d[0].range_count = 1, d[0].code = "800A0502 0001"),
        TS2_CHEAT_DEF_ERR_TARGET);
    /* Cheat A writing into cheat B's variable: rejected at setup. */
    ONE(d[0].code = "800A0600 0001", TS2_CHEAT_DEF_ERR_TARGET);
#undef ONE
}

static void test_cross_cheat_overlap(void)
{
    static const psxcheat_psx_range r_shares_a[] = { { 0x800A0501u, 0x800A0503u } };
    ts2_cheat_def d[3];
    g_test = "cross overlap";
    memcpy(d, k_abc, sizeof d);
    d[2].ranges = r_shares_a;      /* C now overlaps A */
    d[2].range_count = 1;
    d[2].code = "300A0502 0001";
    CHECK(validate(d, 3) == TS2_CHEAT_TABLE_OK);
    CHECK(g_st[0] == TS2_CHEAT_DEF_ERR_OVERLAP);   /* both isolated */
    CHECK(g_st[1] == TS2_CHEAT_DEF_OK);            /* B unaffected */
    CHECK(g_st[2] == TS2_CHEAT_DEF_ERR_OVERLAP);
    /* An entry that is invalid for another reason still isolates its peer. */
    d[2].code = "garbage";
    CHECK(validate(d, 3) == TS2_CHEAT_TABLE_OK);
    CHECK(g_st[0] == TS2_CHEAT_DEF_ERR_OVERLAP && g_st[2] == TS2_CHEAT_DEF_ERR_PARSE);
}

static void test_invalid_cheat_isolated_in_engine(void)
{
    static const int all_on[3] = { 1, 1, 1 };
    ts2_cheat_def d[3];
    g_test = "isolated setup";
    stub_reset();
    memcpy(d, k_abc, sizeof d);
    d[1].code = "800A0600 0001\nnot a code";   /* B invalid */
    CHECK(init(d, 3, all_on) == TS2_CHEAT_TABLE_OK);
    CHECK(ts2_cheat_engine_count() == 3u);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_UNAVAILABLE);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_setup_status(1) == TS2_CHEAT_DEF_ERR_PARSE);
    CHECK(ts2_cheat_engine_stats_at(1).last_status == PSXCHEAT_ERR_SYNTAX);
    CHECK(ts2_cheat_engine_ready_at(1) == 0 && ts2_cheat_engine_enabled_at(1) == 0);
    CHECK(ts2_cheat_engine_set_enabled_at(1, 1) == 0);   /* cannot be forced on */
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 3);                      /* A: 1, C: 2 */
    CHECK(stub_u8(OFF(0x800A0600u)) == 0);               /* B never wrote */
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ACTIVE);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ACTIVE);
    CHECK(ts2_cheat_engine_stats_at(1).vblanks == 1 && ts2_cheat_engine_stats_at(1).applied == 0);
}

static void test_multi_line(void)
{
    static const int b_on[3] = { 0, 1, 0 };
    uint32_t k;
    g_test = "multi-line";
    stub_reset();
    CHECK(init(k_abc, 3, b_on) == TS2_CHEAT_TABLE_OK);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_OFF);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 3);
    CHECK(g_stub.log[0].address == 0x800A0600u && g_stub.log[0].width == 1 && g_stub.log[0].value == 0x11);
    CHECK(g_stub.log[1].address == 0x800A0601u && g_stub.log[1].width == 1 && g_stub.log[1].value == 0x22);
    CHECK(g_stub.log[2].address == 0x800A0602u && g_stub.log[2].width == 2 && g_stub.log[2].value == 0x3344);
    CHECK(stub_u8(OFF(0x800A0600u)) == 0x11 && stub_u8(OFF(0x800A0601u)) == 0x22);
    CHECK(stub_u16(OFF(0x800A0602u)) == 0x3344);
    CHECK(stub_u8(OFF(0x800A05FFu)) == 0 && stub_u8(OFF(0x800A0604u)) == 0);  /* neighbours */
    CHECK(ts2_cheat_engine_stats_at(1).applied == 1);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ACTIVE);

    /* C: two lines in two separate ranges, CRLF and a trailing newline. */
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 3u + 3u + 2u);
    CHECK(stub_u16(OFF(0x800A0700u)) == 1 && stub_u16(OFF(0x800A0704u)) == 2);
    CHECK(stub_u16(OFF(0x800A0702u)) == 0);              /* the gap untouched */
    for (k = 0; k < g_stub.log_count && k < STUB_LOG_MAX; ++k)
        CHECK(g_stub.log[k].address != 0x800A0500u);     /* A stayed off */
    CHECK(g_stub.bad_addresses == 0);
}

static void test_runtime_isolation(void)
{
    static const int all_on[3] = { 1, 1, 1 };
    uint32_t writes;
    g_test = "runtime isolation";
    stub_reset();
    CHECK(init(k_abc, 3, all_on) == TS2_CHEAT_TABLE_OK);

    /* B's second line is dropped by the runtime: read-back fails. */
    g_stub.drop_from = OFF(0x800A0601u);
    g_stub.drop_to = OFF(0x800A0602u);
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ERROR);
    CHECK(ts2_cheat_engine_stats_at(1).failures == 1);
    CHECK(ts2_cheat_engine_stats_at(1).last_status == PSXCHEAT_ERR_MEMORY_WRITE);
    /* No rollback: line 1 stays written, line 3 never ran. */
    CHECK(stub_u8(OFF(0x800A0600u)) == 0x11);
    CHECK(stub_u16(OFF(0x800A0602u)) == 0);
    /* A and C are unaffected, in the same VBlank. */
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ACTIVE);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ACTIVE);
    CHECK(ts2_cheat_engine_stats_at(0).failures == 0 && ts2_cheat_engine_stats_at(2).failures == 0);
    CHECK(stub_u16(OFF(0x800A0500u)) == 0x1234 && stub_u16(OFF(0x800A0704u)) == 2);

    /* Later VBlanks: B stays off (never re-enabled by the engine). */
    writes = g_stub.write_calls;
    g_stub.drop_to = 0;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == writes + 3u);            /* A 1 + C 2 */
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ERROR);
    CHECK(ts2_cheat_engine_stats_at(0).applied == 2 && ts2_cheat_engine_stats_at(2).applied == 2);

    /* Explicit re-enable of B re-arms only B. */
    CHECK(ts2_cheat_engine_set_enabled_at(1, 1) == 1);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ACTIVE);
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ACTIVE);
    CHECK(stub_u16(OFF(0x800A0602u)) == 0x3344);

    /* Disabling one leaves the others running. */
    CHECK(ts2_cheat_engine_set_enabled_at(0, 0) == 1);
    writes = g_stub.write_calls;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == writes + 5u);            /* B 3 + C 2 */
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_OFF);
}

static void test_not_started_and_savestate(void)
{
    static const int all_on[3] = { 1, 1, 1 };
    g_test = "not started / savestate";
    stub_reset();
    CHECK(init(k_abc, 3, all_on) == TS2_CHEAT_TABLE_OK);
    g_stub.game_started = 0;
    ts2_cheat_engine_vblank();
    CHECK(g_stub.started_calls == 1);       /* asked once, not per cheat */
    CHECK(g_stub.write_calls == 0);
    CHECK(ts2_cheat_engine_stats_at(0).not_started == 1 &&
          ts2_cheat_engine_stats_at(2).not_started == 1);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_stats_at(1).failures == 0);

    g_stub.game_started = 1;
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ACTIVE &&
          ts2_cheat_engine_state_at(1) == TS2_CHEAT_ACTIVE);
    g_stub.drop_from = OFF(0x800A0700u);    /* C fails */
    g_stub.drop_to = OFF(0x800A0702u);
    g_stub.ram[OFF(0x800A0700u)] = 9;
    ts2_cheat_engine_vblank();
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ERROR);
    g_stub.drop_to = 0;

    /* A restore re-arms every enabled slot; an error stays an error. */
    ts2_cheat_engine_savestate();
    CHECK(ts2_cheat_engine_state_at(0) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_state_at(1) == TS2_CHEAT_ARMED);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ERROR);
    CHECK(ts2_cheat_engine_stats_at(0).savestates == 1 && ts2_cheat_engine_stats_at(2).savestates == 1);
}

static void test_indices_and_legacy(void)
{
    static const int a_on[3] = { 1, 0, 0 };
    ts2_cheat_engine_stats st;
    g_test = "indices / legacy";
    stub_reset();
    CHECK(init(k_abc, 3, a_on) == TS2_CHEAT_TABLE_OK);
    /* Legacy API = index 0. */
    CHECK(ts2_cheat_engine_enabled() == 1 && ts2_cheat_engine_enabled_at(0) == 1);
    CHECK(ts2_cheat_engine_set_enabled(0) == 1 && ts2_cheat_engine_enabled_at(0) == 0);
    CHECK(ts2_cheat_engine_enabled_at(1) == 0 && ts2_cheat_engine_enabled_at(2) == 0);
    CHECK(ts2_cheat_engine_state() == ts2_cheat_engine_state_at(0));
    CHECK(ts2_cheat_engine_ready() == 1);
    CHECK(strcmp(ts2_cheat_engine_def(1)->id, "b") == 0);
    /* Out of range. */
    CHECK(ts2_cheat_engine_def(3) == NULL);
    CHECK(ts2_cheat_engine_state_at(3) == TS2_CHEAT_UNAVAILABLE);
    CHECK(ts2_cheat_engine_state_at((size_t)-1) == TS2_CHEAT_UNAVAILABLE);
    CHECK(ts2_cheat_engine_set_enabled_at(3, 1) == 0);
    CHECK(ts2_cheat_engine_ready_at(3) == 0 && ts2_cheat_engine_enabled_at(3) == 0);
    CHECK(ts2_cheat_engine_setup_status(3) == TS2_CHEAT_DEF_ERR_NOT_CHECKED);
    st = ts2_cheat_engine_stats_at(99);
    CHECK(st.vblanks == 0 && st.applied == 0 && st.last_status == PSXCHEAT_OK);
    /* Names. */
    CHECK(strcmp(ts2_cheat_table_status_name(TS2_CHEAT_TABLE_ERR_DUPLICATE_ID), "duplicate id") == 0);
    CHECK(strcmp(ts2_cheat_def_status_name(TS2_CHEAT_DEF_ERR_TARGET), "write outside own ranges") == 0);

    /* Back to production: index 0 is Max Coins again, nothing else. */
    CHECK(ts2_cheat_engine_init(1) == 1);
    CHECK(ts2_cheat_engine_count() == 1u);
    CHECK(strcmp(ts2_cheat_engine_def(0)->id, "max-coins") == 0);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B2226u)) == 99);
}

static void test_activation_uses_ids(void)
{
    g_test = "activation";
    stub_reset();
    g_plugin.option_value = "true";        /* stub answers "max-coins" */
    ts2_cheat_engine_activate();
    CHECK(ts2_cheat_engine_count() == 1u && ts2_cheat_engine_enabled_at(0) == 1);
    g_plugin.option_value = "yes";
    ts2_cheat_engine_activate();
    CHECK(ts2_cheat_engine_enabled_at(0) == 0);
    g_plugin.option_value = NULL;
    CHECK(g_stub.write_calls == 0);
}

int main(void)
{
    test_production_table();
    test_fatal_table_errors();
    test_per_cheat_errors();
    test_cross_cheat_overlap();
    test_invalid_cheat_isolated_in_engine();
    test_multi_line();
    test_runtime_isolation();
    test_not_started_and_savestate();
    test_indices_and_legacy();
    test_activation_uses_ids();
    printf("cheat table: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
