// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// SPDX-FileCopyrightText: 2026 PeriBluGaming
//
// Cheat Menu (Phase 3C.2B): external cheat file loader. See ts2_cheat_file.h
// for the format, the trust boundary and the guarantees. Nothing in here may
// let a C++ exception escape: every entry point catches everything.

#include "ts2_cheat_file.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
// NOT VERIFIED: no Windows toolchain was available when this was written
// (Phase 3C.2B). NOMINMAX keeps <windows.h> from defining min/max macros,
// which would break toml11's std::numeric_limits<...>::max() below.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "toml.hpp"

namespace {

// ---- Reading -----------------------------------------------------------------

#if defined(_WIN32)
struct FileHandle {
    int fd = -1;
    ~FileHandle() { if (fd >= 0) _close(fd); }
};

ts2_cheat_file_status read_file(const char *path, std::vector<unsigned char> &out)
{
    const int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
    if (wlen <= 0) return TS2_CHEAT_FILE_ERR_OPEN;
    std::vector<wchar_t> wpath(static_cast<size_t>(wlen));
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wpath.data(), wlen) != wlen)
        return TS2_CHEAT_FILE_ERR_OPEN;
    FileHandle h;
    h.fd = _wopen(wpath.data(), _O_RDONLY | _O_BINARY);
    if (h.fd < 0) return TS2_CHEAT_FILE_ERR_OPEN;
    struct _stat64 st;
    if (_fstat64(h.fd, &st) != 0) return TS2_CHEAT_FILE_ERR_READ;
    if ((st.st_mode & _S_IFMT) != _S_IFREG) return TS2_CHEAT_FILE_ERR_NOT_REGULAR;
    if (st.st_size < 0 || static_cast<unsigned long long>(st.st_size) > TS2_CHEAT_FILE_MAX_BYTES)
        return TS2_CHEAT_FILE_ERR_TOO_LARGE;
    out.assign(TS2_CHEAT_FILE_MAX_BYTES + 1u, 0);
    size_t got = 0;
    while (got < out.size()) {
        const int r = _read(h.fd, out.data() + got, static_cast<unsigned>(out.size() - got));
        if (r < 0) {
            if (errno == EINTR) continue;
            return TS2_CHEAT_FILE_ERR_READ;
        }
        if (r == 0) break;
        got += static_cast<size_t>(r);
    }
    if (got > TS2_CHEAT_FILE_MAX_BYTES) return TS2_CHEAT_FILE_ERR_TOO_LARGE;
    out.resize(got);
    return TS2_CHEAT_FILE_OK;
}
#else
struct FileHandle {
    int fd = -1;
    ~FileHandle() { if (fd >= 0) ::close(fd); }
};

ts2_cheat_file_status read_file(const char *path, std::vector<unsigned char> &out)
{
    int flags = O_RDONLY | O_NONBLOCK;   // a FIFO must not block the game
#if defined(O_CLOEXEC)
    flags |= O_CLOEXEC;
#endif
#if defined(O_NOCTTY)
    flags |= O_NOCTTY;
#endif
    FileHandle h;
    do {
        h.fd = ::open(path, flags);
    } while (h.fd < 0 && errno == EINTR);
    if (h.fd < 0) return TS2_CHEAT_FILE_ERR_OPEN;
    struct stat st;
    if (::fstat(h.fd, &st) != 0) return TS2_CHEAT_FILE_ERR_READ;   // the SAME handle
    if (!S_ISREG(st.st_mode)) return TS2_CHEAT_FILE_ERR_NOT_REGULAR;
    if (st.st_size < 0 || static_cast<unsigned long long>(st.st_size) > TS2_CHEAT_FILE_MAX_BYTES)
        return TS2_CHEAT_FILE_ERR_TOO_LARGE;
    // Read up to one byte past the limit: a file that grew after fstat is
    // refused, never truncated.
    out.assign(TS2_CHEAT_FILE_MAX_BYTES + 1u, 0);
    size_t got = 0;
    while (got < out.size()) {
        const ssize_t r = ::read(h.fd, out.data() + got, out.size() - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return TS2_CHEAT_FILE_ERR_READ;
        }
        if (r == 0) break;
        got += static_cast<size_t>(r);
    }
    if (got > TS2_CHEAT_FILE_MAX_BYTES) return TS2_CHEAT_FILE_ERR_TOO_LARGE;
    out.resize(got);
    return TS2_CHEAT_FILE_OK;
}
#endif

