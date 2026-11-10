/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef PSXCHEAT_H
#define PSXCHEAT_H

/* psxcheat: a small, dependency-free PS1 GameShark / Action Replay core.
 *
 * Plain C99. No SDL, ImGui, recomp-ui, psxrecomp or game-specific
 * dependencies, no global state: every function is reentrant, and
 * separate programs may be used from separate threads.
 *
 * Pipeline:
 *   text --psxcheat_parse--> psxcheat_program --psxcheat_execute--> memory
 *
 * Supported code types (psx-spx, "PSX Gameshark Code Format"):
 *   30aaaaaa 00dd   8-bit write   [aaaaaa] = dd
 *   80aaaaaa dddd   16-bit write  [aaaaaa] = dddd (little-endian in RAM)
 *
 * Policy beyond the reference (deliberately strict; psx-spx does not specify
 * these points):
 *  - aaaaaa is a canonical offset into the 2 MiB PS1 main RAM: it must be
 *    below PSXCHEAT_RAM_SIZE. Mirrors and other regions are rejected, never
 *    folded. Mapping an offset to a guest address is the memory backend's job.
 *  - A 30 code's value must have 00 in its high byte.
 *  - A 80 code's offset must be even.
 *  - Xplorer/Xploder lines share first digits 3 and 8 but carry encryption and
 *    flags in the second digit (3t/8t, t != 0). They are rejected with
 *    PSXCHEAT_ERR_XPLORER, never decrypted or reinterpreted.
 *  - Every other code type (D0, D1, 50, ...) is rejected with
 *    PSXCHEAT_ERR_UNSUPPORTED_TYPE until it is implemented.
 *
 * Text format: one code per line, "TTAAAAAA VVVV" (8 hex digits, spaces or
 * tabs, 4 hex digits), either case. Blank lines are ignored. '#' or ';'
 * starts a comment that runs to the end of the line, on its own line or after
 * a code. Lines end in LF or CRLF; the last line may also end without one.
 * A CR that is not followed by LF (even at the end of the input) is an
 * invalid character. Anything else is a syntax error.
 *
 * Execution validates the complete program against the memory backend first
 * and writes nothing if any line is invalid; then it writes in source order.
 * A write the backend reports as failed stops execution at that line; earlier
 * writes of the same run remain (memory has no transactions), and the result
 * says how many were made. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits (bounded parsing) --------------------------------------------- */
#define PSXCHEAT_RAM_SIZE      0x00200000u  /* 2 MiB main RAM */
#define PSXCHEAT_MAX_TEXT      65536u       /* bytes of input text */
#define PSXCHEAT_MAX_LINE      256u         /* bytes per line, excluding EOL */
#define PSXCHEAT_MAX_CODES     256u         /* code lines per program */
#define PSXCHEAT_MESSAGE_SIZE  160u         /* error message buffer */

/* ---- Status ----------------------------------------------------------------- */
typedef enum psxcheat_status {
    PSXCHEAT_OK = 0,
    PSXCHEAT_ERR_ARGUMENT,          /* NULL pointer or malformed backend */
    PSXCHEAT_ERR_TEXT_TOO_LONG,     /* input exceeds PSXCHEAT_MAX_TEXT */
    PSXCHEAT_ERR_LINE_TOO_LONG,     /* a line exceeds PSXCHEAT_MAX_LINE */
    PSXCHEAT_ERR_TOO_MANY_CODES,    /* more than PSXCHEAT_MAX_CODES codes */
    PSXCHEAT_ERR_INVALID_CHAR,      /* NUL, control or non-ASCII byte */
    PSXCHEAT_ERR_SYNTAX,            /* not "TTAAAAAA VVVV" */
    PSXCHEAT_ERR_HEX,               /* a non-hex digit in a code field */
    PSXCHEAT_ERR_UNSUPPORTED_TYPE,  /* a code type this core does not run */
    PSXCHEAT_ERR_XPLORER,           /* Xplorer/Xploder (possibly encrypted) */
    PSXCHEAT_ERR_VALUE_WIDTH,       /* value does not fit the code's width */
    PSXCHEAT_ERR_ADDRESS_RANGE,     /* offset or span outside 2 MiB RAM */
    PSXCHEAT_ERR_ALIGNMENT,         /* 16-bit write at an odd offset */
    PSXCHEAT_ERR_INVALID_PROGRAM,   /* a program not produced by the parser */
    PSXCHEAT_ERR_MEMORY_REJECTED,   /* backend refused a span (validation) */
    PSXCHEAT_ERR_MEMORY_WRITE       /* backend reported a failed write */
} psxcheat_status;

