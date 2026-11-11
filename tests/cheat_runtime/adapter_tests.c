/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Unit tests for the psxcheat <-> psxrecomp adapter and the TS2 memory
 * policy. Runtime services are stubs (psx_runtime_stub.c): no guest memory. */
#include "psx_runtime_stub.h"
#include "psxcheat.h"
#include "psxcheat_psxrecomp.h"
#include "ts2_cheat_memory.h"

#include <stdio.h>
#include <stdlib.h>
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

static psxcheat_psx_adapter g_ad;
static psxcheat_program g_prog;
static psxcheat_error g_err;

/* Fresh stub + adapter with `count` ranges; returns the init status. */
static psxcheat_psx_status setup(const psxcheat_psx_range *ranges, size_t count, int verify)
{
    psxcheat_psx_config cfg;
    stub_reset();
    cfg.writable = ranges;
    cfg.writable_count = count;
    cfg.verify_readback = verify;
    return psxcheat_psx_init(&g_ad, &cfg);
}

static int valid(uint32_t offset, uint32_t width)
{
    const psxcheat_memory m = psxcheat_psx_backend(&g_ad);
    return m.valid(m.ctx, offset, width);
}
static int w8(uint32_t offset, uint8_t v)
{
    const psxcheat_memory m = psxcheat_psx_backend(&g_ad);
    return m.write8(m.ctx, offset, v);
}
static int w16(uint32_t offset, uint16_t v)
{
    const psxcheat_memory m = psxcheat_psx_backend(&g_ad);
    return m.write16(m.ctx, offset, v);
}

/* ------------------------------------------------------------------------- */

static void test_translation(void)
{
    g_test = "translation";
    CHECK(psxcheat_psx_guest_address(0) == 0x80000000u);
    CHECK(psxcheat_psx_guest_address(0x0A10F4u) == 0x800A10F4u);
    CHECK(psxcheat_psx_guest_address(0x1FFFFFu) == 0x801FFFFFu);
    CHECK(psxcheat_psx_guest_address(0x200000u) == 0u);
    CHECK(psxcheat_psx_guest_address(0xFFFFFFFFu) == 0u);
}

