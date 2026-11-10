/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "psxcheat_psxrecomp.h"

#include <string.h>

#include "mod_plugins.h"  /* psx_mod_read/write_byte/half, psx_mod_game_started */

const char *psxcheat_psx_status_name(psxcheat_psx_status status)
{
    switch (status) {
    case PSXCHEAT_PSX_OK: return "ok";
    case PSXCHEAT_PSX_ERR_ARGUMENT: return "invalid argument";
    case PSXCHEAT_PSX_ERR_TOO_MANY_RANGES: return "too many writable ranges";
    case PSXCHEAT_PSX_ERR_RANGE_EMPTY: return "empty or inverted range";
    case PSXCHEAT_PSX_ERR_RANGE_OUTSIDE: return "range outside 2 MiB RAM";
    case PSXCHEAT_PSX_ERR_RANGE_OVERLAP: return "overlapping ranges";
    }
    return "unknown status";
}

const char *psxcheat_psx_reject_name(psxcheat_psx_reject reason)
{
    switch (reason) {
    case PSXCHEAT_PSX_REJECT_NONE: return "none";
    case PSXCHEAT_PSX_REJECT_NOT_CONFIGURED: return "adapter not configured";
    case PSXCHEAT_PSX_REJECT_NO_CONTEXT: return "no execution context";
    case PSXCHEAT_PSX_REJECT_NOT_STARTED: return "game not started";
    case PSXCHEAT_PSX_REJECT_WIDTH: return "unsupported width";
    case PSXCHEAT_PSX_REJECT_OUTSIDE_RAM: return "outside 2 MiB RAM";
    case PSXCHEAT_PSX_REJECT_MISALIGNED: return "misaligned";
    case PSXCHEAT_PSX_REJECT_NO_ALLOWLIST: return "no writable ranges configured";
    case PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED: return "not in a writable range";
    case PSXCHEAT_PSX_REJECT_NULL_OUTPUT: return "null output";
    case PSXCHEAT_PSX_REJECT_READBACK: return "read-back mismatch";
    }
    return "unknown reason";
}

uint32_t psxcheat_psx_guest_address(uint32_t offset)
{
    return offset < PSXCHEAT_RAM_SIZE ? PSXCHEAT_PSX_RAM_BASE + offset : 0u;
}

psxcheat_psx_status psxcheat_psx_init(psxcheat_psx_adapter *adapter,
                                      const psxcheat_psx_config *config)
{
    size_t i, j;
    if (!adapter) return PSXCHEAT_PSX_ERR_ARGUMENT;
    memset(adapter, 0, sizeof *adapter);  /* unconfigured until fully valid */
    if (!config) return PSXCHEAT_PSX_ERR_ARGUMENT;
    if (config->writable_count > 0 && !config->writable) return PSXCHEAT_PSX_ERR_ARGUMENT;
    if (config->writable_count > PSXCHEAT_PSX_MAX_RANGES) return PSXCHEAT_PSX_ERR_TOO_MANY_RANGES;

    for (i = 0; i < config->writable_count; ++i) {
        const psxcheat_psx_range r = config->writable[i];
        if (r.start >= r.end) return PSXCHEAT_PSX_ERR_RANGE_EMPTY;
        if (r.start < PSXCHEAT_PSX_RAM_BASE || r.end > PSXCHEAT_PSX_RAM_END)
            return PSXCHEAT_PSX_ERR_RANGE_OUTSIDE;
        for (j = 0; j < i; ++j) {
            const psxcheat_psx_range q = config->writable[j];
            if (r.start < q.end && q.start < r.end) return PSXCHEAT_PSX_ERR_RANGE_OVERLAP;
        }
    }
    for (i = 0; i < config->writable_count; ++i) {
        adapter->ranges[i].start = config->writable[i].start - PSXCHEAT_PSX_RAM_BASE;
        adapter->ranges[i].end = config->writable[i].end - PSXCHEAT_PSX_RAM_BASE;
    }
    adapter->range_count = config->writable_count;
    adapter->verify_readback = config->verify_readback != 0;
    adapter->configured = 1;
    return PSXCHEAT_PSX_OK;
}

int psxcheat_psx_begin(psxcheat_psx_adapter *adapter)
{
    if (!adapter || !adapter->configured || adapter->context_active) return 0;
    adapter->context_active = 1;
    return 1;
}

void psxcheat_psx_end(psxcheat_psx_adapter *adapter)
{
    if (adapter) adapter->context_active = 0;
}

/* Span inside the 2 MiB RAM, width 1 or 2, 2-byte spans even. */
static psxcheat_psx_reject check_span(uint32_t offset, uint32_t width)
{
    if (width != 1u && width != 2u) return PSXCHEAT_PSX_REJECT_WIDTH;
    if (offset > PSXCHEAT_RAM_SIZE - width) return PSXCHEAT_PSX_REJECT_OUTSIDE_RAM;
    if (width == 2u && (offset & 1u) != 0u) return PSXCHEAT_PSX_REJECT_MISALIGNED;
    return PSXCHEAT_PSX_REJECT_NONE;
}

