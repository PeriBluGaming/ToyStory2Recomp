/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include "psxcheat_internal.h"

/* Width in bytes of an operation, 0 for an unknown one. */
static uint32_t op_width(uint8_t op)
{
    switch (op) {
    case PSXCHEAT_OP_WRITE8: return 1u;
    case PSXCHEAT_OP_WRITE16: return 2u;
    default: return 0u;
    }
}

static int backend_ok(const psxcheat_memory *m)
{
    return m && m->valid && m->write8 && m->write16;
}

/* A line's own invariants, independent of the backend. They hold for every
 * parser-produced program; checking them again keeps a hand-built or
 * corrupted program from reaching the backend. */
static psxcheat_status check_line(const psxcheat_line *l, psxcheat_error *err)
{
    const uint32_t width = op_width(l->op);
    if (width == 0u)
        return psxcheat_fail(err, PSXCHEAT_ERR_INVALID_PROGRAM, l->source_line, 0,
                             "unknown operation %u", (unsigned)l->op);
    if ((l->op == PSXCHEAT_OP_WRITE8 && (l->code_type != 0x30u || l->value > 0xFFu)) ||
        (l->op == PSXCHEAT_OP_WRITE16 && l->code_type != 0x80u))
        return psxcheat_fail(err, PSXCHEAT_ERR_INVALID_PROGRAM, l->source_line, 0,
                             "operation %u does not match code type %02X / value %04X",
                             (unsigned)l->op, (unsigned)l->code_type, (unsigned)l->value);
    /* Span check without overflow: offset + width <= RAM size. */
    if (l->offset > PSXCHEAT_RAM_SIZE - width)
        return psxcheat_fail(err, PSXCHEAT_ERR_ADDRESS_RANGE, l->source_line, 0,
                             "%u-byte write at offset %06X leaves the 2 MiB RAM",
                             (unsigned)width, (unsigned)l->offset);
    if (width == 2u && (l->offset & 1u) != 0u)
        return psxcheat_fail(err, PSXCHEAT_ERR_ALIGNMENT, l->source_line, 0,
                             "16-bit write at odd offset %06X", (unsigned)l->offset);
    return PSXCHEAT_OK;
}

psxcheat_status psxcheat_validate(const psxcheat_program *program,
                                  const psxcheat_memory *memory,
                                  psxcheat_error *err)
{
    uint32_t i;
    if (err) {
        err->status = PSXCHEAT_OK;
        err->line = 0;
        err->column = 0;
        err->message[0] = '\0';
    }
    if (!program)
        return psxcheat_fail(err, PSXCHEAT_ERR_ARGUMENT, 0, 0, "no program");
    if (!backend_ok(memory))
        return psxcheat_fail(err, PSXCHEAT_ERR_ARGUMENT, 0, 0,
                             "memory backend lacks valid/write8/write16");
    if (program->count > PSXCHEAT_MAX_CODES)
        return psxcheat_fail(err, PSXCHEAT_ERR_INVALID_PROGRAM, 0, 0,
                             "program claims %u codes, the limit is %u",
                             (unsigned)program->count, (unsigned)PSXCHEAT_MAX_CODES);
    for (i = 0; i < program->count; ++i) {
        const psxcheat_line *l = &program->lines[i];
        const psxcheat_status st = check_line(l, err);
        if (st != PSXCHEAT_OK) return st;
        if (!memory->valid(memory->ctx, l->offset, op_width(l->op)))
            return psxcheat_fail(err, PSXCHEAT_ERR_MEMORY_REJECTED, l->source_line, 0,
                                 "memory backend rejected %u bytes at offset %06X",
                                 (unsigned)op_width(l->op), (unsigned)l->offset);
    }
    return PSXCHEAT_OK;
}

psxcheat_status psxcheat_execute(const psxcheat_program *program,
                                 const psxcheat_memory *memory,
                                 psxcheat_result *result,
                                 psxcheat_error *err)
{
    uint32_t i, done = 0;
    psxcheat_status st;
    if (result) result->writes = 0;
    /* Validate everything before the first write: an invalid program writes
     * nothing at all. */
    st = psxcheat_validate(program, memory, err);
    if (st != PSXCHEAT_OK) return st;
    for (i = 0; i < program->count; ++i) {
        const psxcheat_line *l = &program->lines[i];
        const int ok = l->op == PSXCHEAT_OP_WRITE8
            ? memory->write8(memory->ctx, l->offset, (uint8_t)l->value)
            : memory->write16(memory->ctx, l->offset, l->value);
        if (!ok)
            /* Stop at the first failure; the writes before it remain. */
            return psxcheat_fail(err, PSXCHEAT_ERR_MEMORY_WRITE, l->source_line, 0,
                                 "memory backend failed to write offset %06X (%u earlier writes of this run remain)",
                                 (unsigned)l->offset, (unsigned)done);
        ++done;
        if (result) result->writes = done;
    }
    return PSXCHEAT_OK;
}
