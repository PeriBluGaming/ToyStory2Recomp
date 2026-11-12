/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
/* Phase 3C.4B-1: Advanced trust tier for external Action Replay cheats.
 * The fixed Advanced region, the validator in both modes, the engine's own
 * tier check and start state, and the guest writes that result. External
 * entries are synthetic in-memory records (plus one TOML buffer through the
 * loader, which must stay Safe). Real validator, policies, engine and
 * psxcheat adapter; psxrecomp services are stubs over a fake 2 MiB RAM.
 * No guest memory, no file. */
#include "plugin_stub.h"
#include "psx_runtime_stub.h"
#include "ts2_cheat_engine.h"
#include "ts2_cheat_external.h"
#include "ts2_cheat_file.h"
#include "ts2_cheat_memory.h"
#include "ts2_cheat_table.h"

#include <stdio.h>
#include <string.h>

static int g_checks, g_failures;
static const char *g_test = "";

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            printf("  FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond); \
        }                                                                    \
    } while (0)

#define OFF(addr) ((addr) - 0x80000000u)
#define LINES(...) ((const char *const[]){ __VA_ARGS__ })
#define NLINES(...) (sizeof((const char *const[]){ __VA_ARGS__ }) / sizeof(const char *))
#define R(a, b) ((psxcheat_psx_range){ (a), (b) })

static ts2_cheat_ext_status g_st[8];
static ts2_cheat_trust g_tr[8];

static ts2_cheat_external_input mk(const char *id, const char *const *lines, size_t n, int on)
{
    ts2_cheat_external_input in;
    memset(&in, 0, sizeof in);
    in.id = id;
    in.label = id;
    in.code_lines = lines;
    in.code_line_count = n;
    in.enabled = on;
    return in;
}

/* A fresh process: no engine, no combined table, clean RAM. */
static void fresh(const char *name)
{
    g_test = name;
    ts2_cheat_engine_shutdown();
    ts2_cheat_combined_reset();
    stub_reset();
    memset(g_st, 0, sizeof g_st);
    memset(g_tr, 0, sizeof g_tr);
}

static ts2_cheat_build_status build(ts2_cheat_ext_mode mode, const ts2_cheat_external_input *in,
                                    size_t n)
{
    ts2_cheat_engine_shutdown();
    ts2_cheat_combined_reset();
    CHECK(ts2_cheat_session_begin() == 1);
    return ts2_cheat_combined_build_mode(in, n, mode, g_st, g_tr);
}

/* One single-line entry in its own build: its verdict. */
static ts2_cheat_ext_status one_line(ts2_cheat_ext_mode mode, const char *line)
{
    const char *lines[1];
    ts2_cheat_external_input in;
    lines[0] = line;
    in = mk("x", lines, 1, 1);
    CHECK(build(mode, &in, 1) == TS2_CHEAT_BUILD_OK);
    return g_st[0];
}

static size_t active_count(void)
{
    size_t n = 0;
    (void)ts2_cheat_active_table(&n);
    return n;
}

static const ts2_cheat_def *active_at(size_t k)
{
    static const ts2_cheat_def empty;
    size_t n = 0;
    const ts2_cheat_def *t = ts2_cheat_active_table(&n);
    return t && k < n ? &t[k] : &empty;
}

static int ram_zero(uint32_t addr, uint32_t len)
{
    uint32_t k;
    for (k = 0; k < len; ++k)
        if (stub_u8(OFF(addr + k)) != 0) return 0;
    return 1;
}

/* ------------------------------------------------------------------------- */

static void test_region(void)
{
    size_t n = 0;
    const psxcheat_psx_range *r = ts2_cheat_advanced_region(&n);
    fresh("region");
    CHECK(n == 1u && r && r[0].start == 0x800A04C8u && r[0].end == 0x800D0000u);
    CHECK(ts2_cheat_advanced_region(NULL) == r);
    /* Inside: first byte, first halfword, last halfword, last byte, all. */
    CHECK(ts2_cheat_advanced_range_permitted(R(0x800A04C8u, 0x800A04C9u)));
    CHECK(ts2_cheat_advanced_range_permitted(R(0x800A04C8u, 0x800A04CAu)));
    CHECK(ts2_cheat_advanced_range_permitted(R(0x800CFFFEu, 0x800D0000u)));
    CHECK(ts2_cheat_advanced_range_permitted(R(0x800CFFFFu, 0x800D0000u)));
    CHECK(ts2_cheat_advanced_range_permitted(R(0x800A04C8u, 0x800D0000u)));
    /* Outside or straddling; empty and inverted ranges. */
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x800A04C6u, 0x800A04C8u)));
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x800A04C7u, 0x800A04C9u)));
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x800CFFFFu, 0x800D0001u)));
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x800D0000u, 0x800D0002u)));
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x800C1618u, 0x800C1618u)));
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x800C161Au, 0x800C1618u)));
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x00000000u, 0x00000002u)));
    CHECK(!ts2_cheat_advanced_range_permitted(R(0x000C1618u, 0x000C161Au)));   /* KUSEG mirror */
    CHECK(!ts2_cheat_advanced_range_permitted(R(0xA00C1618u, 0xA00C161Au)));   /* KSEG1 mirror */
    /* The region is not the verified list: the Safe rules are unchanged. */
    CHECK(!ts2_cheat_range_permitted(R(0x800C1618u, 0x800C1626u)));
    CHECK(ts2_cheat_config_valid() == 1);
    {
        size_t nv = 0, np = 0;
        (void)ts2_cheat_writable_ranges(&nv);
        (void)ts2_cheat_external_policy(&np);
        CHECK(nv == 3u && np == 3u);
    }
}

