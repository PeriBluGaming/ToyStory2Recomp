/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

/* SetClipRect (0x80010000): $a0..$a3 = X min, X max, Y min, Y max in pixels.
 * It stores them as Q16 in scratchpad (0x1F800060..6C); the mesh renderer
 * (0x800100E4) rejects polygons whose vertices all lie outside that rectangle.
 * Widen only the full-screen X range (0..512) by the culling margin. */
#define TS2_SET_CLIP_RECT 0x80010000u
#define TS2_SCREEN_WIDTH  512u

static int widescreen_clip_rect_filter(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    int32_t margin = psx_mod_widescreen_x_margin();
    if (margin > 0 && cpu->gpr[4] == 0u && cpu->gpr[5] == TS2_SCREEN_WIDTH) {
        cpu->gpr[4] = (uint32_t)(-margin);
        cpu->gpr[5] = TS2_SCREEN_WIDTH + (uint32_t)margin;
    }
    return 0;
}

/* AddPrim (0x80084FCC): $a0 = OT slot pointer, $a1 = packet (P_TAG).
 * The in-game HUD is linked into OT slot 175 (offset 0x2BC from the
 * double-buffered base at *0x800A1468) by three builders, identified by the
 * caller's return address. Gameplay HUD packets lie within x<100 or x>412;
 * anything else (e.g. the pause box borders, x~160/~350) stays centred. */
#define TS2_ADD_PRIM        0x80084FCCu
#define TS2_OT_BASE_PTR     0x800A1468u
#define TS2_HUD_OT_SLOT     175u
#define TS2_HUD_RA_QUAD_2D  0x80046058u
#define TS2_HUD_RA_SPRT     0x80045DB8u
#define TS2_HUD_RA_QUAD_2C  0x80046A54u
/* The Level Select overlay (loaded around 0x800E1000) reuses the HUD builders.
 * Its two AddPrim call sites hold "jal AddPrim"; in gameplay those words are
 * other code or data, so this identifies the overlay without frame heuristics. */
#define TS2_LEVEL_SELECT_JAL_A 0x800E1E40u
#define TS2_LEVEL_SELECT_JAL_B 0x800E23C4u
#define TS2_JAL_ADD_PRIM       0x0C0213F3u
#define TS2_LEFT_EDGE_MAX  100
#define TS2_RIGHT_EDGE_MIN 412

#define TS2_LS_LEFT_GROUP_MAX 140
#define TS2_LS_RIGHT_GROUP_MIN 360

/* Level Select frame gaps: textured 0x66 sprites cut from uniform runs of the
 * original bezel texture, packed into one GPU-DMA node per frame buffer. */
typedef struct { int16_t x, y; uint8_t u, v, w, h; } ls_fill_t;
#define LS_PAGE0_CLUT 0x3FF0u
#define LS_PAGE1_CLUT 0x3FB0u
static const ls_fill_t ls_fill_page0[] = {
    {43, 0, 97, 8, 36, 24}, {79, 0, 97, 8, 36, 24}, {115, 0, 97, 8, 13, 24},
    {43, 216, 129, 224, 18, 24}, {61, 216, 129, 224, 18, 24}, {79, 216, 129, 224, 18, 24},
    {97, 216, 129, 224, 18, 24}, {115, 216, 129, 224, 13, 24},
};
static const ls_fill_t ls_fill_page1[] = {
    {384, 0, 68, 8, 36, 24}, {420, 0, 68, 8, 36, 24}, {456, 0, 68, 8, 13, 24},
    {384, 216, 128, 224, 18, 24}, {402, 216, 128, 224, 18, 24}, {420, 216, 128, 224, 18, 24},
    {438, 216, 128, 224, 18, 24}, {456, 216, 128, 224, 13, 24},
};
#define LS_FILL_WORDS (2u + 16u * 4u)
static uint32_t ls_filler_node[2];
static unsigned ls_filler_flip;

