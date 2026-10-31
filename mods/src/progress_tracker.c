/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

/* Progress Tracker: a second pause-menu page.
 *
 * The page lives only in this mod. The game's pause state (0x800A10F4), page
 * (0x800A1624), cursor and settings are never written, so a savestate, rewind
 * or disabling the mod always leaves the original menu exactly as it was.
 *
 * A paused frame runs the HUD (0x80074F80), which draws the pause menu, and
 * then the pause input handler (0x800742C4). Page changes made by the input
 * hook therefore show from the next frame on, like the game's own. */
#define TS2_PAUSED            0x800A10F4u   /* int16, nonzero while paused */
#define TS2_PAUSE_PAGE        0x800A1624u   /* int16, 0 = main pause menu */
#define TS2_PAUSE_IDLE        0x800A14CCu   /* menu hidden when it reaches 0 */
#define TS2_PAD_NOW           0x800A1480u
#define TS2_PAD_PREV          0x800A11E4u
#define TS2_BTN_START         0x0008u
#define TS2_BTN_RIGHT         0x0020u
#define TS2_BTN_LEFT          0x0080u
#define TS2_BTN_TRIANGLE      0x1000u

#define TS2_PAUSE_INPUT       0x800742C4u

/* `beq $v0,$zr,0x8007577C` on the idle timer: every paused frame reaches it
 * (the token-dialog path joins at 0x80075394), and taking it skips the whole
 * original menu, box included. */
#define TS2_MENU_SHOW_SITE    0x8007539Cu
#define TS2_BEQ_MENU_HIDDEN   0x104000F7u

/* The first instruction after the pause-menu block is `jal 0x80074DEC` with
 * $a0 = 0; 0x80074DEC has nine other callers, told apart by $ra. */
#define TS2_HUD_SLOT          0x80074DECu
#define TS2_AFTER_MENU_RA     0x80075784u

/* Text: 0x800641F0(y, string, r, g) with b at sp+0x10 and a large-font flag
 * at sp+0x14; lines are centred at x 160 of the 320-wide HUD space, 8 units
 * per character (12.8 px of the 512-wide screen). The font has only a-z, 0-9,
 * space and ! ' * , - . > ?, so labels avoid '/' and ':'; its glyphs are
 * yellow, so colour values only dim them.
 *
 * Sprite: 0x8004676C(x, y, set, frame) with r, g, b at sp+0x10/0x14/0x18, a
 * texture-page mode at sp+0x1C and x/y scale (1/4096) at sp+0x20/0x24. Set 6
 * frame 1 is a solid texel; the pause box (0x80063954) builds its borders
 * (mode 0x60: opaque) and its fill (mode 0: 50% blend) from it. In 16:9 the
 * widescreen mod centres these sprites only while they stay within x
 * 100..412, so the panel keeps inside that range. */
#define TS2_DRAW_TEXT         0x800641F0u
#define TS2_DRAW_SPRITE       0x8004676Cu
#define TS2_SPRITE_SET_UI     6u
#define TS2_SPRITE_SOLID      1u
#define TS2_MODE_BLEND        0x00u
#define TS2_MODE_OPAQUE       0x60u

/* The Progress panel, in 512-wide screen space: centred on x 256 like the
 * original box, wider for 20-character rows, and grown upwards so its bottom
 * edge stays on the original box's (y 145) and the page dots keep their
 * place. */
#define PT_PANEL_X            112u
#define PT_PANEL_Y            34u
#define PT_PANEL_W            288u
#define PT_PANEL_H            112u          /* bottom edge at y 145 */
#define PT_TITLE_Y            39u
#define PT_LEVEL_Y            52u
#define PT_TOKENS_Y           62u
#define PT_RULE_Y             75u
#define PT_RULE_INSET         16u
#define PT_ROW_Y              81u
#define PT_ROW_STEP           12u

/* The sprite set's texture page blends 50/50 (ABR 0), so three layers of a
 * dark purple cover the scene about 87%; a 1 px opaque line in a muted
 * lavender draws the border and the header rule. */
#define PT_FILL_LAYERS        3
#define PT_FILL_R             0x28u
#define PT_FILL_G             0x04u
#define PT_FILL_B             0x44u
#define PT_LINE_R             0x50u
#define PT_LINE_G             0x38u
#define PT_LINE_B             0x78u

/* Rows are centred strings of one fixed length, so their labels share a left
 * edge; the state box sits in the free columns 0..1 before the label. */