static void test_rejections(void)
{
    const ts2_cheat_ext_mode A = TS2_CHEAT_EXT_MODE_ADVANCED;
    fresh("rejections");
    /* Kernel, EXE code, .rodata/.data gap. */
    CHECK(one_line(A, "80000010 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(one_line(A, "8000FFFE 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(one_line(A, "80010000 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(one_line(A, "80082D60 0001") == TS2_CHEAT_EXT_ERR_TARGET);   /* entry point */
    CHECK(one_line(A, "80097DC8 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(one_line(A, "800A04C6 0001") == TS2_CHEAT_EXT_ERR_TARGET);   /* just below */
    CHECK(one_line(A, "300A04C7 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    /* Region edges accepted. */
    CHECK(one_line(A, "800A04C8 0001") == TS2_CHEAT_EXT_OK && g_tr[0] == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(one_line(A, "300A04C8 0001") == TS2_CHEAT_EXT_OK);
    CHECK(one_line(A, "800CFFFE 0001") == TS2_CHEAT_EXT_OK);
    CHECK(one_line(A, "300CFFFF 0001") == TS2_CHEAT_EXT_OK);
    /* BSS tail, overlays, heap, stack. */
    CHECK(one_line(A, "800D0000 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(one_line(A, "300D0000 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(one_line(A, "800D12C0 0001") == TS2_CHEAT_EXT_ERR_TARGET);   /* level overlay */
    CHECK(one_line(A, "800D5D20 0001") == TS2_CHEAT_EXT_ERR_TARGET);   /* memcard / FMV */
    CHECK(one_line(A, "80100000 0001") == TS2_CHEAT_EXT_ERR_TARGET);   /* heap */
    CHECK(one_line(A, "801F8000 0001") == TS2_CHEAT_EXT_ERR_TARGET);   /* stack */
    CHECK(one_line(A, "801FFFF0 0001") == TS2_CHEAT_EXT_ERR_TARGET);
    /* Not RAM, misaligned, unsupported or Xploder: never parsed. */
    CHECK(one_line(A, "80200000 0001") == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(one_line(A, "800A04C9 0001") == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(one_line(A, "300C1618 0100") == TS2_CHEAT_EXT_ERR_PARSE);   /* 8-bit value too wide */
    CHECK(one_line(A, "D00C1618 0001") == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(one_line(A, "E00C1618 0001") == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(one_line(A, "100C1618 0001") == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(one_line(A, "50000702 0002") == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(one_line(A, "C0000000 0000") == TS2_CHEAT_EXT_ERR_PARSE);
    CHECK(one_line(A, "810C1618 0001") == TS2_CHEAT_EXT_ERR_PARSE);   /* Xploder */
    CHECK(one_line(A, "800C1618 1F1F 00") == TS2_CHEAT_EXT_ERR_PARSE);
    /* Every rejected entry leaves the table at the built-ins. */
    (void)one_line(A, "800D12C0 0001");
    CHECK(active_count() == 2u && ts2_cheat_active_external_count() == 0u);
    CHECK(g_tr[0] == TS2_CHEAT_TRUST_SAFE);
}

static void test_writes_8_16(void)
{
    ts2_cheat_external_input in[2];
    in[0] = mk("byte", LINES("300C1640 00AB"), 1, 1);
    in[1] = mk("half", LINES("800C1642 BEEF"), 1, 1);
    fresh("8 and 16 bit");
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, in, 2) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_st[1] == TS2_CHEAT_EXT_OK);
    CHECK(g_tr[0] == TS2_CHEAT_TRUST_ADVANCED && g_tr[1] == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(active_count() == 4u);
    CHECK(active_at(2)->range_count == 1u && active_at(2)->ranges[0].start == 0x800C1640u &&
          active_at(2)->ranges[0].end == 0x800C1641u);
    CHECK(active_at(3)->ranges[0].start == 0x800C1642u && active_at(3)->ranges[0].end == 0x800C1644u);
    CHECK(ts2_cheat_active_trust(2) == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(ts2_cheat_active_trust(0) == TS2_CHEAT_TRUST_SAFE);
    CHECK(ts2_cheat_active_trust(99) == TS2_CHEAT_TRUST_SAFE);
    ts2_cheat_engine_activate();
    /* Off at start despite enabled = 1; nothing written. */
    CHECK(ts2_cheat_engine_enabled_at(2) == 0 && ts2_cheat_engine_enabled_at(3) == 0);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 0u);
    /* Explicitly switched on: the exact bytes, nothing else. */
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1 && ts2_cheat_engine_set_enabled_at(3, 1) == 1);
    ts2_cheat_engine_vblank();
    CHECK(stub_u8(OFF(0x800C1640u)) == 0xAB && stub_u16(OFF(0x800C1642u)) == 0xBEEF);
    CHECK(stub_u8(OFF(0x800C1641u)) == 0 && ram_zero(0x800C1644u, 4));
    CHECK(g_stub.write_calls == 2u && g_stub.bad_addresses == 0u);
    CHECK(ts2_cheat_engine_state_at(2) == TS2_CHEAT_ACTIVE && ts2_cheat_engine_state_at(3) == TS2_CHEAT_ACTIVE);
}

static void test_multiline_tokens(void)
{
    ts2_cheat_external_input in = mk("all-tokens",
        LINES("800C1618 1F1F", "800C161A 1F1F", "800C161C 1F1F", "800C161E 1F1F",
              "800C1620 1F1F", "800C1622 1F1F", "800C1624 1F1F"), 7, 1);
    uint32_t a;
    fresh("multi-line");
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, &in, 1) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_tr[0] == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(active_at(2)->range_count == 1u);
    CHECK(active_at(2)->ranges[0].start == 0x800C1618u && active_at(2)->ranges[0].end == 0x800C1626u);
    ts2_cheat_engine_activate();
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);
    ts2_cheat_engine_vblank();
    for (a = 0x800C1618u; a < 0x800C1626u; ++a) CHECK(stub_u8(OFF(a)) == 0x1F);
    CHECK(stub_u8(OFF(0x800C1617u)) == 0 && stub_u8(OFF(0x800C1626u)) == 0);
    CHECK(stub_u8(OFF(0x800C1627u)) == 0);
    CHECK(g_stub.write_calls == 7u);
    /* One switch for the whole program. */
    CHECK(ts2_cheat_engine_set_enabled_at(2, 0) == 1);
    stub_reset();
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 0u);
}

static void test_duplicate_lines(void)
{
    /* The submitted variant: 800C1620 twice, 800C1622 missing. */
    ts2_cheat_external_input in = mk("tokens-as-submitted",
        LINES("800C1618 1F1F", "800C161A 1F1F", "800C161C 1F1F", "800C161E 1F1F",
              "800C1620 1F1F", "800C1620 1F1F", "800C1624 1F1F"), 7, 0);
    fresh("duplicate lines");
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, &in, 1) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_tr[0] == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(active_at(2)->range_count == 2u);
    CHECK(active_at(2)->ranges[0].start == 0x800C1618u && active_at(2)->ranges[0].end == 0x800C1622u);
    CHECK(active_at(2)->ranges[1].start == 0x800C1624u && active_at(2)->ranges[1].end == 0x800C1626u);
    ts2_cheat_engine_activate();
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800C1620u)) == 0x1F1F && stub_u16(OFF(0x800C1624u)) == 0x1F1F);
    CHECK(stub_u16(OFF(0x800C1622u)) == 0);     /* not silently corrected */
    CHECK(g_stub.write_calls == 7u && g_stub.bad_addresses == 0u);
    /* Overlapping 8/16-bit lines in one cheat merge too. */
    in = mk("overlap-self", LINES("800C1640 1111", "300C1641 0022", "300C1642 0033"), 3, 0);
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, &in, 1) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && active_at(2)->range_count == 1u);
    CHECK(active_at(2)->ranges[0].start == 0x800C1640u && active_at(2)->ranges[0].end == 0x800C1643u);
}

static void test_cross_overlap(void)
{
    ts2_cheat_external_input in[3];
    fresh("cross-cheat overlap");
    in[0] = mk("a", LINES("800C1620 1F1F"), 1, 0);
    in[1] = mk("b", LINES("300C1621 0001"), 1, 0);
    in[2] = mk("c", LINES("800C1700 0001"), 1, 0);
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, in, 3) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL && g_st[1] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL);
    CHECK(g_st[2] == TS2_CHEAT_EXT_OK && active_count() == 3u);
    CHECK(g_tr[0] == TS2_CHEAT_TRUST_SAFE && g_tr[1] == TS2_CHEAT_TRUST_SAFE);   /* rejected */
    /* Advanced vs a Safe-tier entry (health): both rejected. */
    in[0] = mk("health", LINES("800B221E 000E"), 1, 1);
    in[1] = mk("health-hi", LINES("300B221F 0000"), 1, 0);
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, in, 2) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL && g_st[1] == TS2_CHEAT_EXT_ERR_OVERLAP_EXTERNAL);
    CHECK(active_count() == 2u);
}

static void test_builtin_overlap(void)
{
    ts2_cheat_external_input in;
    const ts2_cheat_ext_mode A = TS2_CHEAT_EXT_MODE_ADVANCED;
    fresh("built-in overlap");
    CHECK(one_line(A, "800B2226 0063") == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);   /* coins */
    CHECK(one_line(A, "300B2227 0001") == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);
    CHECK(one_line(A, "800B2222 0009") == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN);   /* lives */
    /* One overlapping line rejects the whole multi-line entry. */
    in = mk("mixed", LINES("800C1618 1F1F", "300B2226 0063"), 2, 0);
    CHECK(build(A, &in, 1) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_OVERLAP_BUILTIN && active_count() == 2u);
    /* The built-ins themselves are untouched and still SAFE. */
    CHECK(strcmp(active_at(0)->id, "max-coins") == 0 && active_at(0)->range_count == 1u);
    CHECK(ts2_cheat_active_trust(0) == TS2_CHEAT_TRUST_SAFE &&
          ts2_cheat_active_trust(1) == TS2_CHEAT_TRUST_SAFE);
}

static void test_start_disabled(void)
{
    ts2_cheat_external_input in[3];
    fresh("advanced starts disabled");
    in[0] = mk("health", LINES("800B221E 000E"), 1, 1);                 /* Safe tier */
    in[1] = mk("tokens", LINES("800C1618 1F1F"), 1, 1);                 /* Advanced */
    in[2] = mk("mixed", LINES("800C1700 0001", "300B2224 0005"), 2, 1); /* Advanced */
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, in, 3) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_OK && g_tr[0] == TS2_CHEAT_TRUST_SAFE);
    CHECK(g_st[1] == TS2_CHEAT_EXT_OK && g_tr[1] == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(g_st[2] == TS2_CHEAT_EXT_OK && g_tr[2] == TS2_CHEAT_TRUST_ADVANCED);
    CHECK(ts2_cheat_active_external_enabled(2) == 1);   /* Safe: file start state */
    CHECK(ts2_cheat_active_external_enabled(3) == 0);
    CHECK(ts2_cheat_active_external_enabled(4) == 0);
    CHECK(active_at(4)->range_count == 2u);             /* not touching: two spans */
    ts2_cheat_engine_activate();
    CHECK(ts2_cheat_engine_enabled_at(2) == 1);
    CHECK(ts2_cheat_engine_enabled_at(3) == 0 && ts2_cheat_engine_enabled_at(4) == 0);
    CHECK(ts2_cheat_engine_state_at(3) == TS2_CHEAT_OFF);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800B221Eu)) == 14);            /* only the Safe entry ran */
    CHECK(ram_zero(0x800C1618u, 2) && ram_zero(0x800C1700u, 2) && ram_zero(0x800B2224u, 1));
    CHECK(g_stub.write_calls == 1u);
}

