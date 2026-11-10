/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef PSXCHEAT_PSXRECOMP_H
#define PSXCHEAT_PSXRECOMP_H

/* psxcheat <-> psxrecomp memory adapter.
 *
 * Generic for every psxrecomp title: it knows no game addresses. It turns the
 * psxcheat core's canonical RAM offsets into guest accesses through the
 * runtime's trusted-plugin services (mod_plugins.h: psx_mod_read_byte/half,
 * psx_mod_write_byte/half, psx_mod_game_started).
 *
 * Address translation: guest address = 0x80000000 + offset (KSEG0). The core
 * only produces offsets below 2 MiB; KUSEG/KSEG0/KSEG1 reach the same RAM
 * bytes, and those bytes are the same with the opt-in 8 MiB map.
 *
 * A write is permitted only when ALL of these hold:
 *  - the adapter was initialised with a valid configuration;
 *  - the whole span [offset, offset + width) lies inside ONE configured
 *    writable range (half-open guest-address ranges, KSEG0, inside the
 *    canonical 2 MiB RAM); no ranges configured means nothing is writable;
 *  - width is 1 or 2, a 2-byte span starts at an even offset;
 *  - an execution context is active (psxcheat_psx_begin .. psxcheat_psx_end);
 *  - psx_mod_game_started() reports the game has started.
 * Reads (reserved for conditional code types) need a valid configuration,
 * an active context, a started game and a span inside the 2 MiB RAM, but no
 * writable range.
 *
 * Limitation, by design of the runtime API: psx_mod_write_byte/half return
 * void. The adapter cannot know that a write reached RAM. It counts writes
 * it HANDED to the runtime as "attempted". With read-back enabled it reads
 * the bytes again right away and counts "verified" or "mismatched"; a match
 * only shows that RAM held the value at that instant (the guest may change
 * it at any time afterwards), and the runtime has contexts (render passes,
 * netplay host output) where writes are diverted or dropped and reads may
 * not reflect RAM. Read-back is evidence, not a guarantee.
 *
 * Threading: an adapter is not thread-safe. Use it only on the emulation
 * thread (a psxrecomp VBlank or savestate callback), never from a render,
 * UI or ImGui callback. The execution context exists to enforce this. */

#include <stddef.h>
#include <stdint.h>

#include "psxcheat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PSXCHEAT_PSX_RAM_BASE     0x80000000u   /* KSEG0 */
#define PSXCHEAT_PSX_RAM_END      (PSXCHEAT_PSX_RAM_BASE + PSXCHEAT_RAM_SIZE)
#define PSXCHEAT_PSX_MAX_RANGES   32u

/* Half-open guest-address range [start, end), KSEG0. */
typedef struct psxcheat_psx_range {
    uint32_t start;
    uint32_t end;
} psxcheat_psx_range;

typedef struct psxcheat_psx_config {
    const psxcheat_psx_range *writable;  /* may be NULL when count is 0 */
    size_t writable_count;
    int verify_readback;                 /* nonzero: read back after writing */
} psxcheat_psx_config;

typedef enum psxcheat_psx_status {
    PSXCHEAT_PSX_OK = 0,
    PSXCHEAT_PSX_ERR_ARGUMENT,        /* NULL adapter/config, NULL ranges with count > 0 */
    PSXCHEAT_PSX_ERR_TOO_MANY_RANGES, /* more than PSXCHEAT_PSX_MAX_RANGES */
    PSXCHEAT_PSX_ERR_RANGE_EMPTY,     /* start >= end */
    PSXCHEAT_PSX_ERR_RANGE_OUTSIDE,   /* not inside [RAM_BASE, RAM_END) */
    PSXCHEAT_PSX_ERR_RANGE_OVERLAP    /* two ranges overlap */
} psxcheat_psx_status;

/* Why the last access was refused (diagnostics). */
typedef enum psxcheat_psx_reject {
    PSXCHEAT_PSX_REJECT_NONE = 0,
    PSXCHEAT_PSX_REJECT_NOT_CONFIGURED,
    PSXCHEAT_PSX_REJECT_NO_CONTEXT,
    PSXCHEAT_PSX_REJECT_NOT_STARTED,
    PSXCHEAT_PSX_REJECT_WIDTH,
    PSXCHEAT_PSX_REJECT_OUTSIDE_RAM,
    PSXCHEAT_PSX_REJECT_MISALIGNED,
    PSXCHEAT_PSX_REJECT_NO_ALLOWLIST,    /* configured with zero writable ranges */
    PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED, /* span not inside one writable range */
    PSXCHEAT_PSX_REJECT_NULL_OUTPUT,     /* read into a NULL pointer */
    PSXCHEAT_PSX_REJECT_READBACK         /* read-back did not match */
} psxcheat_psx_reject;

typedef struct psxcheat_psx_stats {
    uint32_t allowed;     /* spans the valid() callback accepted */
    uint32_t rejected;    /* accesses refused (any reason) */
    uint32_t attempted;   /* writes handed to the runtime (not proof) */
    uint32_t verified;    /* read-back matched (read-back mode only) */
    uint32_t mismatched;  /* read-back differed (read-back mode only) */
    uint32_t reads;       /* reads handed to the runtime */
} psxcheat_psx_stats;

typedef struct psxcheat_psx_adapter {
    int configured;
    int context_active;
    int verify_readback;
    size_t range_count;
    /* Stored as canonical RAM offsets [start, end). */
    psxcheat_psx_range ranges[PSXCHEAT_PSX_MAX_RANGES];
    psxcheat_psx_stats stats;
    psxcheat_psx_reject last_reject;
} psxcheat_psx_adapter;

/* Validate `config` and set the adapter up. On any error the adapter stays
 * unconfigured and refuses every access. Ranges are copied. */
psxcheat_psx_status psxcheat_psx_init(psxcheat_psx_adapter *adapter,
                                      const psxcheat_psx_config *config);

/* The psxcheat_memory backend bound to `adapter`. */
psxcheat_memory psxcheat_psx_backend(psxcheat_psx_adapter *adapter);

/* Open the execution context. Returns 1 when it was opened, 0 when the
 * adapter is NULL, unconfigured, or a context is already open (no nesting). */
int psxcheat_psx_begin(psxcheat_psx_adapter *adapter);
void psxcheat_psx_end(psxcheat_psx_adapter *adapter);

/* Guest address of a canonical RAM offset; 0 when offset >= 2 MiB. */
uint32_t psxcheat_psx_guest_address(uint32_t offset);

/* 1 when [offset, offset + width) lies inside one configured writable range
 * (ignores context and game state; for configuration checks and tests). */
int psxcheat_psx_span_writable(const psxcheat_psx_adapter *adapter,
                               uint32_t offset, uint32_t width);

const char *psxcheat_psx_status_name(psxcheat_psx_status status);
const char *psxcheat_psx_reject_name(psxcheat_psx_reject reason);

#ifdef __cplusplus
}
#endif

#endif
