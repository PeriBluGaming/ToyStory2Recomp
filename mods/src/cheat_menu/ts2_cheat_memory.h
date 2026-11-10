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
 *        not verified against the overlay load addresses. DISABLED.
 *   Everything else (kernel, EXE code, .rodata/.data gap, overlay/heap/stack
 *   area) is rejected.
 *
 * D1 and D2 are CANDIDATE regions only, never enabled as a whole. What the
 * adapter may write is the explicit list of verified variables / narrow
 * spans below; each entry must lie inside an enabled candidate region. The
 * list is empty until a verified test cheat exists, so the configuration
 * currently allows no write at all.
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
#define TS2_CHEAT_D2_ENABLED 0  /* until overlay bounds and ownership are verified */

/* The explicit writable list (may be empty; count via *count). */
const psxcheat_psx_range *ts2_cheat_writable_ranges(size_t *count);

/* 1 when `range` is a non-empty half-open range inside an ENABLED candidate
 * region (D1; D2 only when TS2_CHEAT_D2_ENABLED). */
int ts2_cheat_range_permitted(psxcheat_psx_range range);

/* 1 when every explicit entry is permitted and no two entries overlap. */
int ts2_cheat_config_valid(void);

/* Adapter configuration for TS2: the explicit list, read-back on. Returns 0
 * (leaving *out describing no writable range) when the list is invalid. */
int ts2_cheat_adapter_config(psxcheat_psx_config *out);

#ifdef __cplusplus
}
#endif

#endif