static void test_safe_regression(void)
{
    ts2_cheat_external_input in[2];
    fresh("safe regression");
    in[0] = mk("tokens", LINES("800C1618 1F1F"), 1, 1);
    in[1] = mk("health", LINES("800B221E 000E"), 1, 1);
    CHECK(build(TS2_CHEAT_EXT_MODE_SAFE, in, 2) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_TARGET && g_st[1] == TS2_CHEAT_EXT_OK);
    CHECK(g_tr[0] == TS2_CHEAT_TRUST_SAFE && g_tr[1] == TS2_CHEAT_TRUST_SAFE);
    CHECK(active_count() == 3u && ts2_cheat_active_external_enabled(2) == 1);
    /* The mode-less API is Safe. */
    ts2_cheat_combined_reset();
    CHECK(ts2_cheat_session_begin() == 1);
    CHECK(ts2_cheat_combined_build(in, 2, g_st) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_TARGET && g_st[1] == TS2_CHEAT_EXT_OK);
    CHECK(ts2_cheat_active_trust(2) == TS2_CHEAT_TRUST_SAFE);
    /* Safe mode still rejects at the first line outside the policy (a later
     * unparsable line is not looked at); Advanced looks further. */
    in[0] = mk("early", LINES("800C1618 1F1F", "ZZZ"), 2, 0);
    CHECK(build(TS2_CHEAT_EXT_MODE_SAFE, in, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_TARGET);
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, in, 1) == TS2_CHEAT_BUILD_OK && g_st[0] == TS2_CHEAT_EXT_ERR_PARSE);
    /* An unknown mode value: refused, built-ins only. */
    in[0] = mk("health", LINES("800B221E 000E"), 1, 1);
    CHECK(build((ts2_cheat_ext_mode)7, in, 1) == TS2_CHEAT_BUILD_ERR_POLICY);
    CHECK(active_count() == 2u && g_tr[0] == TS2_CHEAT_TRUST_SAFE);
    /* The engine with the plain init: everything Safe, as before. */
    fresh("safe regression engine");
    CHECK(ts2_cheat_engine_init(1) == 1);
    CHECK(ts2_cheat_engine_enabled_at(0) == 1);
}