#define PT_ROW_LEN            20u
#define PT_ROW_LABEL_COL      2u

#define PT_BRIGHT             0x80u         /* r = g = 0x80, b = 0: the menu yellow */
#define PT_SUBTLE             0x68u
#define PT_DIM                0x48u
#define PT_TOKENS_TINT        0x50u         /* the token line, a step quieter */

/* State boxes, drawn from the UI sprite. Its texel is a grey of about 80
 * (measured), so one layer reaches at most ~160 per channel: a done box adds
 * a second, additive layer to reach the font's yellow (249, 201, 0); an open
 * box is one opaque layer at the dimmed label's (140, 113, 0). A 1-unit dark
 * outline matches the font's own. 13 x 8 reads square on the 512 x 240
 * screen; glyph bodies span rows y+1..y+6. */
#define TS2_MODE_ADD          0x20u         /* ABR 1: background + sprite */
#define PT_BOX_X              129u
#define PT_BOX_W              13u
#define PT_BOX_H              8u
#define PT_BOX_ON_R           0xC6u
#define PT_BOX_ON_G           0xA8u
#define PT_BOX_OFF_R          0xE0u
#define PT_BOX_OFF_G          0xBEu
#define PT_INK                0x10u         /* outline and check mark: near black */

/* The check mark, cut dark into a done box: rows of { y, x, width } inside
 * the 13 x 8 box: a short stroke from x 2 and a long one from x 11 meet at
 * the tip, x 5..6 on row 6. */
typedef struct { uint8_t y, x, w; } pt_span_t;
static const pt_span_t pt_check[] = {
    { 1, 10, 2 }, { 2, 9, 2 }, { 3, 2, 2 }, { 3, 8, 2 },
    { 4, 3, 2 }, { 4, 7, 2 }, { 5, 4, 4 }, { 6, 5, 2 },
};

/* The font has no '/': the token line leaves its column blank and the slash
 * is drawn there, in the line's own colour (one opaque layer reaches it),
 * rising over the glyph rows y+1..y+6 of a 12.8-unit character cell. */
#define PT_SLASH_R            0xFAu
#define PT_SLASH_G            0xD4u
static const pt_span_t pt_slash[] = {
    { 1, 8, 3 }, { 2, 7, 3 }, { 3, 6, 3 }, { 4, 5, 3 }, { 5, 4, 3 }, { 6, 3, 3 },
};

/* Page dots: '*' glyphs a little below the box. Equal-length strings keep both
 * dots at the same two positions whichever one is lit. */
#define PT_DOTS_Y             156u
#define PT_DOT_ON             0x80u
#define PT_DOT_OFF            0x28u

/* Stack arguments and the page's strings go below the caller's $sp, in stack
 * the original call would use; the callees' frames lie below this block. */
#define PT_SCRATCH            0x70u
#define PT_SCRATCH_STR        0x30u         /* above the sprite's six stack args */

enum { PT_VIEW_NONE, PT_VIEW_MENU, PT_VIEW_PROGRESS };

static int pt_page;   /* 1 while the Progress page replaces the main menu */
static int pt_view;   /* what this frame's menu block showed */

static void pt_reset(void)
{
    pt_page = 0;
    pt_view = PT_VIEW_NONE;
}

static int pt_paused(void)
{
    return psx_mod_read_half(TS2_PAUSED) != 0;
}

/* Only a lone new press counts, so the hidden menu never sees a confirm
 * pressed together with a page change. Skipping the handler on a frame
 * without such a press changes nothing: with page 0 and no new press it
 * writes no state. */
static int pt_input_filter(struct CPUState* cpu, uint32_t address)
{
    (void)cpu;
    (void)address;
    if (!pt_paused()) {
        pt_reset();
        return 0;
    }
    const uint16_t pressed =
        (uint16_t)(psx_mod_read_half(TS2_PAD_NOW) & ~psx_mod_read_half(TS2_PAD_PREV));
    const int16_t page = (int16_t)psx_mod_read_half(TS2_PAUSE_PAGE);

    if (pt_page) {
        if (page != 0) {
            pt_page = 0;
            return 0;
        }
        if (pressed & (TS2_BTN_LEFT | TS2_BTN_TRIANGLE)) {
            pt_page = 0;
            return 1;
        }
        /* Start alone: the original handler unpauses from the main page. */
        if (pressed == TS2_BTN_START) {
            pt_page = 0;
            return 0;
        }
        return 1;
    }
    /* Right is unused on the main page; other pages keep their own. */
    if (page == 0 && pressed == TS2_BTN_RIGHT &&
        psx_mod_read_word(TS2_PAUSE_IDLE) != 0) {
        pt_page = 1;
        return 1;
    }
    return 0;
}

