/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "ts2_cheat_memory.h"

/* Verified writable variables / narrow spans, half-open, KSEG0. Each entry
 * needs a provenance comment (how it was verified) and must be permitted by
 * ts2_cheat_range_permitted. */
static const psxcheat_psx_range k_writable[] = {
    /* Coin counter (int16), D2. Phase 1C-C1, static analysis of the
     * generated code: func_8007678C loads it from 0x800B2226 and stores
     * value + 1 only while value < 99 (coin pickup, 0x80076D8C..0x80076DAC);
     * the HUD (func_80074F80, 0x800750B8) reads it; func_8007776C compares
     * it with 50 (Hamm trade). Fixed global: always addressed absolutely
     * (lui 0x800B + 0x2226), never through a heap pointer. Not code (no
     * code-range manifest entry), far below the known overlay load
     * addresses (>= 0x800D0000). Used by the "Max Coins" test cheat
     * 800B2226 0063. */
    { TS2_CHEAT_COINS_ADDR, TS2_CHEAT_COINS_ADDR + 2u },
    /* Lives counter (int16), D2. Phase 2E-D1, static analysis of the EXE
     * (disc/SLUS_008.93) and a pattern scan of the whole disc image: every
     * access found in the EXE is absolute (lui 0x800B + 0x2222), and the
     * scan found no direct overlay store to it with the searched patterns
     * (lui 0x800B + store, lui 0x800B + addiu). Indirect or computed
     * overlay accesses were not ruled out. Extra-life pickup func_8007678C
     * adds 1 only while value < 9 (0x80076C70..0x80076C9C); the death path
     * in func_8007A9E8 goes to game over at 0 and otherwise subtracts 1
     * (0x8007B030..0x8007B060; respawn func_8007BCE4 0x8007BE30); the HUD
     * (func_80074F80, 0x800757AC) prints it; save/restore copies it as a
     * byte into the save block 0x800C1608 (0x80078C84 / 0x80078CC4). Not
     * code (ts2_ranges_check), below the overlay load addresses, disjoint
     * from the coin counter. Used by the "Infinite Lives" cheat
     * 800B2222 0009. */
    { TS2_CHEAT_LIVES_ADDR, TS2_CHEAT_LIVES_ADDR + 2u },
    /* Player health (int16, signed), D2. Phase 3B.2, static analysis of the
     * EXE: every access is absolute (lui 0x800B + 0x221E); the disc pattern
     * scan found stores in the EXE only (indirect or computed overlay
     * accesses not ruled out). Damage func_8006AF10 subtracts 1
     * (0x8006B158..0x8006B18C) and starts the death sequence below 0
     * (0x8006B1B4); the main loop takes a life when it is negative
     * (0x8007B030); healing adds 4 and caps at 14 (0x80076B94..0x80076BCC);
     * respawn func_8007BCE4 sets 14 (0x8007BD48); the HUD derives the gauge
     * from it (0x8007599C); save/restore copies it to the save block
     * 0x800C1608 + 0xC unclamped (0x80078CB8 / 0x80078CF8). Exactly this
     * halfword: the timer at 0x800B2220 and the state at 0x800B221C stay
     * closed. No built-in cheat uses it (external policy only). */
    { TS2_CHEAT_HEALTH_ADDR, TS2_CHEAT_HEALTH_ADDR + 2u },
};
static const size_t k_writable_count = sizeof k_writable / sizeof k_writable[0];

/* External policy (Phase 3B): verified variables only; see the header.
 * Adding an entry here needs the same verification as k_writable. */
static const psxcheat_psx_range k_external[] = {
    { TS2_CHEAT_COINS_ADDR, TS2_CHEAT_COINS_ADDR + 2u },
    { TS2_CHEAT_LIVES_ADDR, TS2_CHEAT_LIVES_ADDR + 2u },
    { TS2_CHEAT_HEALTH_ADDR, TS2_CHEAT_HEALTH_ADDR + 2u },   /* Phase 3B.3 */
};

const psxcheat_psx_range *ts2_cheat_external_policy(size_t *count)
{
    if (count) *count = sizeof k_external / sizeof k_external[0];
    return k_external;
}

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
    if (inside(range, TS2_CHEAT_D2_START, TS2_CHEAT_D2_END)) {
#if TS2_CHEAT_D2_ENABLED
        return 1;
#else
        /* D2 stays disabled as a whole; only narrow verified spans. */
        return range.end - range.start <= TS2_CHEAT_D2_EXPLICIT_MAX;
#endif
    }
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
