/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "ts2_cheat_memory.h"

/* Verified writable variables / narrow spans, half-open, KSEG0. Each entry
 * needs a provenance comment (how it was verified) and must lie inside an
 * enabled candidate region.
 *
 * Intentionally empty: no TS2 cheat target has been verified yet. A
 * one-element placeholder keeps the array valid C99; the count is 0. */
static const psxcheat_psx_range k_writable[1] = { { 0u, 0u } };
static const size_t k_writable_count = 0u;

const psxcheat_psx_range *ts2_cheat_writable_ranges(size_t *count)
{
    if (count) *count = k_writable_count;
    return k_writable;
}

static int inside(psxcheat_psx_range r, uint32_t lo, uint32_t hi)
{
    return r.start < r.end && r.start >= lo && r.end <= hi;
}

int ts2_cheat_range_permitted(psxcheat_psx_range range)
{
    if (inside(range, TS2_CHEAT_D1_START, TS2_CHEAT_D1_END)) return 1;
#if TS2_CHEAT_D2_ENABLED
    if (inside(range, TS2_CHEAT_D2_START, TS2_CHEAT_D2_END)) return 1;
#endif
    return 0;
}

int ts2_cheat_config_valid(void)
{
    size_t i, j;
    for (i = 0; i < k_writable_count; ++i) {
        if (!ts2_cheat_range_permitted(k_writable[i])) return 0;
        for (j = 0; j < i; ++j)
            if (k_writable[i].start < k_writable[j].end &&
                k_writable[j].start < k_writable[i].end)
                return 0;
    }
    return 1;
}

int ts2_cheat_adapter_config(psxcheat_psx_config *out)
{
    if (!out) return 0;
    out->writable = NULL;
    out->writable_count = 0;
    out->verify_readback = 1;
    if (!ts2_cheat_config_valid()) return 0;
    out->writable = k_writable;
    out->writable_count = k_writable_count;
    return 1;
}