/* $v0 holds the idle timer. Zero means the game hides the menu itself. */
static void pt_menu_site(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    pt_view = PT_VIEW_NONE;
    if (cpu->gpr[2] == 0)
        return;
    if (pt_page) {
        cpu->gpr[2] = 0;
        pt_view = PT_VIEW_PROGRESS;
    } else {
        pt_view = PT_VIEW_MENU;
    }
}

static void pt_text(struct CPUState* cpu, uint32_t frame, uint32_t y, uint32_t string,
                    uint32_t r, uint32_t g, uint32_t b)
{
    psx_mod_write_word(frame + 0x10u, b);
    psx_mod_write_word(frame + 0x14u, 0u);
    (void)psx_mod_call_guest(cpu, TS2_DRAW_TEXT, cpu->gpr[31], y, string, r, g);
}

/* Copies a string into the scratch block and returns its guest address. */
static uint32_t pt_string(uint32_t frame, const char* s)
{
    const uint32_t at = frame + PT_SCRATCH_STR;
    uint32_t i = 0;
    for (; s[i] && i < PT_SCRATCH - PT_SCRATCH_STR - 1u; ++i)
        psx_mod_write_byte(at + i, (uint8_t)s[i]);
    psx_mod_write_byte(at + i, 0u);
    return at;
}

static void pt_dots(struct CPUState* cpu, uint32_t frame, int second)
{
    const uint32_t first_level = second ? PT_DOT_OFF : PT_DOT_ON;
    const uint32_t second_level = second ? PT_DOT_ON : PT_DOT_OFF;
    pt_text(cpu, frame, PT_DOTS_Y, pt_string(frame, "*  "),
            first_level, first_level, first_level);
    pt_text(cpu, frame, PT_DOTS_Y, pt_string(frame, "  *"),
            second_level, second_level, second_level);
}

/* Level progress, read-only from game RAM while paused.
 *
 * 0x800A16A8 holds the level ID (1..15; 16 on the memory-card screen) and
 * 0x800A1530 the play-order slot; the game maps slot to ID with the table at
 * 0x8009DEF8, copied below. The tokens of level ID n are bits 0..4 of
 * 0x800C1617 + n in the game's save image, set when a token is picked up;
 * whether that progress is on a memory card is not known here. Every level's
 * hint table (indexed by token bit) and its Hamm call (always slot 0) give
 * one bit order for all ten token levels. The five boss levels set up no
 * tokens at all; their overlays store 0x80 in the level byte during the boss
 * sequence, and the game counts any non-zero byte as a passed level (its
 * token counter at 0x80073458 and the Level Select stop at the first zero),
 * so a non-zero boss byte means the boss is defeated. */
#define TS2_LEVEL_ID          0x800A16A8u
#define TS2_LEVEL_SLOT        0x800A1530u
#define TS2_TOKEN_BYTES       0x800C1617u   /* + level ID */
#define TS2_LEVEL_COUNT       15u
#define TS2_TOKEN_MASK        0x1Fu
#define TS2_TOKENS_PER_LEVEL  5u

