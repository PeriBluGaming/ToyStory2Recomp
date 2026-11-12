/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3C.4B-1: bounded, deterministic fuzzer for the Advanced trust tier.
 * Each iteration builds 1..4 random external entries (random 30/80 lines
 * plus other code types, offsets biased to the region edges, built-in
 * cheats, overlays, heap and stack, aligned and misaligned, values that do
 * and do not fit) in Safe or Advanced mode, activates the real engine,
 * switches EVERY accepted external entry on and runs two VBlanks over the
 * stub RAM. Invariants: every guest write lies inside the Advanced region,
 * inside the derived spans of an accepted entry, never on a built-in cheat,
 * and in Safe mode inside the external policy; Advanced entries exist only
 * in Advanced mode and start off; no access outside RAM. Fixed seed and
 * iteration count. */
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
#include "ts2_cheat_engine.h"
#include "ts2_cheat_external.h"
#include "ts2_cheat_memory.h"
#include "ts2_cheat_table.h"

#include <stdio.h>
#include <string.h>

#define ITERATIONS 20000u
#define MAX_ENTRIES 4u
#define MAX_LINES 6u

static int g_failures;
static unsigned long g_checks;
static unsigned g_it;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            if (g_failures++ < 20)                                           \
                printf("  FAIL iteration %u: %s\n", g_it, #cond);            \
        }                                                                    \
    } while (0)

static unsigned long long g_state = 0xD1B54A32D192ED03ull;

static unsigned rnd(unsigned n)
{
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return n ? (unsigned)(g_state % n) : 0u;
}

/* A RAM offset from one of several interesting neighbourhoods. */
static uint32_t pick_offset(void)
{
    static const uint32_t spots[] = {
        0x0A04C8u, 0x0D0000u, 0x0B221Eu, 0x0B2222u, 0x0B2226u, 0x0C1618u,
        0x0D12C0u, 0x0D5D20u, 0x097DC8u, 0x010000u, 0x000000u, 0x1F8000u,
        0x1FFFF0u, 0x1FFFFEu, 0x0A1070u, 0x0A0CD8u,
    };
    switch (rnd(4u)) {
    case 0: return rnd(0x200000u + 0x40u);                       /* may exceed RAM */
    case 1: return 0x0A04C8u + rnd(0x0D0000u - 0x0A04C8u);       /* inside the region */
    default: {
        const uint32_t s = spots[rnd(sizeof spots / sizeof spots[0])];
        const uint32_t d = rnd(33u);
        return d < 16u ? (s >= 16u - d ? s - (16u - d) : 0u) : s + (d - 16u);
    }
    }
}

static void make_line(char *out, size_t size)
{
    static const char *const other[] = { "D0", "D1", "E0", "10", "11", "20", "50", "C0",
                                         "C1", "1F", "90", "31", "81", "00" };
    uint32_t off = pick_offset();
    unsigned type = rnd(10u);
    if (type < 4u) {            /* 8-bit */
        const unsigned v = rnd(8u) == 0u ? 0x100u + rnd(0xFF00u) : rnd(0x100u);
        snprintf(out, size, "30%06X %04X", (unsigned)(off & 0xFFFFFFu), v);
    } else if (type < 9u) {     /* 16-bit, mostly aligned */
        if (rnd(6u) != 0u) off &= ~1u;
        snprintf(out, size, "80%06X %04X", (unsigned)(off & 0xFFFFFFu), rnd(0x10000u));
    } else {
        snprintf(out, size, "%s%06X %04X", other[rnd(sizeof other / sizeof other[0])],
                 (unsigned)(off & 0xFFFFFFu), rnd(0x10000u));
    }
}

static int inside(uint32_t a, uint32_t w, psxcheat_psx_range r)
{
    return a >= r.start && a + w <= r.end;
}