// ---- Bytes ---------------------------------------------------------------------

bool is_bom(const unsigned char *s, size_t n)
{
    return n >= 3 && s[0] == 0xEF && s[1] == 0xBB && s[2] == 0xBF;
}

// Strict UTF-8, no NUL, at most ONE leading BOM (a second one is refused:
// toml11 would silently skip it too). On success *start skips the BOM.
bool valid_utf8(const unsigned char *s, size_t n, size_t *start)
{
    size_t i = 0;
    if (is_bom(s, n)) i = 3;
    if (i && is_bom(s + i, n - i)) return false;
    *start = i;
    while (i < n) {
        const unsigned c = s[i];
        if (c == 0x00) return false;
        if (c < 0x80) { ++i; continue; }
        size_t len;
        unsigned cp;
        if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1Fu; }
        else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0Fu; }
        else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07u; }
        else return false;                  // continuation byte, C0/C1, F5..FF
        if (n - i < len) return false;      // truncated
        for (size_t k = 1; k < len; ++k) {
            const unsigned cc = s[i + k];
            if ((cc & 0xC0u) != 0x80u) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((len == 3 && cp < 0x800u) || (len == 4 && cp < 0x10000u)) return false;  // overlong
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false;                             // surrogate
        if (cp > 0x10FFFFu) return false;
        i += len;
    }
    return true;
}

// Bounds toml11's recursion before it runs (it has no depth limit; a deep
// structure overflows the stack even in its copy constructor). Two ways to
// nest, both counted outside strings and comments (TOML lexical rules;
// toml11 still judges the syntax):
//   - '[' / '{' depth (arrays, inline tables, table headers) <= max_depth;
//   - '.' per line <= max_dots: dotted keys and dotted table headers
//     (a.b.c = 1, [a.b.c]) nest one table per dot, and a key or header
//     cannot span lines. The schema needs neither dotted keys nor floats.
bool nesting_ok(const unsigned char *s, size_t n, unsigned max_depth, unsigned max_dots)
{
    enum { NORMAL, COMMENT, BASIC, LITERAL, ML_BASIC, ML_LITERAL } st = NORMAL;
    unsigned depth = 0, dots = 0;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = s[i];
        const bool three_dq = i + 2 < n && s[i + 1] == '"' && s[i + 2] == '"';
        const bool three_sq = i + 2 < n && s[i + 1] == '\'' && s[i + 2] == '\'';
        if (c == '\n') dots = 0;
        switch (st) {
        case NORMAL:
            if (c == '.') { if (++dots > max_dots) return false; }
            else if (c == '#') st = COMMENT;
            else if (c == '"') { if (three_dq) { st = ML_BASIC; i += 2; } else st = BASIC; }
            else if (c == '\'') { if (three_sq) { st = ML_LITERAL; i += 2; } else st = LITERAL; }
            else if (c == '[' || c == '{') { if (++depth > max_depth) return false; }
            else if ((c == ']' || c == '}') && depth > 0) --depth;
            break;
        case COMMENT:
            if (c == '\n') st = NORMAL;
            break;
        case BASIC:
            if (c == '\\') ++i;
            else if (c == '"' || c == '\n') st = NORMAL;
            break;
        case LITERAL:
            if (c == '\'' || c == '\n') st = NORMAL;
            break;
        case ML_BASIC:
            if (c == '\\') ++i;
            else if (c == '"' && three_dq) { st = NORMAL; i += 2; }
            break;
        case ML_LITERAL:
            if (c == '\'' && three_sq) { st = NORMAL; i += 2; }
            break;
        }
    }
    return true;
}