static void ls_alloc_filler(void)
{
    if (ls_filler_node[0])
        return;
    uint32_t base = psx_mod_alloc_gpu_dma_memory(2u * 16u * ((LS_FILL_WORDS + 1u + 3u) / 4u), 16u);
    if (base) {
        uint32_t stride = 16u * ((LS_FILL_WORDS + 1u + 3u) / 4u);
        ls_filler_node[0] = base;
        ls_filler_node[1] = base + stride;
    }
}

static uint32_t ls_emit_group(uint32_t at, uint32_t e1, uint32_t clut,
                              const ls_fill_t* f, unsigned n)
{
    psx_mod_write_word(at, e1);
    at += 4u;
    for (unsigned i = 0; i < n; ++i, at += 16u) {
        psx_mod_write_word(at, 0x66808080u);
        psx_mod_write_word(at + 4u, ((uint32_t)(uint16_t)f[i].y << 16) | (uint16_t)f[i].x);
        psx_mod_write_word(at + 8u, (clut << 16) | ((uint32_t)f[i].v << 8) | f[i].u);
        psx_mod_write_word(at + 12u, ((uint32_t)f[i].h << 16) | f[i].w);
    }
    return at;
}

/* Allocated once at activation: the GPU-DMA aperture size is part of the
 * savestate layout cookie, so allocating on first use would make states saved
 * before and after a Level Select visit mutually incompatible. */
static void ls_insert_filler(uint32_t ot_slot)
{
    if (!ls_filler_node[0])
        return;
    uint32_t node = ls_filler_node[ls_filler_flip ^= 1u];
    uint32_t head = psx_mod_read_word(ot_slot);
    uint32_t at = node + 4u;
    at = ls_emit_group(at, 0xE10006C0u, LS_PAGE0_CLUT, ls_fill_page0, 8u);
    at = ls_emit_group(at, 0xE10006C2u, LS_PAGE1_CLUT, ls_fill_page1, 8u);
    psx_mod_write_word(node, (LS_FILL_WORDS << 24) | (head & 0x00FFFFFFu));
    psx_mod_write_word(ot_slot, (head & 0xFF000000u) | (node & 0x00FFFFFFu));
}

/* HUD anchor refresh (Issue #22). The runtime honours an anchor tag for two
 * VBlanks after it was set, but the game builds the HUD (0x80074F80) before
 * the world and the reverse OT reaches slot 175 near the end of the DMA walk,
 * so a slow game frame (3+ VBlanks) let every HUD tag expire and the whole HUD
 * drew at its 4:3 position for that frame.
 *
 * Each tagged packet is remembered with its classification and contents.
 * DrawOTag (0x80086054, called from the frame end at 0x8003A950) seals them
 * into a batch keyed by its OT pointer. libgpu's addque2 starts the transfer
 * through 0x80087A70 (exec(ot): D2_MADR = ot, CHCR = 0x01000401) either at
 * once or, while the GPU is busy, later from the DMA-complete callback; on
 * that entry the matching batch is tagged again, right before the walk. A
 * packet is refreshed only if its length and every word still match what was
 * classified, so reused memory never inherits a stale edge. Classification is
 * unchanged; this only renews the tags' timestamps. */
#define TS2_DRAW_OTAG    0x80086054u
#define TS2_GPU_OT_KICK  0x80087A70u
#define HUD_TRACK_MAX    512u      /* gameplay HUD ~11/frame; menus and text more */
#define HUD_TRACK_WORDS  12u       /* longest GP0 packet the runtime tags */

typedef struct {
    uint32_t prim;
    int8_t edge;
    uint8_t len;
    uint32_t words[HUD_TRACK_WORDS];
} hud_track_t;
typedef struct {
    uint32_t ot;        /* DrawOTag argument; 0 while open */
    uint32_t ot_base;   /* *0x800A1468 when the entries were recorded */
    unsigned count;
    hud_track_t e[HUD_TRACK_MAX];
} hud_batch_t;
/* One open batch (being built) and two sealed ones awaiting their transfer:
 * addque2 may still hold the previous frame's OT when the next is sealed. */