static void test_no_escalation_through_file(void)
{
    static const char doc[] =
        "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00893\"\n"
        "[[cheat]]\nid = \"tokens\"\nlabel = \"Tokens\"\ncode = [\"800C1618 1F1F\"]\nenabled = true\n"
        "[[cheat]]\nid = \"health\"\nlabel = \"Health\"\ncode = [\"800B221E 000E\"]\n";
    static ts2_cheat_file_report r;
    fresh("no escalation");
    CHECK(ts2_cheat_session_begin() == 1);
    CHECK(ts2_cheat_file_load_buffer((const unsigned char *)doc, sizeof doc - 1u, &r) ==
          TS2_CHEAT_FILE_OK);
    CHECK(r.verdict[0] == TS2_CHEAT_EXT_ERR_TARGET && r.verdict[1] == TS2_CHEAT_EXT_OK);
    CHECK(r.accepted == 1u && active_count() == 3u);
    CHECK(ts2_cheat_active_trust(2) == TS2_CHEAT_TRUST_SAFE);
    /* Unknown keys (e.g. a "trust" field) reject the entry, they never raise it. */
    {
        static const char doc2[] =
            "schema = \"toystory2recomp.cheats\"\nversion = 1\ngame = \"SLUS-00893\"\n"
            "[[cheat]]\nid = \"t\"\nlabel = \"T\"\ncode = [\"800C1618 1F1F\"]\ntrust = \"advanced\"\n";
        ts2_cheat_combined_reset();
        CHECK(ts2_cheat_session_begin() == 1);
        CHECK(ts2_cheat_file_load_buffer((const unsigned char *)doc2, sizeof doc2 - 1u, &r) ==
              TS2_CHEAT_FILE_OK);
        CHECK(r.shape[0] == TS2_CHEAT_ENTRY_UNKNOWN_KEY && r.accepted == 0u);
    }
}