typedef struct psxcheat_error {
    psxcheat_status status;
    uint32_t line;      /* 1-based source line, 0 when not line-specific */
    uint32_t column;    /* 1-based byte column, 0 when not applicable */
    char message[PSXCHEAT_MESSAGE_SIZE];  /* NUL-terminated, English */
} psxcheat_error;

/* Short constant name of a status, e.g. "unsupported code type". */
const char *psxcheat_status_name(psxcheat_status status);

/* ---- Validated program ------------------------------------------------------ */
/* Operations. Values are stable; new code types are appended. */
typedef enum psxcheat_op {
    PSXCHEAT_OP_WRITE8 = 1,
    PSXCHEAT_OP_WRITE16 = 2
} psxcheat_op;

typedef struct psxcheat_line {
    uint8_t op;           /* psxcheat_op */
    uint8_t code_type;    /* first byte as written (0x30, 0x80) */
    uint16_t value;
    uint32_t offset;      /* canonical RAM offset */
    uint32_t source_line; /* 1-based line in the parsed text */
} psxcheat_line;

typedef struct psxcheat_program {
    uint32_t count;
    psxcheat_line lines[PSXCHEAT_MAX_CODES];
} psxcheat_program;

/* Parse `len` bytes of `text` (not necessarily NUL-terminated). On success
 * fills `out` and returns PSXCHEAT_OK. On failure returns the error, fills
 * `err` (may be NULL) and leaves `out->count == 0`, so a failed parse can
 * never be executed by accident. A text with no codes parses to count 0. */
psxcheat_status psxcheat_parse(const char *text, size_t len,
                               psxcheat_program *out, psxcheat_error *err);

/* ---- Memory backend --------------------------------------------------------- */
/* All offsets are canonical RAM offsets (below PSXCHEAT_RAM_SIZE). Callbacks
 * return nonzero for success. `valid` must not have side effects. The read
 * callbacks are reserved for conditional code types and may be NULL until
 * those exist. */
typedef struct psxcheat_memory {
    void *ctx;
    int (*valid)(void *ctx, uint32_t offset, uint32_t width);
    int (*write8)(void *ctx, uint32_t offset, uint8_t value);
    int (*write16)(void *ctx, uint32_t offset, uint16_t value);
    int (*read8)(void *ctx, uint32_t offset, uint8_t *value);
    int (*read16)(void *ctx, uint32_t offset, uint16_t *value);
} psxcheat_memory;

/* ---- Execution -------------------------------------------------------------- */
typedef struct psxcheat_result {
    uint32_t writes;        /* writes the backend accepted in this run */
} psxcheat_result;

/* Check every line of `program` (its own invariants, then the backend's
 * `valid`) without writing. */
psxcheat_status psxcheat_validate(const psxcheat_program *program,
                                  const psxcheat_memory *memory,
                                  psxcheat_error *err);

/* psxcheat_validate, then apply every line in source order. `result` and
 * `err` may be NULL. */
psxcheat_status psxcheat_execute(const psxcheat_program *program,
                                 const psxcheat_memory *memory,
                                 psxcheat_result *result,
                                 psxcheat_error *err);

#ifdef __cplusplus
}
#endif

#endif