static void test_init_errors(void)
{
    static psxcheat_psx_range many[PSXCHEAT_PSX_MAX_RANGES + 1u];
    psxcheat_psx_config cfg;
    size_t i;
    const psxcheat_psx_range empty[] = { { 0x80001000u, 0x80001000u } };
    const psxcheat_psx_range inverted[] = { { 0x80001010u, 0x80001000u } };
    const psxcheat_psx_range below[] = { { 0x7FFFFFF0u, 0x80000010u } };
    const psxcheat_psx_range above[] = { { 0x801FFFF0u, 0x80200001u } };
    const psxcheat_psx_range kuseg[] = { { 0x00001000u, 0x00001010u } };
    const psxcheat_psx_range kseg1[] = { { 0xA0001000u, 0xA0001010u } };
    const psxcheat_psx_range overlap[] = { { 0x80001000u, 0x80001010u },
                                           { 0x8000100Fu, 0x80001020u } };
    g_test = "init errors";

    CHECK(psxcheat_psx_init(NULL, &cfg) == PSXCHEAT_PSX_ERR_ARGUMENT);
    CHECK(psxcheat_psx_init(&g_ad, NULL) == PSXCHEAT_PSX_ERR_ARGUMENT);
    CHECK(!g_ad.configured);
    CHECK(setup(NULL, 1, 0) == PSXCHEAT_PSX_ERR_ARGUMENT);
    CHECK(setup(empty, 1, 0) == PSXCHEAT_PSX_ERR_RANGE_EMPTY);
    CHECK(setup(inverted, 1, 0) == PSXCHEAT_PSX_ERR_RANGE_EMPTY);
    CHECK(setup(below, 1, 0) == PSXCHEAT_PSX_ERR_RANGE_OUTSIDE);
    CHECK(setup(above, 1, 0) == PSXCHEAT_PSX_ERR_RANGE_OUTSIDE);
    CHECK(setup(kuseg, 1, 0) == PSXCHEAT_PSX_ERR_RANGE_OUTSIDE);
    CHECK(setup(kseg1, 1, 0) == PSXCHEAT_PSX_ERR_RANGE_OUTSIDE);
    CHECK(setup(overlap, 2, 0) == PSXCHEAT_PSX_ERR_RANGE_OVERLAP);
    for (i = 0; i < PSXCHEAT_PSX_MAX_RANGES + 1u; ++i) {
        many[i].start = 0x80001000u + (uint32_t)i * 2u;
        many[i].end = many[i].start + 1u;
    }
    CHECK(setup(many, PSXCHEAT_PSX_MAX_RANGES + 1u, 0) == PSXCHEAT_PSX_ERR_TOO_MANY_RANGES);
    CHECK(setup(many, PSXCHEAT_PSX_MAX_RANGES, 0) == PSXCHEAT_PSX_OK);

    /* A failed init leaves the adapter refusing everything, without asking
     * the runtime anything. */
    CHECK(setup(overlap, 2, 0) == PSXCHEAT_PSX_ERR_RANGE_OVERLAP);
    CHECK(!g_ad.configured);
    CHECK(psxcheat_psx_begin(&g_ad) == 0);
    CHECK(valid(0x1000, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_CONFIGURED);
    CHECK(w8(0x1000, 1) == 0);
    CHECK(g_stub.write_calls == 0 && g_stub.started_calls == 0);
    CHECK(psxcheat_psx_begin(NULL) == 0);
    psxcheat_psx_end(NULL);  /* must not crash */
}

static void test_empty_allowlist(void)
{
    uint8_t b = 0;
    psxcheat_memory m;
    g_test = "empty allowlist";
    CHECK(setup(NULL, 0, 0) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(valid(0x1000, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NO_ALLOWLIST);
    CHECK(w16(0x1000, 0x1234) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NO_ALLOWLIST);
    CHECK(g_stub.write_calls == 0);
    /* Reads do not need a writable range. */
    g_stub.ram[0x1000] = 0x5A;
    m = psxcheat_psx_backend(&g_ad);
    CHECK(m.read8(m.ctx, 0x1000, &b) == 1 && b == 0x5A);
    psxcheat_psx_end(&g_ad);
}

static void test_accepted_writes(void)
{
    const psxcheat_psx_range r[] = { { 0x80001000u, 0x80001010u } };
    g_test = "accepted writes";
    CHECK(setup(r, 1, 0) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(valid(0x1000, 1) == 1);
    CHECK(w8(0x1000, 0xAB) == 1);
    CHECK(w16(0x100E, 0xBEEF) == 1);  /* ends exactly at the range end */
    CHECK(w8(0x100F, 0x11) == 1);
    psxcheat_psx_end(&g_ad);
    CHECK(g_stub.write_calls == 3 && g_stub.bad_addresses == 0);
    CHECK(g_stub.log[0].address == 0x80001000u && g_stub.log[0].width == 1 && g_stub.log[0].value == 0xAB);
    CHECK(g_stub.log[1].address == 0x8000100Eu && g_stub.log[1].width == 2 && g_stub.log[1].value == 0xBEEF);
    CHECK(stub_u8(0x1000) == 0xAB);
    CHECK(stub_u8(0x100E) == 0xEF && stub_u8(0x100F) == 0x11);
    CHECK(g_ad.stats.attempted == 3 && g_ad.stats.verified == 0 && g_ad.stats.allowed == 1);
}

static void test_boundaries(void)
{
    const psxcheat_psx_range r[] = { { 0x80001000u, 0x80001010u } };
    const psxcheat_psx_range odd[] = { { 0x80001001u, 0x80001003u } };
    const psxcheat_psx_range adjacent[] = { { 0x80002000u, 0x80002001u },
                                            { 0x80002001u, 0x80002002u } };
    g_test = "allowlist boundaries";
    CHECK(setup(r, 1, 0) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(valid(0x0FFF, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(valid(0x1010, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(valid(0x1010, 2) == 0);
    CHECK(valid(0x100F, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_MISALIGNED);
    CHECK(valid(0x0FFE, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(w16(0x0FFE, 1) == 0 && w8(0x1010, 1) == 0);
    psxcheat_psx_end(&g_ad);
    CHECK(g_stub.write_calls == 0);

    /* A span may not cross a range start or end. */
    CHECK(setup(odd, 1, 0) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(valid(0x1000, 2) == 0);   /* starts before the range */
    CHECK(valid(0x1002, 2) == 0);   /* ends after it */
    CHECK(valid(0x1001, 1) == 1 && valid(0x1002, 1) == 1);
    psxcheat_psx_end(&g_ad);

    /* A span must lie inside ONE range, even across adjacent ones. */
    CHECK(setup(adjacent, 2, 0) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(valid(0x2000, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(valid(0x2000, 1) == 1 && valid(0x2001, 1) == 1);
    psxcheat_psx_end(&g_ad);
    CHECK(g_stub.write_calls == 0);
}

static void test_ram_limits(void)
{
    const psxcheat_psx_range top[] = { { 0x801FFFF0u, 0x80200000u } };
    g_test = "RAM limits";
    CHECK(setup(top, 1, 0) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(w16(0x1FFFFE, 0xA1B2) == 1);
    CHECK(w8(0x1FFFFF, 0x77) == 1);
    CHECK(valid(0x1FFFFF, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_OUTSIDE_RAM);
    CHECK(valid(0x200000, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_OUTSIDE_RAM);
    CHECK(valid(0xFFFFFFFFu, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_OUTSIDE_RAM);
    CHECK(valid(0x1FFFF0, 0) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_WIDTH);
    CHECK(valid(0x1FFFF0, 4) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_WIDTH);
    psxcheat_psx_end(&g_ad);
    CHECK(g_stub.write_calls == 2 && g_stub.bad_addresses == 0);
    CHECK(g_stub.log[0].address == 0x801FFFFEu && g_stub.log[1].address == 0x801FFFFFu);
}

static void test_game_started_and_context(void)
{
    const psxcheat_psx_range r[] = { { 0x80001000u, 0x80001010u } };
    g_test = "game start / context";
    CHECK(setup(r, 1, 0) == PSXCHEAT_PSX_OK);

    /* No context: refused before the runtime is asked anything. */
    CHECK(valid(0x1000, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NO_CONTEXT);
    CHECK(w8(0x1000, 1) == 0 && w16(0x1000, 1) == 0);
    CHECK(g_stub.started_calls == 0 && g_stub.write_calls == 0);

    /* Context, but the game has not started. */
    g_stub.game_started = 0;
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(psxcheat_psx_begin(&g_ad) == 0);  /* no nesting */
    CHECK(valid(0x1000, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_STARTED);
    CHECK(w8(0x1000, 1) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_STARTED);
    CHECK(g_stub.write_calls == 0);

    /* Started: allowed. After end: refused again. */
    g_stub.game_started = 1;
    CHECK(w8(0x1000, 0x42) == 1);
    psxcheat_psx_end(&g_ad);
    CHECK(w8(0x1001, 0x43) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NO_CONTEXT);
    CHECK(g_stub.write_calls == 1 && stub_u8(0x1000) == 0x42 && stub_u8(0x1001) == 0);
    CHECK(g_ad.stats.rejected >= 6);
}

static void test_core_integration(void)
{
    const psxcheat_psx_range r[] = { { 0x80001000u, 0x80001004u } };
    psxcheat_memory m;
    psxcheat_result res;
    g_test = "core integration";
    CHECK(setup(r, 1, 0) == PSXCHEAT_PSX_OK);
    m = psxcheat_psx_backend(&g_ad);

    /* Accepted program, source order. */
    CHECK(psxcheat_parse("30001000 00AA\n80001002 1234\n30001001 00BB\n", 42, &g_prog, &g_err) == PSXCHEAT_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(psxcheat_execute(&g_prog, &m, &res, &g_err) == PSXCHEAT_OK && res.writes == 3);
    psxcheat_psx_end(&g_ad);
    CHECK(g_stub.write_calls == 3);
    CHECK(g_stub.log[0].address == 0x80001000u && g_stub.log[1].address == 0x80001002u &&
          g_stub.log[2].address == 0x80001001u);
    CHECK(stub_u8(0x1000) == 0xAA && stub_u8(0x1001) == 0xBB && stub_u16(0x1002) == 0x1234u);

    /* One line outside the allowlist: full prevalidation, nothing written. */
    stub_reset();
    CHECK(psxcheat_parse("30001000 00AA\n80001004 1234\n", 28, &g_prog, &g_err) == PSXCHEAT_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(psxcheat_execute(&g_prog, &m, &res, &g_err) == PSXCHEAT_ERR_MEMORY_REJECTED);
    CHECK(g_err.line == 2 && res.writes == 0);
    psxcheat_psx_end(&g_ad);
    CHECK(g_stub.write_calls == 0);

    /* Game not started / no context: nothing written either. */
    stub_reset();
    g_stub.game_started = 0;
    CHECK(psxcheat_parse("30001000 00AA\n", 14, &g_prog, &g_err) == PSXCHEAT_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(psxcheat_execute(&g_prog, &m, &res, &g_err) == PSXCHEAT_ERR_MEMORY_REJECTED);
    psxcheat_psx_end(&g_ad);
    g_stub.game_started = 1;
    CHECK(psxcheat_execute(&g_prog, &m, &res, &g_err) == PSXCHEAT_ERR_MEMORY_REJECTED);
    CHECK(g_stub.write_calls == 0 && g_stub.bad_addresses == 0);
}

static void test_readback(void)
{
    const psxcheat_psx_range r[] = { { 0x80001000u, 0x80001010u } };
    psxcheat_memory m;
    psxcheat_result res;
    g_test = "read-back";

    /* Verified when the bytes read back. */
    CHECK(setup(r, 1, 1) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(w8(0x1000, 0x11) == 1 && w16(0x1002, 0x2233) == 1);
    CHECK(g_ad.stats.attempted == 2 && g_ad.stats.verified == 2 && g_ad.stats.mismatched == 0);

    /* A runtime that drops the write: the mismatch is reported, and the core
     * stops at that line. */
    g_stub.drop_writes = 1;
    CHECK(w8(0x1004, 0x55) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_READBACK);
    CHECK(g_ad.stats.attempted == 3 && g_ad.stats.mismatched == 1);
    CHECK(psxcheat_parse("30001006 0066\n30001007 0077\n", 28, &g_prog, &g_err) == PSXCHEAT_OK);
    m = psxcheat_psx_backend(&g_ad);
    CHECK(psxcheat_execute(&g_prog, &m, &res, &g_err) == PSXCHEAT_ERR_MEMORY_WRITE);
    CHECK(g_err.line == 1 && res.writes == 0);
    /* Writing the value RAM already holds cannot be told apart from a dropped
     * write: read-back is evidence, not proof. */
    CHECK(w8(0x1000, 0x11) == 1);
    psxcheat_psx_end(&g_ad);

    /* Without read-back a dropped write still counts as attempted "success":
     * the void runtime API cannot report it. */
    CHECK(setup(r, 1, 0) == PSXCHEAT_PSX_OK);
    g_stub.drop_writes = 1;
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(w8(0x1000, 0x99) == 1);
    psxcheat_psx_end(&g_ad);
    CHECK(g_ad.stats.attempted == 1 && g_ad.stats.verified == 0 && stub_u8(0x1000) == 0);
}

static void test_reads(void)
{
    psxcheat_memory m;
    uint8_t b = 0;
    uint16_t h = 0;
    g_test = "reads";
    CHECK(setup(NULL, 0, 0) == PSXCHEAT_PSX_OK);
    m = psxcheat_psx_backend(&g_ad);
    g_stub.ram[0x1FFFFE] = 0x34;
    g_stub.ram[0x1FFFFF] = 0x12;
    CHECK(m.read16(m.ctx, 0x1FFFFE, &h) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NO_CONTEXT);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(m.read16(m.ctx, 0x1FFFFE, &h) == 1 && h == 0x1234u);
    CHECK(m.read8(m.ctx, 0x1FFFFF, &b) == 1 && b == 0x12);
    CHECK(m.read16(m.ctx, 0x1FFFFF, &h) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_OUTSIDE_RAM);
    CHECK(m.read16(m.ctx, 0x1001, &h) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_MISALIGNED);
    CHECK(m.read8(m.ctx, 0x200000, &b) == 0);
    CHECK(m.read8(m.ctx, 0x1000, NULL) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NULL_OUTPUT);
    g_stub.game_started = 0;
    CHECK(m.read8(m.ctx, 0x1000, &b) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_STARTED);
    psxcheat_psx_end(&g_ad);
    CHECK(g_ad.stats.reads == 2 && g_stub.read_calls == 2 && g_stub.write_calls == 0);
    CHECK(g_stub.bad_addresses == 0);
}

static void test_null_context(void)
{
    psxcheat_memory m = psxcheat_psx_backend(NULL);
    uint8_t b;
    g_test = "null adapter";
    stub_reset();
    CHECK(m.valid(m.ctx, 0, 1) == 0);
    CHECK(m.write8(m.ctx, 0, 1) == 0 && m.write16(m.ctx, 0, 1) == 0);
    CHECK(m.read8(m.ctx, 0, &b) == 0);
    CHECK(psxcheat_psx_span_writable(NULL, 0, 1) == 0);
    CHECK(g_stub.write_calls == 0 && g_stub.read_calls == 0 && g_stub.started_calls == 0);
}

static void test_ts2_policy(void)
{
    size_t count = 99;
    psxcheat_psx_config cfg;
    const psxcheat_psx_range d1 = { TS2_CHEAT_D1_START, TS2_CHEAT_D1_END };
    const psxcheat_psx_range pause = { 0x800A10F4u, 0x800A10F6u };
    const psxcheat_psx_range cross = { 0x800A17FEu, 0x800A1802u };
    const psxcheat_psx_range below = { 0x800A04C6u, 0x800A04C9u };
    const psxcheat_psx_range d2_two = { 0x800B2196u, 0x800B2198u };
    const psxcheat_psx_range d2_four = { 0x800C1618u, 0x800C161Cu };
    const psxcheat_psx_range d2_five = { 0x800C1618u, 0x800C161Du };
    const psxcheat_psx_range d2_whole = { TS2_CHEAT_D2_START, TS2_CHEAT_D2_END };
    const psxcheat_psx_range d2_cross_end = { 0x800CFFFEu, 0x800D0002u };
    const psxcheat_psx_range code = { 0x80067440u, 0x80067444u };
    const psxcheat_psx_range kernel = { 0x80000100u, 0x80000104u };
    const psxcheat_psx_range empty = { 0x800A10F4u, 0x800A10F4u };
    const psxcheat_psx_range *list;
    uint8_t b = 0;
    psxcheat_memory m;
    g_test = "TS2 policy";

    /* Exactly two explicit entries: the Max Coins counter and (Phase 2E-D2)
     * the lives counter, 2 bytes each, disjoint. */
    list = ts2_cheat_writable_ranges(&count);
    CHECK(count == 3);
    CHECK(list[2].start == 0x800B221Eu && list[2].end == 0x800B2220u);   /* health (3B.3) */
    CHECK(TS2_CHEAT_HEALTH_ADDR == 0x800B221Eu);
    CHECK(list[0].start == 0x800B2226u && list[0].end == 0x800B2228u);
    CHECK(list[1].start == 0x800B2222u && list[1].end == 0x800B2224u);   /* lives (2E-D2) */
    CHECK(TS2_CHEAT_LIVES_ADDR == 0x800B2222u);
    CHECK(TS2_CHEAT_COINS_ADDR == 0x800B2226u);
    CHECK(ts2_cheat_config_valid() == 1);
    CHECK(ts2_cheat_adapter_config(&cfg) == 1 && cfg.writable_count == 3 && cfg.verify_readback == 1);
    CHECK(ts2_cheat_adapter_config(NULL) == 0);

    /* Through the adapter: only those two spans are writable. */
    stub_reset();
    CHECK(psxcheat_psx_init(&g_ad, &cfg) == PSXCHEAT_PSX_OK);
    CHECK(psxcheat_psx_begin(&g_ad) == 1);
    CHECK(valid(0x0B2226, 2) == 1);
    CHECK(valid(0x0B2226, 1) == 1 && valid(0x0B2227, 1) == 1);  /* bytes of the int16 */
    CHECK(valid(0x0B2224, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(valid(0x0B2228, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(valid(0x0B2225, 1) == 0 && valid(0x0B2228, 1) == 0);
    CHECK(valid(0x0B2227, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_MISALIGNED);
    CHECK(valid(0x0B2222, 2) == 1);   /* lives counter (2E-D2) */
    CHECK(valid(0x0B2222, 1) == 1 && valid(0x0B2223, 1) == 1);
    CHECK(valid(0x0B2220, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(valid(0x0B2221, 1) == 0);   /* byte below the lives span */
    CHECK(valid(0x0B2224, 1) == 0 && valid(0x0B2225, 1) == 0);  /* gap between the spans */
    CHECK(valid(0x0B221E, 2) == 1);   /* health counter (3B.3) */
    CHECK(valid(0x0B221E, 1) == 1 && valid(0x0B221F, 1) == 1);
    CHECK(valid(0x0B221C, 2) == 0 && g_ad.last_reject == PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED);
    CHECK(valid(0x0B221D, 1) == 0);   /* byte below the health span */
    CHECK(valid(0x0B2220, 1) == 0 && valid(0x0B2221, 1) == 0);  /* timer: closed */
    CHECK(valid(0x0A10F4, 2) == 0);   /* D1 variable: not allowlisted */
    CHECK(valid(0x0C1618, 1) == 0);   /* other D2 data: not allowlisted */
    CHECK(w16(0x0B2226, 0x0063) == 1);
    CHECK(w16(0x0B2228, 0x0063) == 0);
    m = psxcheat_psx_backend(&g_ad);
    CHECK(m.read8(m.ctx, 0x0B2226, &b) == 1 && b == 0x63);
    psxcheat_psx_end(&g_ad);
    CHECK(g_stub.write_calls == 1 && g_stub.log[0].address == 0x800B2226u &&
          g_stub.log[0].width == 2 && g_stub.log[0].value == 0x63);
    CHECK(g_ad.stats.verified == 1);

    /* Explicit-entry eligibility: D1 spans; narrow (<= 4 byte) D2 spans; D2
     * never as a whole or in wider spans; nothing else. */
    CHECK(TS2_CHEAT_D2_ENABLED == 0);
    CHECK(ts2_cheat_range_permitted(d1) == 1);
    CHECK(ts2_cheat_range_permitted(pause) == 1);
    CHECK(ts2_cheat_range_permitted(cross) == 0);
    CHECK(ts2_cheat_range_permitted(below) == 0);
    CHECK(ts2_cheat_range_permitted(d2_two) == 1);
    CHECK(ts2_cheat_range_permitted(d2_four) == 1);
    CHECK(ts2_cheat_range_permitted(d2_five) == 0);
    CHECK(ts2_cheat_range_permitted(d2_whole) == 0);
    CHECK(ts2_cheat_range_permitted(d2_cross_end) == 0);
    CHECK(ts2_cheat_range_permitted(code) == 0);
    CHECK(ts2_cheat_range_permitted(kernel) == 0);
    CHECK(ts2_cheat_range_permitted(empty) == 0);
}

static void test_names(void)
{
    int i;
    g_test = "names";
    for (i = PSXCHEAT_PSX_OK; i <= PSXCHEAT_PSX_ERR_RANGE_OVERLAP; ++i)
        CHECK(strcmp(psxcheat_psx_status_name((psxcheat_psx_status)i), "unknown status") != 0);
    for (i = PSXCHEAT_PSX_REJECT_NONE; i <= PSXCHEAT_PSX_REJECT_READBACK; ++i)
        CHECK(strcmp(psxcheat_psx_reject_name((psxcheat_psx_reject)i), "unknown reason") != 0);
}

int main(void)
{
    test_translation();
    test_init_errors();
    test_empty_allowlist();
    test_accepted_writes();
    test_boundaries();
    test_ram_limits();
    test_game_started_and_context();
    test_core_integration();
    test_readback();
    test_reads();
    test_null_context();
    test_ts2_policy();
    test_names();
    printf("psxcheat adapter: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