// ---- Document --------------------------------------------------------------------

struct Entry {
    std::string id, label, desc;
    bool has_desc = false;
    std::vector<std::string> lines;
    bool enabled = false;
};

bool has_nul(const std::string &s) { return s.find('\0') != std::string::npos; }

// Required/optional string member of an entry table.
ts2_cheat_entry_shape get_string(const toml::value::table_type &t, const char *key,
                                 bool required, std::string &out, bool *present)
{
    const auto it = t.find(key);
    if (present) *present = it != t.end();
    if (it == t.end()) return required ? TS2_CHEAT_ENTRY_MISSING_KEY : TS2_CHEAT_ENTRY_OK;
    if (!it->second.is_string()) return TS2_CHEAT_ENTRY_WRONG_TYPE;
    out = it->second.as_string().str;
    return has_nul(out) ? TS2_CHEAT_ENTRY_NUL_IN_STRING : TS2_CHEAT_ENTRY_OK;
}

ts2_cheat_entry_shape check_entry(const toml::value &v, Entry &e)
{
    static const char *const allowed[] = { "id", "label", "description", "code", "enabled" };
    if (!v.is_table()) return TS2_CHEAT_ENTRY_NOT_TABLE;
    const auto &t = v.as_table();
    for (const auto &kv : t) {
        bool known = false;
        for (const char *a : allowed) known = known || kv.first == a;
        if (!known) return TS2_CHEAT_ENTRY_UNKNOWN_KEY;
    }
    ts2_cheat_entry_shape s;
    if ((s = get_string(t, "id", true, e.id, nullptr)) != TS2_CHEAT_ENTRY_OK) return s;
    if ((s = get_string(t, "label", true, e.label, nullptr)) != TS2_CHEAT_ENTRY_OK) return s;
    if ((s = get_string(t, "description", false, e.desc, &e.has_desc)) != TS2_CHEAT_ENTRY_OK)
        return s;
    const auto code = t.find("code");
    if (code == t.end()) return TS2_CHEAT_ENTRY_MISSING_KEY;
    if (!code->second.is_array()) return TS2_CHEAT_ENTRY_WRONG_TYPE;
    const auto &lines = code->second.as_array();
    // Lines are counted by the validator (1..TS2_CHEAT_EXT_LINES_MAX); copy
    // one more than allowed at most so an oversized array is still refused.
    for (const auto &l : lines) {
        if (!l.is_string()) return TS2_CHEAT_ENTRY_CODE_ELEMENT;
        if (e.lines.size() > TS2_CHEAT_EXT_LINES_MAX) break;
        e.lines.push_back(l.as_string().str);
        if (has_nul(e.lines.back())) return TS2_CHEAT_ENTRY_NUL_IN_STRING;
    }
    const auto en = t.find("enabled");
    if (en != t.end()) {
        if (!en->second.is_boolean()) return TS2_CHEAT_ENTRY_WRONG_TYPE;
        e.enabled = en->second.as_boolean();
    }
    return TS2_CHEAT_ENTRY_OK;
}

