/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_FILE_H
#define TS2_CHEAT_FILE_H

/* Cheat Menu (Phase 3C.2B): loader for ONE optional external cheat file
 * (TOML), turning it into ts2_cheat_external_input records and handing them
 * to the existing validator (ts2_cheat_external.h). C interface, C++17
 * implementation (ts2_cheat_file.cpp, toml11 from the psxrecomp submodule).
 *
 * File format (version 1):
 *
 *   schema  = "toystory2recomp.cheats"
 *   version = 1
 *   game    = "SLUS-00893"
 *
 *   [[cheat]]
 *   id          = "health-14"          # required string
 *   label       = "Full Health"        # required string
 *   description = "Keeps health at 14" # optional string
 *   code        = ["800B221E 000E"]    # required array of strings
 *   enabled     = false                # optional boolean, default false
 *
 * Trust boundary. The file is untrusted. It can never grant memory: it has
 * no range field, and every code line goes through the validator, whose
 * trusted external policy (TS2 source code) decides what may be written.
 * All code parsing, address permission, overlap, duplicate-id and final
 * acceptance decisions are the validator's; this loader only reads bytes
 * safely and checks the document's shape.
 *
 * Reading. At most TS2_CHEAT_FILE_MAX_BYTES. The file is opened once,
 * read-only and non-blocking; type and size are checked on that same handle
 * (fstat), so a path swapped after a check cannot be read instead. Only
 * regular files are read (a symlink to a regular file is fine; directories,
 * devices and FIFOs are refused without blocking). Reads loop until EOF and
 * retry when interrupted; more data than the limit is refused. The file is
 * never written, created, renamed or deleted. Windows: the UTF-8 path is
 * converted to UTF-16 (_wopen / _fstat64).
 *
 * Bytes. Strict UTF-8 (no overlong forms, surrogates or code points above
 * U+10FFFF), no NUL byte anywhere; zero or one leading UTF-8 BOM (removed),
 * two or more are refused. Before toml11 sees the text, a string- and
 * comment-aware scan bounds its recursion (toml11 has no depth limit; a
 * deep structure overflows the stack): bracket / brace nesting deeper than
 * TS2_CHEAT_FILE_MAX_DEPTH, or more than TS2_CHEAT_FILE_MAX_DOTS dots on
 * one line (dotted keys and dotted table headers nest one table per dot)
 * are refused. The schema needs neither dotted keys nor floats.
 *
 * Document. schema, version and game must match exactly; any other
 * top-level key is a file error. `cheat` is optional; when present it must
 * be an array (of tables). Each element is checked on its own: not a table,
 * an unknown key, a missing required key, a wrong type, a string holding a
 * NUL character, or a non-string code element rejects only that entry.
 * Entries beyond TS2_CHEAT_EXTERNAL_INPUT_MAX are not examined and read
 * TS2_CHEAT_EXT_ERR_CAPACITY.
 *
 * Session lifecycle (ts2_cheat_external.h). Loading builds the combined
 * table, so it is only allowed inside an unsealed, unreferenced session
 * window (between ts2_cheat_session_begin and ts2_cheat_engine_activate).
 * Otherwise the loader returns TS2_CHEAT_FILE_ERR_SEALED and changes
 * nothing -- not even the file is opened. When building is allowed, any
 * file-level error still builds the table with the built-in cheats only.
 *
 * Diagnostics: status enums and fixed texts only (ts2_cheat_file_describe /
 * _describe_entry); file content is never echoed. A syntax error reports
 * its line number at most.
 *
 * Threading: main thread, activation only (like combined_build). No C++
 * exception crosses this interface. */

#include <stddef.h>
#include <stdint.h>

#include "ts2_cheat_external.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TS2_CHEAT_FILE_MAX_BYTES 65536u
#define TS2_CHEAT_FILE_MAX_DEPTH 4u
#define TS2_CHEAT_FILE_MAX_DOTS  4u    /* unquoted '.' per line */
#define TS2_CHEAT_FILE_SCHEMA    "toystory2recomp.cheats"
#define TS2_CHEAT_FILE_VERSION   1
#define TS2_CHEAT_FILE_GAME      "SLUS-00893"

