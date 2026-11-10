/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef PSXCHEAT_INTERNAL_H
#define PSXCHEAT_INTERNAL_H

/* Shared between the psxcheat sources; not part of the public API. */

#include "psxcheat.h"

#if defined(__GNUC__) || defined(__clang__)
#define PSXCHEAT_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define PSXCHEAT_PRINTF(fmt, args)
#endif

/* Fills `err` (when non-NULL) with the status, location and formatted
 * message, prefixed with "line N[, column C]: " when line is nonzero, and
 * returns `status`. */
psxcheat_status psxcheat_fail(psxcheat_error *err, psxcheat_status status,
                              uint32_t line, uint32_t column,
                              const char *fmt, ...) PSXCHEAT_PRINTF(5, 6);

#endif