static const uint8_t ts2_slot_level_id[TS2_LEVEL_COUNT] = {
    1, 2, 6, 4, 5, 3, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

/* The game's own names, in play order (front-end strings, /BITS/MEMORY.BIN). */
static const char* const ts2_level_names[TS2_LEVEL_COUNT] = {
    "andy's house", "andy's neighborhood", "bombs away", "construction yard",
    "alleys and gullies", "slime time", "al's toy barn", "al's space land",
    "toy barn encounter", "elevator hop", "al's penthouse",
    "the evil emperor zurg", "airport infiltration", "tarmac trouble",
    "final showdown",
};

/* Each level's five token objectives, by level ID and in token-bit order:
 * 0 Hamm's 50 coins, 1 a lost-items quest, 2 a race or challenge, 3 a secret
 * or puzzle, 4 the mini-boss. The objectives come from the level's own hint
 * table (indexed by token bit) and NPC dialogue; the wording is ours, at most
 * PT_ROW_LEN - PT_ROW_LABEL_COL = 18 characters. Boss levels have no row.
 *
 * Not yet confirmed in play: the bit-2 labels marked "unverified". Their hint
 * only says the NPC "has a challenge"; the task comes from a dialogue line in
 * the same level that does not name its speaker. */
static const char* const pt_level_objectives[TS2_LEVEL_COUNT + 1u][TS2_TOKENS_PER_LEVEL] = {
    [1] = { "50 coins for hamm", "find 5 sheep", "race r.c. car",
            "push shelf boxes", "defeat tin robot" },
    [2] = { "50 coins for hamm", "find 5 lost troops", "race r.c. car",
            "inflate the duck", "defeat zurg kite" },
    [4] = { "50 coins for hamm", "find 5 tikes",
            "fetch 5 wrenches",           /* unverified: Slinky */
            "mix the paint", "defeat jackhammer" },
    [5] = { "50 coins for hamm", "find 5 ducklings",
            "fetch 5 bones",              /* unverified: Slinky */
            "float on balloon", "defeat clown top" },
    [7] = { "50 coins for hamm", "find 5 chicks",
            "reach hatching egg",         /* unverified: Rooster */
            "unlock the drawers", "defeat dinosaur" },
    [8] = { "50 coins for hamm", "find 5 aliens", "win zipline race",
            "use claw machine", "defeat buzz buggy" },
    [10] = { "50 coins for hamm", "find 5 baby mice",
             "follow token down",         /* unverified: Father Mouse */
             "solve wire puzzle", "beat spider gunro" },
    [11] = { "50 coins for hamm", "find 5 critters",
             "fetch 5 horseshoes",        /* unverified: Bullseye */
             "solve train puzzle", "defeat gunslinger" },
    [13] = { "50 coins for hamm", "find 5 passengers",
             "fetch 5 weights",           /* unverified: Rocky Gibraltar */
             "reach broken plane", "defeat prospector" },
    [14] = { "50 coins for hamm", "find lost luggage",
             "path without jumps",        /* unverified: Slinky */
             "solve light puzzle", "defeat blacksmith" },
};

typedef struct {
    int valid;        /* ID and slot read and consistent */
    int boss;         /* a boss level: no tokens */
    uint32_t id;
    uint32_t slot;
    uint32_t bits;    /* collected tokens, bit i = objective i */
    int defeated;     /* boss level: the level byte is non-zero */
} pt_level_t;

static pt_level_t pt_read_level(void)
{
    pt_level_t level = { 0, 0, 0, 0, 0, 0 };
    const uint32_t id = psx_mod_read_word(TS2_LEVEL_ID);
    const int16_t slot = (int16_t)psx_mod_read_half(TS2_LEVEL_SLOT);
    if (slot < 0 || (uint32_t)slot >= TS2_LEVEL_COUNT ||
        ts2_slot_level_id[slot] != id)
        return level;
    level.valid = 1;
    level.id = id;
    level.slot = (uint32_t)slot;
    level.boss = id % 3u == 0u;
    const uint8_t byte = psx_mod_read_byte(TS2_TOKEN_BYTES + id);
    if (level.boss)
        level.defeated = byte != 0u;
    else
        level.bits = byte & TS2_TOKEN_MASK;
    return level;
}

static uint32_t pt_count_bits(uint32_t bits)
{
    uint32_t n = 0;
    for (; bits; bits &= bits - 1u)
        ++n;
    return n;
}

/* The label from PT_ROW_LABEL_COL in PT_ROW_LEN columns. */
static void pt_row_string(char out[PT_ROW_LEN + 1u], const char* label)
{
    uint32_t i = 0;
    for (; i < PT_ROW_LEN; ++i)
        out[i] = ' ';
    out[PT_ROW_LEN] = '\0';
    for (i = 0; label[i] && PT_ROW_LABEL_COL + i < PT_ROW_LEN; ++i)
        out[PT_ROW_LABEL_COL + i] = label[i];
}

/* A solid rectangle from the game's own UI sprite, in 512-wide screen space. */
static void pt_rect(struct CPUState* cpu, uint32_t frame, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, uint32_t r, uint32_t g, uint32_t b,
                    uint32_t mode)
{
    psx_mod_write_word(frame + 0x10u, r);
    psx_mod_write_word(frame + 0x14u, g);
    psx_mod_write_word(frame + 0x18u, b);
    psx_mod_write_word(frame + 0x1Cu, mode);
    psx_mod_write_word(frame + 0x20u, w << 12);
    psx_mod_write_word(frame + 0x24u, h << 12);
    (void)psx_mod_call_guest(cpu, TS2_DRAW_SPRITE, cpu->gpr[31], x, y,
                             TS2_SPRITE_SET_UI, TS2_SPRITE_SOLID);
}

static void pt_line(struct CPUState* cpu, uint32_t frame, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h)
{
    pt_rect(cpu, frame, x, y, w, h, PT_LINE_R, PT_LINE_G, PT_LINE_B, TS2_MODE_OPAQUE);
}

static void pt_spans(struct CPUState* cpu, uint32_t frame, uint32_t x, uint32_t y,
                     const pt_span_t* spans, uint32_t n, uint32_t r, uint32_t g)
{
    for (uint32_t i = 0; i < n; ++i)
        pt_rect(cpu, frame, x + spans[i].x, y + spans[i].y, spans[i].w, 1u,
                r, g, 0u, TS2_MODE_OPAQUE);
}

/* Front to back: check mark, yellow (an additive layer over an opaque one
 * for a done box), then the outline. */
static void pt_state_box(struct CPUState* cpu, uint32_t frame, uint32_t y, int done)
{
    if (done) {
        pt_spans(cpu, frame, PT_BOX_X, y, pt_check,
                 sizeof pt_check / sizeof pt_check[0], PT_INK, PT_INK);
        pt_rect(cpu, frame, PT_BOX_X, y, PT_BOX_W, PT_BOX_H,
                PT_BOX_ON_R, PT_BOX_ON_G, 0u, TS2_MODE_ADD);
        pt_rect(cpu, frame, PT_BOX_X, y, PT_BOX_W, PT_BOX_H,
                PT_BOX_ON_R, PT_BOX_ON_G, 0u, TS2_MODE_OPAQUE);
    } else {
        pt_rect(cpu, frame, PT_BOX_X, y, PT_BOX_W, PT_BOX_H,
                PT_BOX_OFF_R, PT_BOX_OFF_G, 0u, TS2_MODE_OPAQUE);
    }
    pt_rect(cpu, frame, PT_BOX_X - 1u, y - 1u, PT_BOX_W + 2u, PT_BOX_H + 2u,
            PT_INK, PT_INK, 0u, TS2_MODE_OPAQUE);
}

/* "3 / 5 tokens": the text with the slash column blank, then the slash drawn
 * into that column. Text is centred at x 160 of the 320-wide space, 8 units
 * per character, and scaled by 8/5 onto the 512-wide screen. */
static void pt_token_line(struct CPUState* cpu, uint32_t frame, uint32_t have)
{
    char line[] = "0   0 tokens";
    line[0] = (char)('0' + have % 10u);
    line[4] = (char)('0' + TS2_TOKENS_PER_LEVEL);
    const uint32_t len = sizeof line - 1u;
    pt_text(cpu, frame, PT_TOKENS_Y, pt_string(frame, line),
            PT_TOKENS_TINT, PT_TOKENS_TINT, 0u);
    const uint32_t cell = (160u - len * 4u + 2u * 8u) * 8u / 5u;
    pt_spans(cpu, frame, cell, PT_TOKENS_Y, pt_slash,
             sizeof pt_slash / sizeof pt_slash[0], PT_SLASH_R, PT_SLASH_G);
}

/* Each primitive is linked in front of the HUD ordering-table slot, so the
 * last one drawn here ends up at the back: text, slash and state boxes
 * first, then the lines, then the fill layers. Done rows show a bright label
 * and a checked box, open rows a dimmed label and an empty, darker box. A
 * boss level shows whether its boss is defeated instead of a token count,
 * and a level the mod cannot identify only the title and a short notice. */
static void pt_progress_page(struct CPUState* cpu, uint32_t frame)
{
    const pt_level_t level = pt_read_level();
    char row[PT_ROW_LEN + 1u];
    pt_text(cpu, frame, PT_TITLE_Y, pt_string(frame, "level progress"),
            PT_BRIGHT, PT_BRIGHT, 0u);
    if (!level.valid) {
        pt_text(cpu, frame, PT_LEVEL_Y, pt_string(frame, "no level data"),
                PT_DIM, PT_DIM, 0u);
    } else {
        pt_text(cpu, frame, PT_LEVEL_Y, pt_string(frame, ts2_level_names[level.slot]),
                PT_SUBTLE, PT_SUBTLE, 0u);
        if (level.boss) {
            pt_text(cpu, frame, PT_TOKENS_Y, pt_string(frame, "boss level"),
                    PT_TOKENS_TINT, PT_TOKENS_TINT, 0u);
            if (level.defeated)
                pt_text(cpu, frame, PT_ROW_Y + PT_ROW_STEP, pt_string(frame, "boss defeated"),
                        PT_BRIGHT, PT_BRIGHT, 0u);
            else
                pt_text(cpu, frame, PT_ROW_Y + PT_ROW_STEP,
                        pt_string(frame, "boss not defeated"), PT_DIM, PT_DIM, 0u);
        } else {
            pt_token_line(cpu, frame, pt_count_bits(level.bits));
            for (uint32_t i = 0; i < TS2_TOKENS_PER_LEVEL; ++i) {
                const uint32_t y = PT_ROW_Y + i * PT_ROW_STEP;
                const int done = (int)((level.bits >> i) & 1u);
                const uint32_t label = done ? PT_BRIGHT : PT_DIM;
                pt_row_string(row, pt_level_objectives[level.id][i]);
                pt_text(cpu, frame, y, pt_string(frame, row), label, label, 0u);
                pt_state_box(cpu, frame, y, done);
            }
        }
    }

    const uint32_t right = PT_PANEL_X + PT_PANEL_W - 1u;
    const uint32_t bottom = PT_PANEL_Y + PT_PANEL_H - 1u;
    pt_line(cpu, frame, PT_PANEL_X + PT_RULE_INSET, PT_RULE_Y,
            PT_PANEL_W - 2u * PT_RULE_INSET, 1u);
    pt_line(cpu, frame, PT_PANEL_X, PT_PANEL_Y, PT_PANEL_W, 1u);
    pt_line(cpu, frame, PT_PANEL_X, bottom, PT_PANEL_W, 1u);
    pt_line(cpu, frame, PT_PANEL_X, PT_PANEL_Y + 1u, 1u, PT_PANEL_H - 2u);
    pt_line(cpu, frame, right, PT_PANEL_Y + 1u, 1u, PT_PANEL_H - 2u);
    for (int layer = 0; layer < PT_FILL_LAYERS; ++layer)
        pt_rect(cpu, frame, PT_PANEL_X + 1u, PT_PANEL_Y + 1u, PT_PANEL_W - 2u,
                PT_PANEL_H - 2u, PT_FILL_R, PT_FILL_G, PT_FILL_B, TS2_MODE_BLEND);
}

static int pt_draw_filter(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    if (cpu->gpr[31] != TS2_AFTER_MENU_RA || cpu->gpr[4] != 0u)
        return 0;
    const int view = pt_view;
    pt_view = PT_VIEW_NONE;
    if (!pt_paused()) {
        pt_reset();
        return 0;
    }
    const int progress = view == PT_VIEW_PROGRESS && pt_page;
    const int menu = view == PT_VIEW_MENU && !pt_page &&
                     psx_mod_read_half(TS2_PAUSE_PAGE) == 0;
    if (!progress && !menu)
        return 0;

    /* psx_mod_call_guest restores every register to its value at the call,
     * $sp included, so the lowered $sp holds for all calls and is put back
     * here; 0x80074DEC then runs unchanged. */
    const uint32_t sp = cpu->gpr[29];
    const uint32_t frame = (sp - PT_SCRATCH) & ~7u;
    cpu->gpr[29] = frame;
    if (progress)
        pt_progress_page(cpu, frame);
    pt_dots(cpu, frame, progress);
    cpu->gpr[29] = sp;
    return 0;
}

static void progress_tracker_activate(void)
{
    pt_reset();
}

/* Host state is not part of a savestate: a restored state shows the original
 * menu, whose page and cursor the mod never changed. */
static void progress_tracker_savestate(void)
{
    pt_reset();
}

PSX_MOD_CONSTRUCTOR(register_progress_tracker)
{
    (void)psx_mod_register_activation_plugin("toystory2.progress-tracker",
                                             progress_tracker_activate);
    (void)psx_mod_register_savestate_plugin("toystory2.progress-tracker",
                                            progress_tracker_savestate);
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.progress-tracker", TS2_PAUSE_INPUT, pt_input_filter);
    (void)psx_mod_register_instruction_plugin("toystory2.progress-tracker",
        TS2_MENU_SHOW_SITE, TS2_BEQ_MENU_HIDDEN, pt_menu_site);
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.progress-tracker", TS2_HUD_SLOT, pt_draw_filter);
}