static hud_batch_t hud_buf[3];
static unsigned hud_open = 0;
static unsigned hud_sealed[2] = { 1u, 2u };   /* [0] older, [1] newer */

static void hud_track_reset(void)
{
    for (unsigned i = 0; i < 3u; ++i) {
        hud_buf[i].ot = 0;
        hud_buf[i].ot_base = 0;
        hud_buf[i].count = 0;
    }
}

static void hud_anchor(uint32_t prim, int edge)
{
    psx_mod_anchor_hud_primitive(prim, edge);

    /* Entries left from an OT that never reached DrawOTag belong to an older
     * frame: drop them instead of carrying them into this one. */
    hud_batch_t* b = &hud_buf[hud_open];
    uint32_t base = psx_mod_read_word(TS2_OT_BASE_PTR);
    if (b->count && base != b->ot_base)
        b->count = 0;
    b->ot_base = base;

    uint32_t len = psx_mod_read_byte(prim + 3u);
    if (len == 0u || len > HUD_TRACK_WORDS || b->count >= HUD_TRACK_MAX) {
        psx_mod_counter_add("toystory2.ws-hud.track_skipped", 1u);
        return;
    }
    hud_track_t* t = &b->e[b->count++];
    t->prim = prim;
    t->edge = (int8_t)(edge < 0 ? -1 : edge > 0 ? 1 : 0);
    t->len = (uint8_t)len;
    for (uint32_t i = 0; i < len; ++i)
        t->words[i] = psx_mod_read_word(prim + 4u + 4u * i);
}

static int hud_track_unchanged(const hud_track_t* t)
{
    if (psx_mod_read_byte(t->prim + 3u) != t->len)
        return 0;
    for (uint32_t i = 0; i < t->len; ++i)
        if (psx_mod_read_word(t->prim + 4u + 4u * i) != t->words[i])
            return 0;
    return 1;
}

static int widescreen_active_for_hud(void)
{
    return psx_mod_widescreen_x_margin() > 0 && psx_mod_display_width() == TS2_SCREEN_WIDTH;
}

/* DrawOTag(ot): seal the open batch under this OT pointer. */
static int widescreen_hud_seal_filter(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    if (!widescreen_active_for_hud()) {
        hud_track_reset();
        return 0;
    }
    uint32_t ot = cpu->gpr[4];
    for (unsigned i = 0; i < 2u; ++i)
        if (hud_buf[hud_sealed[i]].ot == ot)   /* resubmitted before its kick */
            hud_buf[hud_sealed[i]].count = 0;
    if (hud_buf[hud_open].count == 0u)
        return 0;
    unsigned victim = hud_sealed[0];          /* oldest sealed batch is dropped */
    hud_sealed[0] = hud_sealed[1];
    hud_sealed[1] = hud_open;
    hud_buf[hud_open].ot = ot;
    hud_open = victim;
    hud_buf[victim].ot = 0;
    hud_buf[victim].ot_base = 0;
    hud_buf[victim].count = 0;
    return 0;
}

/* exec(ot) at 0x80087A70: the transfer of this OT starts now. */
static int widescreen_hud_kick_filter(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    if (!widescreen_active_for_hud()) {
        hud_track_reset();
        return 0;
    }
    uint32_t ot = cpu->gpr[4];
    for (unsigned s = 0; s < 2u; ++s) {
        hud_batch_t* b = &hud_buf[hud_sealed[s]];
        if (b->ot != ot || b->count == 0u)
            continue;
        uint32_t refreshed = 0, changed = 0;
        for (unsigned i = 0; i < b->count; ++i) {
            if (hud_track_unchanged(&b->e[i])) {
                psx_mod_anchor_hud_primitive(b->e[i].prim, b->e[i].edge);
                ++refreshed;
            } else {
                ++changed;
            }
        }
        if (refreshed)
            psx_mod_counter_add("toystory2.ws-hud.refreshed", refreshed);
        if (changed)
            psx_mod_counter_add("toystory2.ws-hud.refresh_changed", changed);
        /* The game swaps OT buffers right after DrawOTag returns, so a kick
         * that sees the other buffer was deferred by addque2. */
        if (psx_mod_read_word(TS2_OT_BASE_PTR) != b->ot_base)
            psx_mod_counter_add("toystory2.ws-hud.kick_deferred", 1u);
        b->ot = 0;
        b->count = 0;
    }
    return 0;
}