int main(void)
{
    static char text[MAX_ENTRIES][MAX_LINES][24];
    static const char *lines[MAX_ENTRIES][MAX_LINES];
    static char ids[MAX_ENTRIES][8];
    ts2_cheat_external_input in[MAX_ENTRIES];
    ts2_cheat_ext_status st[MAX_ENTRIES];
    ts2_cheat_trust tr[MAX_ENTRIES];
    unsigned long accepted_adv = 0, accepted_safe = 0, writes = 0;
    size_t nb = 0, np = 0;
    const ts2_cheat_def *builtins = ts2_cheat_table(&nb);
    const psxcheat_psx_range *policy = ts2_cheat_external_policy(&np);

    for (g_it = 0; g_it < ITERATIONS; ++g_it) {
        const unsigned ne = 1u + rnd(MAX_ENTRIES);
        const ts2_cheat_ext_mode mode =
            rnd(5u) == 0u ? TS2_CHEAT_EXT_MODE_SAFE : TS2_CHEAT_EXT_MODE_ADVANCED;
        size_t n = 0, e, k, i;
        const ts2_cheat_def *t;
        for (e = 0; e < ne; ++e) {
            const unsigned nl = 1u + rnd(MAX_LINES);
            for (k = 0; k < nl; ++k) {
                make_line(text[e][k], sizeof text[e][k]);
                lines[e][k] = text[e][k];
            }
            snprintf(ids[e], sizeof ids[e], "f%u", (unsigned)e);
            memset(&in[e], 0, sizeof in[e]);
            in[e].id = ids[e];
            in[e].label = ids[e];
            in[e].code_lines = lines[e];
            in[e].code_line_count = nl;
            in[e].enabled = (int)rnd(2u);
        }
        ts2_cheat_engine_shutdown();
        ts2_cheat_combined_reset();
        stub_reset();
        if (ts2_cheat_session_begin() != 1) { CHECK(0); continue; }
        CHECK(ts2_cheat_combined_build_mode(in, ne, mode, st, tr) == TS2_CHEAT_BUILD_OK);
        t = ts2_cheat_active_table(&n);
        CHECK(t != NULL && n >= nb && n <= nb + ne);
        if (!t) continue;
        /* Table invariants per accepted entry. */
        for (i = nb; i < n; ++i) {
            const ts2_cheat_trust tier = ts2_cheat_active_trust(i);
            CHECK(tier == TS2_CHEAT_TRUST_SAFE || tier == TS2_CHEAT_TRUST_ADVANCED);
            CHECK(mode == TS2_CHEAT_EXT_MODE_ADVANCED || tier == TS2_CHEAT_TRUST_SAFE);
            if (tier == TS2_CHEAT_TRUST_ADVANCED) {
                ++accepted_adv;
                CHECK(ts2_cheat_active_external_enabled(i) == 0);
            } else {
                ++accepted_safe;
            }
            for (k = 0; k < t[i].range_count; ++k) {
                int in_policy = 0;
                size_t j;
                CHECK(ts2_cheat_advanced_range_permitted(t[i].ranges[k]));
                for (j = 0; j < np; ++j)
                    in_policy |= t[i].ranges[k].start >= policy[j].start &&
                                 t[i].ranges[k].end <= policy[j].end;
                if (tier == TS2_CHEAT_TRUST_SAFE) CHECK(in_policy);
                for (j = 0; j < nb; ++j) {
                    size_t b;
                    for (b = 0; b < builtins[j].range_count; ++b)
                        CHECK(!(t[i].ranges[k].start < builtins[j].ranges[b].end &&
                                builtins[j].ranges[b].start < t[i].ranges[k].end));
                }
            }
        }
        for (e = 0; e < ne; ++e)
            CHECK(st[e] != TS2_CHEAT_EXT_OK || mode == TS2_CHEAT_EXT_MODE_ADVANCED ||
                  tr[e] == TS2_CHEAT_TRUST_SAFE);
        ts2_cheat_engine_activate();
        for (i = nb; i < n; ++i)
            if (ts2_cheat_active_trust(i) == TS2_CHEAT_TRUST_ADVANCED)
                CHECK(ts2_cheat_engine_enabled_at(i) == 0);
        /* Switch every accepted external entry on; built-ins stay off. */
        for (i = nb; i < n; ++i) (void)ts2_cheat_engine_set_enabled_at(i, 1);
        ts2_cheat_engine_vblank();
        ts2_cheat_engine_vblank();
        CHECK(g_stub.bad_addresses == 0u);
        CHECK(g_stub.log_count <= STUB_LOG_MAX && g_stub.write_calls == g_stub.log_count);
        for (k = 0; k < g_stub.log_count && k < STUB_LOG_MAX; ++k) {
            const uint32_t a = g_stub.log[k].address, w = g_stub.log[k].width;
            int owned = 0;
            ++writes;
            CHECK(a >= TS2_CHEAT_ADVANCED_START && a + w <= TS2_CHEAT_ADVANCED_END);
            for (i = nb; i < n && !owned; ++i) {
                size_t r;
                for (r = 0; r < t[i].range_count && !owned; ++r)
                    owned = inside(a, w, t[i].ranges[r]);
            }
            CHECK(owned);
            for (i = 0; i < nb; ++i) {
                size_t b;
                for (b = 0; b < builtins[i].range_count; ++b)
                    CHECK(!(a < builtins[i].ranges[b].end && builtins[i].ranges[b].start < a + w));
            }
        }
    }
    ts2_cheat_engine_shutdown();
    ts2_cheat_combined_reset();
    printf("advanced cheats fuzz: %u iterations (%lu advanced, %lu safe accepted, %lu writes), "
           "%lu checks, %d failures\n",
           ITERATIONS, accepted_adv, accepted_safe, writes, g_checks, g_failures);
    return g_failures ? 1 : 0;
}
