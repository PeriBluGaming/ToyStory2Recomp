/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef PSX_RUNTIME_STUB_H
#define PSX_RUNTIME_STUB_H

/* Test-only stand-ins for the psxrecomp services the adapter calls
 * (psx_mod_read/write_byte/half, psx_mod_game_started), compiled against the
 * real declarations in mod_plugins.h. Backed by a fake 2 MiB RAM; no guest
 * memory exists in these tests. */

#include <stdint.h>

#define STUB_RAM_SIZE 0x00200000u
#define STUB_LOG_MAX 256u

typedef struct stub_access {
    uint32_t address;  /* guest address as the adapter passed it */
    uint32_t width;
    uint32_t value;
} stub_access;

typedef struct psx_runtime_stub {
    uint8_t ram[STUB_RAM_SIZE];
    int game_started;
    int drop_writes;          /* simulate a runtime that discards writes */
    /* Discard only writes touching canonical offsets [drop_from, drop_to)
     * (inactive while drop_to <= drop_from). */
    uint32_t drop_from, drop_to;
    uint32_t started_calls;
    uint32_t read_calls;
    uint32_t write_calls;
    uint32_t bad_addresses;   /* accesses not in KSEG0 2 MiB RAM */
    uint32_t log_count;
    stub_access log[STUB_LOG_MAX];  /* writes, in order */
} psx_runtime_stub;

extern psx_runtime_stub g_stub;

/* Zeroed RAM, game started, writes kept, counters and log cleared. */
void stub_reset(void);
uint8_t stub_u8(uint32_t offset);
uint16_t stub_u16(uint32_t offset);

#endif