static void widescreen_hud_savestate(void)
{
    /* Restored guest memory need not hold the packets recorded before. */
    hud_track_reset();
}

/* Front-end overlay text (Issue #24). The overlay's centred-text routine
 * 0x800E1984(str, y, centre) draws each character as its own packet: a
 * 16 px SPRT (AddPrim at 0x800E1E40) or, for the button icons 0xB2/0xB3/
 * 0xB9/0xBA while *0x800A14B4 != 0, a 22 px quad (AddPrim at 0x800E1D1C,
 * 10 px extra advance at 0x800E1D40); every character, drawn or not,
 * advances 13 px (0x800E1E58). Classified one by one, long Level Select
 * names (18+ characters) lost their outer letters to the edge groups. At
 * either AddPrim the routine's registers describe the whole string: s7 =
 * start (after an optional '^' colour prefix), s5 = length, s3 = current
 * character, s2 = its x. The string's drawn extent decides one group for
 * all of its packets, with the same thresholds; strings that lie inside a
 * group keep it (the right-hand select/back prompts, for example). */
#define TS2_FE_TEXT_RA_GLYPH   0x800E1E48u
#define TS2_FE_TEXT_RA_ICON    0x800E1D24u
#define TS2_FE_TEXT_JAL_ICON   0x800E1D1Cu
#define TS2_FE_TEXT_ICON_STEP  0x800E1D40u
#define TS2_FE_TEXT_STEP       0x800E1E58u
#define TS2_ADDIU_S2_10        0x2652000Au
#define TS2_ADDIU_S2_13        0x2652000Du
#define TS2_FE_ICON_SCALE      0x800A14B4u
#define TS2_FE_TEXT_MAX_LEN    64u

static int ts2_fe_text_icon(uint32_t c)
{
    return c == 0xB2u || c == 0xB3u || c == 0xB9u || c == 0xBAu;
}

/* Characters with a glyph in the routine's jump table (0x800D6144). */
static int ts2_fe_text_drawn(uint32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '\'' || c == '.' || c == '?' || c == '!' || c == '\\' || c == '/' ||
           ts2_fe_text_icon(c);
}

static int ts2_fe_string_edge(const struct CPUState* cpu, int* edge)
{
    uint32_t ra = cpu->gpr[31];
    if (ra != TS2_FE_TEXT_RA_GLYPH && ra != TS2_FE_TEXT_RA_ICON)
        return 0;
    if (psx_mod_read_word(TS2_FE_TEXT_JAL_ICON) != TS2_JAL_ADD_PRIM ||
        psx_mod_read_word(TS2_FE_TEXT_ICON_STEP) != TS2_ADDIU_S2_10 ||
        psx_mod_read_word(TS2_FE_TEXT_STEP) != TS2_ADDIU_S2_13)
        return 0;

    uint32_t start = cpu->gpr[23], len = cpu->gpr[21], cur = cpu->gpr[19];
    if (len == 0u || len > TS2_FE_TEXT_MAX_LEN || cur < start || cur - start >= len ||
        !ts2_fe_text_drawn(psx_mod_read_byte(cur)))
        return 0;

    int32_t icon_extra = psx_mod_read_word(TS2_FE_ICON_SCALE) != 0u ? 10 : 0;
    int32_t pos = 0, cur_pos = 0, lo = 0, hi = 0;
    int drawn = 0;
    for (uint32_t i = 0; i < len; ++i) {
        uint32_t c = psx_mod_read_byte(start + i);
        int quad = icon_extra && ts2_fe_text_icon(c);
        if (start + i == cur)
            cur_pos = pos;
        if (ts2_fe_text_drawn(c)) {
            int32_t end = pos + (quad ? 22 : 16);
            if (!drawn || pos < lo) lo = pos;
            if (!drawn || end > hi) hi = end;
            drawn = 1;
        }
        pos += 13 + (quad ? icon_extra : 0);
    }

    int32_t origin = (int32_t)cpu->gpr[18] - cur_pos;
    if (origin + hi < TS2_LS_LEFT_GROUP_MAX)
        *edge = -1;
    else if (origin + lo >= TS2_LS_RIGHT_GROUP_MIN)
        *edge = 1;
    else
        *edge = 0;
    return 1;
}