static void test_engine_independent(void)
{
    static const psxcheat_psx_range r_overlay[] = { { 0x800D12C0u, 0x800D12C2u } };
    static const psxcheat_psx_range r_tok40[] = { { 0x800C1640u, 0x800C1642u } };
    static const psxcheat_psx_range r_tok18[] = { { 0x800C1618u, 0x800C161Au } };
    static const psxcheat_psx_range r_tok70[] = { { 0x800C1700u, 0x800C1702u } };
    static const psxcheat_psx_range r_edge[] = { { 0x800A04C0u, 0x800A04D0u } };
    /* Verified health span: valid under SAFE, so only the unknown tier can
     * reject entry D. */
    static const psxcheat_psx_range r_tok90[] = { { 0x800B221Eu, 0x800B2220u } };
    static const ts2_cheat_def defs[] = {
        { "a-overlay", "A", "800D12C0 0001\n", r_overlay, 1u, NULL, NULL, NULL },
        { "b-safe-wide", "B", "800C1640 0001\n", r_tok40, 1u, NULL, NULL, NULL },
        { "c-adv-ok", "C", "800C1618 1F1F\n", r_tok18, 1u, NULL, NULL, NULL },
        { "d-bad-tier", "D", "800B221E 000E\n", r_tok90, 1u, NULL, NULL, NULL },
        { "e-target", "E", "800C1800 0001\n", r_tok70, 1u, NULL, NULL, NULL },
        { "f-straddle", "F", "800A04C8 0001\n", r_edge, 1u, NULL, NULL, NULL },
    };
    const ts2_cheat_trust trust[] = {
        TS2_CHEAT_TRUST_ADVANCED, TS2_CHEAT_TRUST_SAFE, TS2_CHEAT_TRUST_ADVANCED,
        (ts2_cheat_trust)7, TS2_CHEAT_TRUST_ADVANCED, TS2_CHEAT_TRUST_ADVANCED,
    };
    const int on[] = { 1, 1, 1, 1, 1, 1 };
    size_t nv = 0, i;
    const psxcheat_psx_range *v = ts2_cheat_writable_ranges(&nv);
    fresh("engine independent");
    CHECK(ts2_cheat_engine_init_table_tiered(defs, 6, v, nv, trust, on) == TS2_CHEAT_TABLE_OK);
    CHECK(ts2_cheat_engine_setup_status(0) == TS2_CHEAT_DEF_ERR_POLICY);   /* outside region */
    CHECK(ts2_cheat_engine_setup_status(1) == TS2_CHEAT_DEF_ERR_POLICY);   /* Safe: not verified */
    CHECK(ts2_cheat_engine_setup_status(2) == TS2_CHEAT_DEF_OK);
    CHECK(ts2_cheat_engine_setup_status(3) == TS2_CHEAT_DEF_ERR_POLICY);   /* unknown tier */
    CHECK(ts2_cheat_engine_setup_status(4) == TS2_CHEAT_DEF_ERR_TARGET);   /* code beyond ranges */
    CHECK(ts2_cheat_engine_setup_status(5) == TS2_CHEAT_DEF_ERR_POLICY);   /* straddles start */
    /* Advanced starts off although enabled[] says on. */
    CHECK(ts2_cheat_engine_ready_at(2) == 1 && ts2_cheat_engine_enabled_at(2) == 0);
    for (i = 0; i < 6; ++i)
        if (i != 2) CHECK(ts2_cheat_engine_ready_at(i) == 0 && ts2_cheat_engine_set_enabled_at(i, 1) == 0);
    ts2_cheat_engine_vblank();
    CHECK(g_stub.write_calls == 0u);
    CHECK(ts2_cheat_engine_set_enabled_at(2, 1) == 1);
    ts2_cheat_engine_vblank();
    CHECK(stub_u16(OFF(0x800C1618u)) == 0x1F1F && g_stub.write_calls == 1u);
    CHECK(ram_zero(0x800D12C0u, 2) && ram_zero(0x800C1640u, 2) && ram_zero(0x800C1800u, 2));
    /* Without tiers (all Safe) only D (verified health span) is usable. */
    fresh("engine independent (no tiers)");
    CHECK(ts2_cheat_engine_init_table_tiered(defs, 6, v, nv, NULL, on) == TS2_CHEAT_TABLE_OK);
    for (i = 0; i < 6; ++i) CHECK(ts2_cheat_engine_ready_at(i) == (i == 3 ? 1 : 0));
    CHECK(ts2_cheat_engine_init_table(defs, 6, v, nv, on) == TS2_CHEAT_TABLE_OK);
    CHECK(ts2_cheat_engine_setup_status(2) == TS2_CHEAT_DEF_ERR_POLICY);
    /* The table validator agrees (pure). */
    {
        ts2_cheat_def_status per[6];
        CHECK(ts2_cheat_validate_table_tiered(defs, 6, v, nv, trust, per) == TS2_CHEAT_TABLE_OK);
        CHECK(per[2] == TS2_CHEAT_DEF_OK && per[0] == TS2_CHEAT_DEF_ERR_POLICY &&
              per[3] == TS2_CHEAT_DEF_ERR_POLICY);
        CHECK(ts2_cheat_validate_table(defs, 6, v, nv, per) == TS2_CHEAT_TABLE_OK);
        CHECK(per[2] == TS2_CHEAT_DEF_ERR_POLICY);
    }
}

