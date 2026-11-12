/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Consistency check: TS2 writable-memory policy vs the committed recompiler
 * code-range manifest (generated/SLUS_008.93_full.ranges).
 *
 * The manifest is NOT a complete or exact code map:
 *  - it lists only functions the recompiler found (no overlays, no kernel);
 *  - it is over-inclusive: 0x800A04C8 (0x1338 bytes) is gp small data that
 *    was decoded as one "function" of nop/undefined opcodes.
 * So this test does not prove that D1 is data. It pins the known state: the
 * only manifest entry touching D1 is that misclassified block, nothing touches
 * D2, and every explicit TS2 writable entry avoids all other code ranges. If a
 * regeneration changes any of that, the test fails and the policy must be
 * reviewed instead of silently drifting. */
#include "ts2_cheat_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TS2_RANGES_FILE
#error "TS2_RANGES_FILE must name generated/SLUS_008.93_full.ranges"
#endif

#define MAX_RANGES 8192
#define KNOWN_MISCLASSIFIED_D1 0x800A04C8u  /* sdata decoded as a function */

typedef struct { uint32_t lo, len; } code_range;

static code_range g_ranges[MAX_RANGES];
static size_t g_count;
static int g_checks, g_failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

static int load(const char *path, size_t *functions)
{
    char line[128];
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    *functions = 0;
    while (fgets(line, sizeof line, f)) {
        unsigned long lo, len;
        if (line[0] == 'F') {
            ++*functions;
        } else if (line[0] == 'R' && sscanf(line + 1, "%lx %lx", &lo, &len) == 2) {
            if (g_count >= MAX_RANGES) { fclose(f); return 0; }
            g_ranges[g_count].lo = (uint32_t)lo;
            g_ranges[g_count].len = (uint32_t)len;
            ++g_count;
        }
    }
    fclose(f);
    return 1;
}

static int overlaps(const code_range *r, uint32_t start, uint32_t end)
{
    const uint64_t lo = r->lo, hi = (uint64_t)r->lo + r->len;
    return lo < end && start < hi;
}

/* Count manifest ranges overlapping [start, end) other than `except`. */
static size_t count_overlaps(uint32_t start, uint32_t end, uint32_t except, int *saw_except)
{
    size_t i, n = 0;
    if (saw_except) *saw_except = 0;
    for (i = 0; i < g_count; ++i) {
        if (!overlaps(&g_ranges[i], start, end)) continue;
        if (g_ranges[i].lo == except) {
            if (saw_except) *saw_except = 1;
            continue;
        }
        ++n;
        printf("    overlap: range 0x%08X+0x%X with [0x%08X, 0x%08X)\n",
               (unsigned)g_ranges[i].lo, (unsigned)g_ranges[i].len,
               (unsigned)start, (unsigned)end);
    }
    return n;
}

static int inside_some_range(uint32_t address)
{
    size_t i;
    for (i = 0; i < g_count; ++i)
        if (overlaps(&g_ranges[i], address, address + 1u)) return 1;
    return 0;
}

int main(void)
{
    size_t functions = 0, n, i;
    int saw = 0;
    const psxcheat_psx_range *explicit_ranges;

    CHECK(load(TS2_RANGES_FILE, &functions));
    CHECK(functions >= 1000 && g_count >= 1000);
    printf("ranges manifest: %u functions, %u ranges\n", (unsigned)functions, (unsigned)g_count);

    /* D1: only the known misclassified block. */
    n = count_overlaps(TS2_CHEAT_D1_START, TS2_CHEAT_D1_END, KNOWN_MISCLASSIFIED_D1, &saw);
    CHECK(n == 0);
    CHECK(saw == 1);  /* still listed: keep the documentation honest */

    /* D2: nothing (the manifest has no ranges beyond the EXE image). */
    CHECK(count_overlaps(TS2_CHEAT_D2_START, TS2_CHEAT_D2_END, 0u, NULL) == 0);

    /* Explicit writable entries avoid every other code range. */
    explicit_ranges = ts2_cheat_writable_ranges(&n);
    for (i = 0; i < n; ++i)
        CHECK(count_overlaps(explicit_ranges[i].start, explicit_ranges[i].end,
                             KNOWN_MISCLASSIFIED_D1, NULL) == 0);

    /* Phase 3C.4B-1: the Advanced region overlaps no code range except the
     * known misclassified data block, starts above the last EXE code range
     * and is exactly one entry. */
    {
        size_t na = 0;
        uint64_t code_end = 0;
        const psxcheat_psx_range *adv = ts2_cheat_advanced_region(&na);
        CHECK(na == 1u && adv != NULL);
        CHECK(adv && adv[0].start == TS2_CHEAT_ADVANCED_START &&
              adv[0].end == TS2_CHEAT_ADVANCED_END);
        CHECK(count_overlaps(TS2_CHEAT_ADVANCED_START, TS2_CHEAT_ADVANCED_END,
                             KNOWN_MISCLASSIFIED_D1, NULL) == 0);
        for (i = 0; i < g_count; ++i)
            if (g_ranges[i].lo != KNOWN_MISCLASSIFIED_D1 &&
                (uint64_t)g_ranges[i].lo + g_ranges[i].len > code_end)
                code_end = (uint64_t)g_ranges[i].lo + g_ranges[i].len;
        CHECK(code_end == 0x80097DC8u);             /* last real code range */
        CHECK(code_end <= TS2_CHEAT_ADVANCED_START);
        CHECK(!ts2_cheat_advanced_range_permitted((psxcheat_psx_range){ 0x80067440u, 0x80067444u }));
    }

    /* Sanity: hook sites used by existing mods are code in the manifest and
     * outside both candidate regions. */
    CHECK(inside_some_range(0x80067440u));  /* camera target update */
    CHECK(inside_some_range(0x800742C4u));  /* pause input handler */
    CHECK(inside_some_range(0x80086054u));  /* DrawOTag */
    CHECK(!ts2_cheat_range_permitted((psxcheat_psx_range){ 0x80067440u, 0x80067444u }));

    printf("ts2 ranges check: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