static int level_select_filter(struct CPUState* cpu)
{
    uint32_t prim = cpu->gpr[5];
    uint32_t code = psx_mod_read_byte(prim + 7u);
    int32_t xmin, xmax;
    if (code == 0x2Cu || code == 0x2Du) {
        xmin = xmax = (int16_t)psx_mod_read_half(prim + 8u);
        for (uint32_t off = 0x10u; off <= 0x20u; off += 8u) {
            int32_t x = (int16_t)psx_mod_read_half(prim + off);
            if (x < xmin) xmin = x;
            if (x > xmax) xmax = x;
        }
    } else if (code == 0x64u || code == 0x65u || code == 0x66u) {
        xmin = (int16_t)psx_mod_read_half(prim + 8u);
        xmax = xmin + (int32_t)psx_mod_read_half(prim + 0x10u);
    } else {
        return 0;
    }

    int edge = 0;
    if (code == 0x66u) {
        if (xmin == 0) {
            edge = -1;
            if ((int16_t)psx_mod_read_half(prim + 10u) == -8)
                ls_insert_filler(cpu->gpr[4]);
        }
        else if (xmin == 384)
            edge = 1;
    } else if (ts2_fe_string_edge(cpu, &edge)) {
        /* one group for the whole string */
    } else if (xmax < TS2_LS_LEFT_GROUP_MAX) {
        edge = -1;
    } else if (xmin >= TS2_LS_RIGHT_GROUP_MIN) {
        edge = 1;
    }
    hud_anchor(prim, edge);
    return 0;
}

/* Dialogue and message boxes (Issue #23). The NPC dialogue box (drawn inline
 * in 0x80063AC8, x 20..494 when open) and the box helper 0x80063954 (pause
 * box; "you have collected a token!" box, x 77..435) each draw two 2 px
 * vertical bars, two full-width bars and a fill as separate sprites through
 * 0x8004676C. Classified one by one, the vertical bars fell beyond the edge
 * thresholds and moved outward while the rest of the box stayed centred; the
 * dialogue's inline button icons (0x80045DD8 via 0x80064058) could do the
 * same. AddPrim is a leaf, so at its entry $sp is still the builder's frame:
 * 0x8004676C saves $ra at 68($sp) (0x800467C0) and 0x80045DD8 at 64($sp)
 * (0x80045E10), both straight-line before any AddPrim call, and neither
 * changes $sp again until its epilogue, after the AddPrim. When that saved
 * caller is one of these verified box or dialogue sites, the part stays
 * centred with the box. Every other caller keeps its classification. */
#define TS2_QUAD_2C_SAVED_RA 68u
#define TS2_QUAD_2D_SAVED_RA 64u
static const uint32_t ts2_box_sprite_sites[] = {
    0x800639C8u, 0x800639FCu, 0x80063A2Cu, 0x80063A60u, 0x80063A94u, /* 0x80063954 */
    0x80063F54u, 0x80063F88u, 0x80063FB8u, 0x80063FECu, 0x80064024u, /* dialogue box */
};
static const uint32_t ts2_dialogue_icon_sites[] = {
    0x80063D84u,                /* "continue" prompt below the dialogue text */
    0x800640B0u, 0x800640D8u,   /* inline '@' (X) and '~' (Triangle) icons */
};