// Header keys and the `cheat` array. Fills entries and shapes.
ts2_cheat_file_status check_document(const toml::value &root, std::vector<Entry> &entries,
                                     ts2_cheat_file_report &r)
{
    if (!root.is_table()) return TS2_CHEAT_FILE_ERR_STRUCTURE;
    const auto &t = root.as_table();
    for (const auto &kv : t)
        if (kv.first != "schema" && kv.first != "version" && kv.first != "game" &&
            kv.first != "cheat")
            return TS2_CHEAT_FILE_ERR_STRUCTURE;
    const auto schema = t.find("schema");
    if (schema == t.end() || !schema->second.is_string() ||
        schema->second.as_string().str != TS2_CHEAT_FILE_SCHEMA)
        return TS2_CHEAT_FILE_ERR_SCHEMA;
    const auto version = t.find("version");
    if (version == t.end() || !version->second.is_integer() ||
        version->second.as_integer() != TS2_CHEAT_FILE_VERSION)
        return TS2_CHEAT_FILE_ERR_VERSION;
    const auto game = t.find("game");
    if (game == t.end() || !game->second.is_string() ||
        game->second.as_string().str != TS2_CHEAT_FILE_GAME)
        return TS2_CHEAT_FILE_ERR_GAME;
    const auto cheat = t.find("cheat");
    if (cheat == t.end()) return TS2_CHEAT_FILE_OK;           // header only: no entries
    if (!cheat->second.is_array()) return TS2_CHEAT_FILE_ERR_STRUCTURE;
    const auto &arr = cheat->second.as_array();
    r.entries = arr.size();
    const size_t examined = arr.size() < TS2_CHEAT_EXTERNAL_INPUT_MAX ? arr.size()
                                                                      : TS2_CHEAT_EXTERNAL_INPUT_MAX;
    entries.resize(examined);
    for (size_t i = 0; i < examined; ++i) r.shape[i] = check_entry(arr[i], entries[i]);
    return TS2_CHEAT_FILE_OK;
}

void report_init(ts2_cheat_file_report &r)
{
    std::memset(&r, 0, sizeof r);
    r.file = TS2_CHEAT_FILE_ERR_INTERNAL;
    r.build = TS2_CHEAT_BUILD_ERR_SEALED;
    for (size_t i = 0; i < TS2_CHEAT_EXTERNAL_INPUT_MAX; ++i) {
        r.shape[i] = TS2_CHEAT_ENTRY_NOT_EXAMINED;
        r.verdict[i] = TS2_CHEAT_EXT_ERR_NOT_CHECKED;
    }
}

bool build_allowed() { return !ts2_cheat_session_sealed() && ts2_cheat_table_refs() == 0u; }

// Built-ins only (any file error while building is allowed).
void build_builtins_only(ts2_cheat_file_report &r)
{
    for (size_t i = 0; i < TS2_CHEAT_EXTERNAL_INPUT_MAX; ++i)
        if (r.verdict[i] == TS2_CHEAT_EXT_OK) r.verdict[i] = TS2_CHEAT_EXT_ERR_NOT_CHECKED;
    r.validated = 0;
    r.accepted = 0;
    r.build = ts2_cheat_combined_build(nullptr, 0, nullptr);
}