int psxcheat_psx_span_writable(const psxcheat_psx_adapter *adapter,
                               uint32_t offset, uint32_t width)
{
    size_t i;
    if (!adapter || !adapter->configured) return 0;
    if (check_span(offset, width) != PSXCHEAT_PSX_REJECT_NONE) return 0;
    for (i = 0; i < adapter->range_count; ++i) {
        const psxcheat_psx_range r = adapter->ranges[i];
        /* offset + width cannot overflow: check_span bounded it by 2 MiB. */
        if (offset >= r.start && offset + width <= r.end) return 1;
    }
    return 0;
}

/* Common gate for every access; `write` adds the allowlist requirement. */
static psxcheat_psx_reject gate(const psxcheat_psx_adapter *a, uint32_t offset,
                                uint32_t width, int write)
{
    psxcheat_psx_reject r;
    if (!a->configured) return PSXCHEAT_PSX_REJECT_NOT_CONFIGURED;
    if (!a->context_active) return PSXCHEAT_PSX_REJECT_NO_CONTEXT;
    r = check_span(offset, width);
    if (r != PSXCHEAT_PSX_REJECT_NONE) return r;
    if (write) {
        if (a->range_count == 0) return PSXCHEAT_PSX_REJECT_NO_ALLOWLIST;
        if (!psxcheat_psx_span_writable(a, offset, width))
            return PSXCHEAT_PSX_REJECT_NOT_ALLOWLISTED;
    }
    /* Last, so a misconfigured adapter never even asks the runtime. */
    if (!psx_mod_game_started()) return PSXCHEAT_PSX_REJECT_NOT_STARTED;
    return PSXCHEAT_PSX_REJECT_NONE;
}

static int refuse(psxcheat_psx_adapter *a, psxcheat_psx_reject reason)
{
    a->last_reject = reason;
    ++a->stats.rejected;
    return 0;
}

static int a_valid(void *ctx, uint32_t offset, uint32_t width)
{
    psxcheat_psx_adapter *a = (psxcheat_psx_adapter *)ctx;
    psxcheat_psx_reject r;
    if (!a) return 0;
    r = gate(a, offset, width, 1);
    if (r != PSXCHEAT_PSX_REJECT_NONE) return refuse(a, r);
    ++a->stats.allowed;
    return 1;
}

/* After a write: 1 when the write counts as done (handed over, and matching
 * on read-back when enabled). */
static int finish_write(psxcheat_psx_adapter *a, int matched)
{
    ++a->stats.attempted;
    if (!a->verify_readback) return 1;
    if (!matched) {
        ++a->stats.mismatched;
        return refuse(a, PSXCHEAT_PSX_REJECT_READBACK);
    }
    ++a->stats.verified;
    return 1;
}

static int a_write8(void *ctx, uint32_t offset, uint8_t value)
{
    psxcheat_psx_adapter *a = (psxcheat_psx_adapter *)ctx;
    psxcheat_psx_reject r;
    if (!a) return 0;
    r = gate(a, offset, 1u, 1);  /* re-checked even after valid() */
    if (r != PSXCHEAT_PSX_REJECT_NONE) return refuse(a, r);
    psx_mod_write_byte(PSXCHEAT_PSX_RAM_BASE + offset, value);
    return finish_write(a, a->verify_readback
        ? psx_mod_read_byte(PSXCHEAT_PSX_RAM_BASE + offset) == value : 1);
}

static int a_write16(void *ctx, uint32_t offset, uint16_t value)
{
    psxcheat_psx_adapter *a = (psxcheat_psx_adapter *)ctx;
    psxcheat_psx_reject r;
    if (!a) return 0;
    r = gate(a, offset, 2u, 1);
    if (r != PSXCHEAT_PSX_REJECT_NONE) return refuse(a, r);
    psx_mod_write_half(PSXCHEAT_PSX_RAM_BASE + offset, value);
    return finish_write(a, a->verify_readback
        ? psx_mod_read_half(PSXCHEAT_PSX_RAM_BASE + offset) == value : 1);
}

static int a_read8(void *ctx, uint32_t offset, uint8_t *value)
{
    psxcheat_psx_adapter *a = (psxcheat_psx_adapter *)ctx;
    psxcheat_psx_reject r;
    if (!a) return 0;
    if (!value) return refuse(a, PSXCHEAT_PSX_REJECT_NULL_OUTPUT);
    r = gate(a, offset, 1u, 0);
    if (r != PSXCHEAT_PSX_REJECT_NONE) return refuse(a, r);
    *value = psx_mod_read_byte(PSXCHEAT_PSX_RAM_BASE + offset);
    ++a->stats.reads;
    return 1;
}

static int a_read16(void *ctx, uint32_t offset, uint16_t *value)
{
    psxcheat_psx_adapter *a = (psxcheat_psx_adapter *)ctx;
    psxcheat_psx_reject r;
    if (!a) return 0;
    if (!value) return refuse(a, PSXCHEAT_PSX_REJECT_NULL_OUTPUT);
    r = gate(a, offset, 2u, 0);
    if (r != PSXCHEAT_PSX_REJECT_NONE) return refuse(a, r);
    *value = psx_mod_read_half(PSXCHEAT_PSX_RAM_BASE + offset);
    ++a->stats.reads;
    return 1;
}

psxcheat_memory psxcheat_psx_backend(psxcheat_psx_adapter *adapter)
{
    psxcheat_memory m;
    m.ctx = adapter;
    m.valid = a_valid;
    m.write8 = a_write8;
    m.write16 = a_write16;
    m.read8 = a_read8;
    m.read16 = a_read16;
    return m;
}
