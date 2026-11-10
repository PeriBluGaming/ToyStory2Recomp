/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef PSXCHEAT_FAKE_MEMORY_H
#define PSXCHEAT_FAKE_MEMORY_H

/* Test-only psxcheat memory backend: a 2 MiB byte array standing in for PS1
 * main RAM, with a write log and injectable failures. Never touches guest
 * memory. */

#include "psxcheat.h"

#define FAKE_MEMORY_LOG_MAX 1024u

typedef struct fake_write {
    uint32_t offset;
    uint32_t width;
    uint16_t value;
} fake_write;

typedef struct fake_memory {
    uint8_t ram[PSXCHEAT_RAM_SIZE];

    /* Injection. */
    uint32_t valid_limit;     /* valid() accepts spans ending at or below it */
    int reject_offset_set;    /* valid() rejects any span touching... */
    uint32_t reject_offset;   /* ...this byte */
    int fail_write_index;     /* write number (0-based) that fails; -1 none */

    /* Observation. */
    uint32_t valid_calls;
    uint32_t write_calls;     /* attempted writes, including the failed one */
    uint32_t log_count;       /* successful writes */
    fake_write log[FAKE_MEMORY_LOG_MAX];
} fake_memory;

/* Zeroed RAM, everything valid, no injected failures, empty log. */
void fake_memory_reset(fake_memory *m);
/* A psxcheat_memory bound to `m` (read callbacks included). */
psxcheat_memory fake_memory_backend(fake_memory *m);

uint8_t fake_memory_u8(const fake_memory *m, uint32_t offset);
uint16_t fake_memory_u16(const fake_memory *m, uint32_t offset);  /* little-endian */
/* 1 when no byte of RAM is nonzero. */
int fake_memory_all_zero(const fake_memory *m);

#endif