static int ts2_centred_box_part(const struct CPUState* cpu, uint32_t ra)
{
    const uint32_t* sites;
    unsigned count;
    uint32_t slot;
    if (ra == TS2_HUD_RA_QUAD_2C) {
        sites = ts2_box_sprite_sites;
        count = sizeof ts2_box_sprite_sites / sizeof ts2_box_sprite_sites[0];
        slot = TS2_QUAD_2C_SAVED_RA;
    } else if (ra == TS2_HUD_RA_QUAD_2D) {
        sites = ts2_dialogue_icon_sites;
        count = sizeof ts2_dialogue_icon_sites / sizeof ts2_dialogue_icon_sites[0];
        slot = TS2_QUAD_2D_SAVED_RA;
    } else {
        return 0;
    }
    uint32_t caller = psx_mod_read_word(cpu->gpr[29] + slot);
    for (unsigned i = 0; i < count; ++i)
        if (caller == sites[i])
            return 1;
    return 0;
}

static int widescreen_hud_filter(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    if (psx_mod_widescreen_x_margin() <= 0 || psx_mod_display_width() != TS2_SCREEN_WIDTH)
        return 0;

    if (psx_mod_read_word(TS2_LEVEL_SELECT_JAL_A) == TS2_JAL_ADD_PRIM &&
        psx_mod_read_word(TS2_LEVEL_SELECT_JAL_B) == TS2_JAL_ADD_PRIM)
        return level_select_filter(cpu);

    uint32_t ra = cpu->gpr[31];
    if (ra != TS2_HUD_RA_QUAD_2D && ra != TS2_HUD_RA_SPRT && ra != TS2_HUD_RA_QUAD_2C)
        return 0;

    uint32_t base = psx_mod_read_word(TS2_OT_BASE_PTR);
    if (cpu->gpr[4] != base + TS2_HUD_OT_SLOT * 4u)
        return 0;

    uint32_t prim = cpu->gpr[5];
    uint32_t code = psx_mod_read_byte(prim + 7u);
    int32_t xmin, xmax;
    if (code == 0x2Cu || code == 0x2Du) {
        xmin = xmax = (int16_t)psx_mod_read_half(prim + 8u);
        for (uint32_t off = 0x10u; off <= 0x20u; off += 8u) {
            int32_t x = (int16_t)psx_mod_read_half(prim + off);
            if (x < xmin) xmin = x;
            if (x > xmax) xmax = x;
        }
    } else if (code == 0x65u) {
        xmin = (int16_t)psx_mod_read_half(prim + 8u);
        xmax = xmin + (int32_t)psx_mod_read_half(prim + 0x10u);
    } else {
        return 0;
    }

    int edge = 0;
    if (xmax < TS2_LEFT_EDGE_MAX)
        edge = -1;
    else if (xmin > TS2_RIGHT_EDGE_MIN)
        edge = 1;
    if (edge && ts2_centred_box_part(cpu, ra))
        edge = 0;
    hud_anchor(prim, edge);
    return 0;
}

static void widescreen_activate(void)
{
    (void)psx_mod_set_fixed_display_aspect(16, 9);
    ls_alloc_filler();
    hud_track_reset();
}

PSX_MOD_CONSTRUCTOR(register_widescreen)
{
    (void)psx_mod_register_activation_plugin(
        "toystory2.widescreen",
        widescreen_activate
    );
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.ws-clip-rect",
        TS2_SET_CLIP_RECT,
        widescreen_clip_rect_filter
    );
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.ws-hud-anchor", TS2_ADD_PRIM, widescreen_hud_filter);
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.ws-hud-anchor", TS2_DRAW_OTAG, widescreen_hud_seal_filter);
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.ws-hud-anchor", TS2_GPU_OT_KICK, widescreen_hud_kick_filter);
    (void)psx_mod_register_savestate_plugin(
        "toystory2.ws-hud-anchor", widescreen_hud_savestate);
}