typedef enum ts2_cheat_file_status {
    TS2_CHEAT_FILE_OK = 0,          /* document valid; per-entry verdicts in the report */
    TS2_CHEAT_FILE_NONE,            /* no path given: built-ins only */
    TS2_CHEAT_FILE_ERR_SEALED,      /* table sealed/referenced: nothing done */
    TS2_CHEAT_FILE_ERR_OPEN,        /* cannot open (missing, permission, bad path) */
    TS2_CHEAT_FILE_ERR_NOT_REGULAR, /* directory, device, FIFO, ... */
    TS2_CHEAT_FILE_ERR_TOO_LARGE,   /* more than TS2_CHEAT_FILE_MAX_BYTES */
    TS2_CHEAT_FILE_ERR_READ,        /* read error */
    TS2_CHEAT_FILE_ERR_ENCODING,    /* NUL byte, invalid UTF-8, or more than one BOM */
    TS2_CHEAT_FILE_ERR_NESTING,     /* nesting / dotted keys beyond the limits */
    TS2_CHEAT_FILE_ERR_SYNTAX,      /* not valid TOML (incl. duplicate keys) */
    TS2_CHEAT_FILE_ERR_SCHEMA,      /* schema missing or different */
    TS2_CHEAT_FILE_ERR_VERSION,     /* version missing, not an integer, or not 1 */
    TS2_CHEAT_FILE_ERR_GAME,        /* game missing or different */
    TS2_CHEAT_FILE_ERR_STRUCTURE,   /* unknown top-level key, `cheat` not an array */
    TS2_CHEAT_FILE_ERR_INTERNAL     /* unexpected failure (exception caught) */
} ts2_cheat_file_status;

/* Shape of one [[cheat]] entry, checked by the loader. Only entries with
 * TS2_CHEAT_ENTRY_OK reach the validator. */
typedef enum ts2_cheat_entry_shape {
    TS2_CHEAT_ENTRY_OK = 0,
    TS2_CHEAT_ENTRY_NOT_EXAMINED,   /* file error, or beyond the input limit */
    TS2_CHEAT_ENTRY_NOT_TABLE,
    TS2_CHEAT_ENTRY_UNKNOWN_KEY,
    TS2_CHEAT_ENTRY_MISSING_KEY,    /* id, label or code missing */
    TS2_CHEAT_ENTRY_WRONG_TYPE,
    TS2_CHEAT_ENTRY_NUL_IN_STRING,
    TS2_CHEAT_ENTRY_CODE_ELEMENT    /* a code element is not a string */
} ts2_cheat_entry_shape;

typedef struct ts2_cheat_file_report {
    ts2_cheat_file_status file;
    ts2_cheat_build_status build;   /* the table build; ERR_SEALED when not attempted */
    size_t bytes;                   /* bytes read (before BOM removal) */
    size_t entries;                 /* elements of `cheat` (all, also unexamined) */
    size_t validated;               /* entries handed to the validator */
    size_t accepted;                /* entries the validator accepted */
    uint32_t error_line;            /* TOML syntax error line, 0 when unknown */
    ts2_cheat_entry_shape shape[TS2_CHEAT_EXTERNAL_INPUT_MAX];
    ts2_cheat_ext_status verdict[TS2_CHEAT_EXTERNAL_INPUT_MAX];  /* per file entry */
} ts2_cheat_file_report;

/* Load `path_utf8` (NULL or empty: TS2_CHEAT_FILE_NONE) and build the
 * combined table (built-ins always; accepted external entries appended).
 * `report` may be NULL. Never throws, never writes a file. */
ts2_cheat_file_status ts2_cheat_file_load(const char *path_utf8, ts2_cheat_file_report *report);

/* The same from memory (tests and fuzzing): `data`/`size` are the file's
 * bytes; the size limit applies. */
ts2_cheat_file_status ts2_cheat_file_load_buffer(const unsigned char *data, size_t size,
                                                 ts2_cheat_file_report *report);

const char *ts2_cheat_file_status_name(ts2_cheat_file_status status);
const char *ts2_cheat_entry_shape_name(ts2_cheat_entry_shape shape);

/* One-line summary / per-entry line from fixed texts and numbers only
 * (no file content). Return the length written (snprintf semantics). */
int ts2_cheat_file_describe(const ts2_cheat_file_report *report, char *out, size_t size);
int ts2_cheat_file_describe_entry(const ts2_cheat_file_report *report, size_t index,
                                  char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