static void test_no_partial_writes(void)
{
    ts2_cheat_external_input in = mk("half-bad", LINES("800C1618 1F1F", "800D12C0 0001"), 2, 1);
    static const psxcheat_psx_range r_tok18[] = { { 0x800C1618u, 0x800C161Au } };
    static const ts2_cheat_def def[] = {
        { "two-lines", "T", "800C1618 1F1F\n800C1640 0001\n", r_tok18, 1u, NULL, NULL, NULL },
    };
    const ts2_cheat_trust adv[] = { TS2_CHEAT_TRUST_ADVANCED };
    const int on[] = { 1 };
    size_t nv = 0;
    const psxcheat_psx_range *v = ts2_cheat_writable_ranges(&nv);
    fresh("no partial writes");
    /* Validator: one bad line rejects the whole entry. */
    CHECK(build(TS2_CHEAT_EXT_MODE_ADVANCED, &in, 1) == TS2_CHEAT_BUILD_OK);
    CHECK(g_st[0] == TS2_CHEAT_EXT_ERR_TARGET && active_count() == 2u);
    ts2_cheat_engine_activate();
    ts2_cheat_engine_vblank();
    CHECK(ram_zero(0x800C1618u, 2) && g_stub.write_calls == 0u);
    /* Engine: a program with a line beyond its own ranges is never set up. */
    fresh("no partial writes (engine)");
    CHECK(ts2_cheat_engine_init_table_tiered(def, 1, v, nv, adv, on) == TS2_CHEAT_TABLE_OK);
    CHECK(ts2_cheat_engine_setup_status(0) == TS2_CHEAT_DEF_ERR_TARGET);
    CHECK(ts2_cheat_engine_set_enabled_at(0, 1) == 0);
    ts2_cheat_engine_vblank();
    CHECK(ram_zero(0x800C1618u, 2) && ram_zero(0x800C1640u, 2) && g_stub.write_calls == 0u);
}

int main(void)
{
    test_region();
    test_rejections();
    test_writes_8_16();
    test_multiline_tokens();
    test_duplicate_lines();
    test_cross_overlap();
    test_builtin_overlap();
    test_start_disabled();
    test_safe_regression();
    test_no_escalation_through_file();
    test_engine_independent();
    test_no_partial_writes();
    ts2_cheat_engine_shutdown();
    ts2_cheat_combined_reset();
    printf("advanced cheats: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