// Parse bytes and build. Caller holds a report and has checked build_allowed().
void load_bytes(const unsigned char *data, size_t size, ts2_cheat_file_report &r)
{
    r.bytes = size;
    if (size > TS2_CHEAT_FILE_MAX_BYTES) { r.file = TS2_CHEAT_FILE_ERR_TOO_LARGE; return; }
    size_t start = 0;
    if (!valid_utf8(data, size, &start)) { r.file = TS2_CHEAT_FILE_ERR_ENCODING; return; }
    if (!nesting_ok(data + start, size - start, TS2_CHEAT_FILE_MAX_DEPTH,
                    TS2_CHEAT_FILE_MAX_DOTS)) {
        r.file = TS2_CHEAT_FILE_ERR_NESTING;
        return;
    }
    toml::value root;
    try {
        std::istringstream in(std::string(reinterpret_cast<const char *>(data) + start, size - start));
        root = toml::parse(in, "cheat file");
    } catch (const toml::exception &e) {
        r.file = TS2_CHEAT_FILE_ERR_SYNTAX;
        r.error_line = static_cast<uint32_t>(e.location().line());
        return;
    }
    std::vector<Entry> entries;
    r.file = check_document(root, entries, r);
    if (r.file != TS2_CHEAT_FILE_OK) {
        for (size_t i = 0; i < TS2_CHEAT_EXTERNAL_INPUT_MAX; ++i) r.shape[i] = TS2_CHEAT_ENTRY_NOT_EXAMINED;
        r.entries = 0;
        return;
    }
    // Inputs for the validator: shape-valid entries only. `entries` is not
    // changed any more, so the pointers below stay valid for the build call.
    std::vector<std::vector<const char *>> line_ptrs(entries.size());
    std::vector<ts2_cheat_external_input> inputs;
    std::vector<size_t> origin;     // input index -> file entry index
    inputs.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        if (r.shape[i] != TS2_CHEAT_ENTRY_OK) continue;
        const Entry &e = entries[i];
        for (const std::string &l : e.lines) line_ptrs[i].push_back(l.c_str());
        ts2_cheat_external_input in;
        std::memset(&in, 0, sizeof in);
        in.id = e.id.c_str();
        in.label = e.label.c_str();
        in.description = e.has_desc ? e.desc.c_str() : nullptr;
        in.code_lines = line_ptrs[i].empty() ? nullptr : line_ptrs[i].data();
        in.code_line_count = line_ptrs[i].size();
        in.enabled = e.enabled ? 1 : 0;
        inputs.push_back(in);
        origin.push_back(i);
    }
    std::vector<ts2_cheat_ext_status> verdicts(inputs.size(), TS2_CHEAT_EXT_ERR_NOT_CHECKED);
    r.build = ts2_cheat_combined_build(inputs.empty() ? nullptr : inputs.data(), inputs.size(),
                                       verdicts.empty() ? nullptr : verdicts.data());
    r.validated = inputs.size();
    for (size_t k = 0; k < inputs.size(); ++k) {
        r.verdict[origin[k]] = verdicts[k];
        if (verdicts[k] == TS2_CHEAT_EXT_OK) ++r.accepted;
    }
    // Entries beyond the input limit were never looked at.
    for (size_t i = entries.size(); i < r.entries && i < TS2_CHEAT_EXTERNAL_INPUT_MAX; ++i)
        r.verdict[i] = TS2_CHEAT_EXT_ERR_CAPACITY;
}

// Common driver: lifecycle gate, load, fallback, exception barrier.
template <typename LoadFn>
ts2_cheat_file_status run(ts2_cheat_file_report *out, LoadFn &&load) noexcept
{
    ts2_cheat_file_report local;
    ts2_cheat_file_report &r = out ? *out : local;
    report_init(r);
    if (!build_allowed()) {             // the engine or the menu uses the table
        r.file = TS2_CHEAT_FILE_ERR_SEALED;
        return r.file;
    }
    try {
        load(r);
    } catch (...) {
        r.file = TS2_CHEAT_FILE_ERR_INTERNAL;
    }
    if (r.file != TS2_CHEAT_FILE_OK) build_builtins_only(r);   // C call: cannot throw
    return r.file;
}

}  // namespace

extern "C" ts2_cheat_file_status ts2_cheat_file_load(const char *path_utf8,
                                                     ts2_cheat_file_report *report)
{
    return run(report, [path_utf8](ts2_cheat_file_report &r) {
        if (!path_utf8 || !path_utf8[0]) {
            r.file = TS2_CHEAT_FILE_NONE;
            return;
        }
        std::vector<unsigned char> bytes;
        r.file = read_file(path_utf8, bytes);
        if (r.file != TS2_CHEAT_FILE_OK) return;
        load_bytes(bytes.data(), bytes.size(), r);
    });
}

extern "C" ts2_cheat_file_status ts2_cheat_file_load_buffer(const unsigned char *data, size_t size,
                                                            ts2_cheat_file_report *report)
{
    return run(report, [data, size](ts2_cheat_file_report &r) {
        if (!data && size) { r.file = TS2_CHEAT_FILE_ERR_READ; return; }
        static const unsigned char empty = 0;
        load_bytes(data ? data : &empty, size, r);
    });
}

