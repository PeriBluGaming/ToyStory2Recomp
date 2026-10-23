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
    } else if (xmax < TS2_LS_LEFT_GROUP_MAX) {
        edge = -1;
    } else if (xmin >= TS2_LS_RIGHT_GROUP_MIN) {
        edge = 1;
    }
    psx_mod_anchor_hud_primitive(prim, edge);
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
    psx_mod_anchor_hud_primitive(prim, edge);
    return 0;
}

static void widescreen_activate(void)
{
    (void)psx_mod_set_fixed_display_aspect(16, 9);
    ls_alloc_filler();
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
}
