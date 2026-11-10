/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "psx_runtime_stub.h"

#include <string.h>

#include "mod_plugins.h"  /* the real declarations: signatures must match */

psx_runtime_stub g_stub;

void stub_reset(void)
{
    memset(&g_stub, 0, sizeof g_stub);
    g_stub.game_started = 1;
}

uint8_t stub_u8(uint32_t offset) { return g_stub.ram[offset]; }

uint16_t stub_u16(uint32_t offset)
{
    return (uint16_t)(g_stub.ram[offset] | ((uint16_t)g_stub.ram[offset + 1u] << 8));
}

/* Offset of a KSEG0 RAM address, or -1 (counted) for anything else. */
static long ram_offset(uint32_t address, uint32_t width)
{
    if (address < 0x80000000u || address - 0x80000000u > STUB_RAM_SIZE - width) {
        ++g_stub.bad_addresses;
        return -1;
    }
    return (long)(address - 0x80000000u);
}

/* Write `width` bytes at `off`? Off for drop_writes or the drop window. */
static int keeps(long off, uint32_t width)
{
    if (off < 0 || g_stub.drop_writes) return 0;
    if (g_stub.drop_to > g_stub.drop_from &&
        (uint32_t)off < g_stub.drop_to && (uint32_t)off + width > g_stub.drop_from)
        return 0;
    return 1;
}

static void log_write(uint32_t address, uint32_t width, uint32_t value)
{
    if (g_stub.log_count < STUB_LOG_MAX) {
        g_stub.log[g_stub.log_count].address = address;
        g_stub.log[g_stub.log_count].width = width;
        g_stub.log[g_stub.log_count].value = value;
    }
    ++g_stub.log_count;
}

int psx_mod_game_started(void)
{
    ++g_stub.started_calls;
    return g_stub.game_started;
}

uint8_t psx_mod_read_byte(uint32_t address)
{
    const long off = ram_offset(address, 1u);
    ++g_stub.read_calls;
    return off < 0 ? 0u : g_stub.ram[off];
}

uint16_t psx_mod_read_half(uint32_t address)
{
    const long off = ram_offset(address, 2u);
    ++g_stub.read_calls;
    return off < 0 ? 0u : stub_u16((uint32_t)off);
}

void psx_mod_write_byte(uint32_t address, uint8_t value)
{
    const long off = ram_offset(address, 1u);
    ++g_stub.write_calls;
    log_write(address, 1u, value);
    if (keeps(off, 1u)) g_stub.ram[off] = value;
}

void psx_mod_write_half(uint32_t address, uint16_t value)
{
    const long off = ram_offset(address, 2u);
    ++g_stub.write_calls;
    log_write(address, 2u, value);
    if (keeps(off, 2u)) {
        g_stub.ram[off] = (uint8_t)(value & 0xFFu);
        g_stub.ram[off + 1] = (uint8_t)(value >> 8);
    }
}