extern "C" const char *ts2_cheat_file_status_name(ts2_cheat_file_status s)
{
    switch (s) {
    case TS2_CHEAT_FILE_OK: return "ok";
    case TS2_CHEAT_FILE_NONE: return "no file";
    case TS2_CHEAT_FILE_ERR_SEALED: return "cheat table in use (not loaded)";
    case TS2_CHEAT_FILE_ERR_OPEN: return "cannot open";
    case TS2_CHEAT_FILE_ERR_NOT_REGULAR: return "not a regular file";
    case TS2_CHEAT_FILE_ERR_TOO_LARGE: return "file too large";
    case TS2_CHEAT_FILE_ERR_READ: return "read error";
    case TS2_CHEAT_FILE_ERR_ENCODING: return "invalid encoding";
    case TS2_CHEAT_FILE_ERR_NESTING: return "nesting too deep";
    case TS2_CHEAT_FILE_ERR_SYNTAX: return "TOML syntax error";
    case TS2_CHEAT_FILE_ERR_SCHEMA: return "wrong or missing schema";
    case TS2_CHEAT_FILE_ERR_VERSION: return "unsupported version";
    case TS2_CHEAT_FILE_ERR_GAME: return "wrong or missing game";
    case TS2_CHEAT_FILE_ERR_STRUCTURE: return "unexpected document structure";
    case TS2_CHEAT_FILE_ERR_INTERNAL: return "internal error";
    }
    return "unknown";
}

extern "C" const char *ts2_cheat_entry_shape_name(ts2_cheat_entry_shape s)
{
    switch (s) {
    case TS2_CHEAT_ENTRY_OK: return "ok";
    case TS2_CHEAT_ENTRY_NOT_EXAMINED: return "not examined";
    case TS2_CHEAT_ENTRY_NOT_TABLE: return "not a table";
    case TS2_CHEAT_ENTRY_UNKNOWN_KEY: return "unknown key";
    case TS2_CHEAT_ENTRY_MISSING_KEY: return "missing required key";
    case TS2_CHEAT_ENTRY_WRONG_TYPE: return "wrong value type";
    case TS2_CHEAT_ENTRY_NUL_IN_STRING: return "NUL character in string";
    case TS2_CHEAT_ENTRY_CODE_ELEMENT: return "code element is not a string";
    }
    return "unknown";
}

extern "C" int ts2_cheat_file_describe(const ts2_cheat_file_report *r, char *out, size_t size)
{
    if (!out || size == 0) return 0;
    if (!r) return std::snprintf(out, size, "cheat file: no report");
    if (r->file == TS2_CHEAT_FILE_ERR_SYNTAX && r->error_line)
        return std::snprintf(out, size, "cheat file: %s (line %u)",
                             ts2_cheat_file_status_name(r->file), static_cast<unsigned>(r->error_line));
    if (r->file != TS2_CHEAT_FILE_OK)
        return std::snprintf(out, size, "cheat file: %s", ts2_cheat_file_status_name(r->file));
    return std::snprintf(out, size, "cheat file: ok, %u entries, %u accepted, %u rejected",
                         static_cast<unsigned>(r->entries), static_cast<unsigned>(r->accepted),
                         static_cast<unsigned>(r->entries - r->accepted));
}

extern "C" int ts2_cheat_file_describe_entry(const ts2_cheat_file_report *r, size_t index,
                                             char *out, size_t size)
{
    if (!out || size == 0) return 0;
    if (!r || index >= r->entries) return std::snprintf(out, size, "cheat entry: none");
    const unsigned n = static_cast<unsigned>(index + 1);
    if (index >= TS2_CHEAT_EXTERNAL_INPUT_MAX)
        return std::snprintf(out, size, "cheat entry %u: %s", n,
                             ts2_cheat_ext_status_name(TS2_CHEAT_EXT_ERR_CAPACITY));
    if (r->shape[index] != TS2_CHEAT_ENTRY_OK)
        return std::snprintf(out, size, "cheat entry %u: %s", n,
                             ts2_cheat_entry_shape_name(r->shape[index]));
    return std::snprintf(out, size, "cheat entry %u: %s", n,
                         ts2_cheat_ext_status_name(r->verdict[index]));
}
