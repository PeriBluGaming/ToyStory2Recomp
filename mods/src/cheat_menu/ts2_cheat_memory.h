/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_MEMORY_H
#define TS2_CHEAT_MEMORY_H

/* Toy Story 2 (SLUS-00893) writable-memory policy for the psxcheat adapter.
 *
 * Classification (Phase 1C-A.1 audit, static analysis + runtime-verified
 * variables):
 *   D1 [0x800A04C8, 0x800A1800)  gp small data (.sdata/.sbss); the
 *        recompiler's code-range manifest wrongly lists it as one function
 *        (0x800A04C8, 0x1338 bytes of nop/undefined opcodes). High confidence.
 *   D2 [0x800A1800, 0x800D0000)  BSS/heap above the EXE image. Upper bound
 *        not verified against the overlay load addresses. DISABLED as a
 *        whole; it may only hold individually verified narrow spans.
 *   Everything else (kernel, EXE code, .rodata/.data gap, overlay/heap/stack
 *   area) is rejected.
 *
 * D1 and D2 are CANDIDATE regions only, never enabled as a whole. What the
 * adapter may write is the explicit list of verified variables below, and
 * nothing else; each entry must be permitted by ts2_cheat_range_permitted:
 * inside D1, or a span of at most TS2_CHEAT_D2_EXPLICIT_MAX bytes inside D2.
 *
 * No executable-code patching: no entry may cover code. */

#include <stddef.h>

#include "psxcheat_psxrecomp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS2_CHEAT_D1_START 0x800A04C8u
#define TS2_CHEAT_D1_END   0x800A1800u
#define TS2_CHEAT_D2_START 0x800A1800u
#define TS2_CHEAT_D2_END   0x800D0000u
#define TS2_CHEAT_D2_ENABLED 0  /* D2 as a whole: until overlay bounds and ownership are verified */
#define TS2_CHEAT_D2_EXPLICIT_MAX 4u  /* bytes: largest individually verified D2 span */

/* Max Coins: coin counter, int16, capped at 99 by the game. */
#define TS2_CHEAT_COINS_ADDR 0x800B2226u
/* Infinite Lives: lives counter, int16, capped at 9 by the game. */
#define TS2_CHEAT_LIVES_ADDR 0x800B2222u
/* Player health, int16 (signed): 0..14 in normal play, negative = dead.
 * Verified for EXTERNAL cheats only (Phase 3B.2); no built-in uses it. The
 * policy authorises the address, not a value: 14 (0x000E) is the game's
 * own maximum and the recommended value, see TS2_CHEAT_HEALTH_RECOMMENDED. */
#define TS2_CHEAT_HEALTH_ADDR 0x800B221Eu
#define TS2_CHEAT_HEALTH_RECOMMENDED "800B221E 000E"

/* The explicit writable list (count via *count). */
const psxcheat_psx_range *ts2_cheat_writable_ranges(size_t *count);

/* 1 when `range` may be an explicit writable entry: a non-empty half-open
 * range inside D1, or inside D2 and at most TS2_CHEAT_D2_EXPLICIT_MAX bytes
 * long (any size when TS2_CHEAT_D2_ENABLED). */
int ts2_cheat_range_permitted(psxcheat_psx_range range);

/* 1 when every explicit entry is permitted and no two entries overlap. */
int ts2_cheat_config_valid(void);

/* The trusted EXTERNAL policy (Phase 3B): the only memory a user-supplied
 * cheat (ts2_cheat_external.h) may write. Source code, never file content;
 * every entry must lie inside the verified list above. The coins and lives
 * spans are owned by built-in cheats, so an external cheat there is
 * rejected as overlapping a built-in; health (Phase 3B.3) is the first span
 * an external cheat can actually use. Addresses only: no value checks. */
const psxcheat_psx_range *ts2_cheat_external_policy(size_t *count);

/* ADVANCED region (Phase 3C.4B-1): the one data window an external cheat of
 * the Advanced trust tier (ts2_cheat_table.h, ts2_cheat_trust) may write.
 * It is NOT part of the verified list above, never configures the built-in
 * adapter, and changes nothing for Safe entries. Only the caller of the
 * external build selects Advanced mode (never file content), and only for
 * entries the Safe policy rejects; their writes stay inside this region and
 * inside each cheat's own derived spans. Evidence (static, disc/SLUS_008.93
 * and the overlay caches of earlier runs):
 *  - EXE image 0x80010000..0x800A1800 (header t_size 0x91800, no b_size);
 *    recompiled code ranges end at 0x80097DC8; the only manifest entry in
 *    the region is the known misclassified data block at 0x800A04C8
 *    (ts2_ranges_check); no j/jal in the EXE targets the region.
 *  - Startup __start (0x80082D60) clears BSS [0x800A1070, 0x800D12C0) and
 *    calls InitHeap (A0:39, 0x80089344) with heap [0x800D12C4, 0x801F8000)
 *    (2 MiB - stack 0x8000, word 0x800A0764); stack above, sp from RAM top.
 *  - Code overlays load at or above _end: level overlays at 0x800D12C0
 *    (0x8003DEA4), bits\memory.bin and fmv\fmv.bin at 0x800D5D20. Files
 *    read into the region are data (PAD\PATHnn.BIN at 0x800BA0C8, *.vh at
 *    0x800BB8D8). All 612 cached overlay code ranges lie below 0x800A04C8
 *    (kernel / EXE pages) or at/above 0x800D6000.
 *  - gp data (2194 gp-relative accesses) spans 0x800A0CE0..0x800A8AAE.
 * So [0x800A04C8, 0x800D0000) is image data (.sdata) and BSS; it ends
 * 0x12C0 below the heap. It holds pointer tables, buffers and game state:
 * a bad value can break or crash the game (and reach memory card saves),
 * but no write can reach code, overlays, heap, stack, kernel or host
 * memory. Not verified: destinations computed at run time (.all, .anm,
 * level.dat loads) -- data files in any case. */
#define TS2_CHEAT_ADVANCED_START 0x800A04C8u
#define TS2_CHEAT_ADVANCED_END   0x800D0000u

/* The Advanced region as a one-entry list (count via *count). */
const psxcheat_psx_range *ts2_cheat_advanced_region(size_t *count);
/* 1 when `range` is non-empty and lies inside the Advanced region. */
int ts2_cheat_advanced_range_permitted(psxcheat_psx_range range);

/* Adapter configuration for TS2: the explicit list, read-back on. Returns 0
 * (leaving *out describing no writable range) when the list is invalid. */
int ts2_cheat_adapter_config(psxcheat_psx_config *out);

#ifdef __cplusplus
}
#endif

#endif
