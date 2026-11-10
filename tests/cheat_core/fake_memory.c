/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "fake_memory.h"

#include <string.h>

void fake_memory_reset(fake_memory *m)
{
    memset(m, 0, sizeof *m);
    m->valid_limit = PSXCHEAT_RAM_SIZE;
    m->fail_write_index = -1;
}

static int span_ok(const fake_memory *m, uint32_t offset, uint32_t width)
{
    if (width == 0u || width > 2u) return 0;
    if (offset > PSXCHEAT_RAM_SIZE - width) return 0;   /* no overflow */
    if (offset + width > m->valid_limit) return 0;
    if (m->reject_offset_set && m->reject_offset >= offset &&
        m->reject_offset < offset + width)
        return 0;
    return 1;
}

static int f_valid(void *ctx, uint32_t offset, uint32_t width)
{
    fake_memory *m = (fake_memory *)ctx;
    ++m->valid_calls;
    return span_ok(m, offset, width);
}

static int begin_write(fake_memory *m, uint32_t offset, uint32_t width)
{
    const uint32_t index = m->write_calls++;
    if (m->fail_write_index >= 0 && index == (uint32_t)m->fail_write_index) return 0;
    return span_ok(m, offset, width);
}

static void log_write(fake_memory *m, uint32_t offset, uint32_t width, uint16_t value)
{
    if (m->log_count < FAKE_MEMORY_LOG_MAX) {
        fake_write *w = &m->log[m->log_count];
        w->offset = offset;
        w->width = width;
        w->value = value;
    }
    ++m->log_count;
}

static int f_write8(void *ctx, uint32_t offset, uint8_t value)
{
    fake_memory *m = (fake_memory *)ctx;
    if (!begin_write(m, offset, 1u)) return 0;
    m->ram[offset] = value;
    log_write(m, offset, 1u, value);
    return 1;
}

static int f_write16(void *ctx, uint32_t offset, uint16_t value)
{
    fake_memory *m = (fake_memory *)ctx;
    if (!begin_write(m, offset, 2u)) return 0;
    m->ram[offset] = (uint8_t)(value & 0xFFu);
    m->ram[offset + 1u] = (uint8_t)(value >> 8);
    log_write(m, offset, 2u, value);
    return 1;
}

static int f_read8(void *ctx, uint32_t offset, uint8_t *value)
{
    fake_memory *m = (fake_memory *)ctx;
    if (!value || !span_ok(m, offset, 1u)) return 0;
    *value = m->ram[offset];
    return 1;
}

static int f_read16(void *ctx, uint32_t offset, uint16_t *value)
{
    fake_memory *m = (fake_memory *)ctx;
    if (!value || !span_ok(m, offset, 2u)) return 0;
    *value = fake_memory_u16(m, offset);
    return 1;
}

psxcheat_memory fake_memory_backend(fake_memory *m)
{
    psxcheat_memory b;
    b.ctx = m;
    b.valid = f_valid;
    b.write8 = f_write8;
    b.write16 = f_write16;
    b.read8 = f_read8;
    b.read16 = f_read16;
    return b;
}

uint8_t fake_memory_u8(const fake_memory *m, uint32_t offset)
{
    return m->ram[offset];
}

uint16_t fake_memory_u16(const fake_memory *m, uint32_t offset)
{
    return (uint16_t)(m->ram[offset] | ((uint16_t)m->ram[offset + 1u] << 8));
}

int fake_memory_all_zero(const fake_memory *m)
{
    uint32_t i;
    for (i = 0; i < PSXCHEAT_RAM_SIZE; ++i)
        if (m->ram[i]) return 0;
    return 1;
}
