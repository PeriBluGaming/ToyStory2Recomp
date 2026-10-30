/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include <stdint.h>
#include <stdlib.h>

#include "cpu_state.h"
#include "mod_plugins.h"

/* Modern Camera: the right analog stick orbits the camera around Buzz.
 *
 * 0x80067440 builds the follow camera's target (0x800BA048) every gameplay
 * frame: it orbits Buzz at yaw +0x28 and elevation +0x2E, runs the original
 * collision raycasts and aims the look-at at Buzz. The mod only turns that
 * orbit before the update runs, so collision, look-at and camera-relative
 * movement stay the game's own. Angles are 12-bit (4096 = 360 degrees). */
#define TS2_CAM_TARGET_UPDATE 0x80067440u
#define TS2_CAM_ORBIT_YAW     0x800BA070u
#define TS2_CAM_ORBIT_ELEV    0x800BA076u
#define MC_ELEV_MIN           (-64)
#define MC_ELEV_MAX           512

/* While Buzz moves, the update first lets obstacle avoidance raise or lower
 * the elevation (flags +0x32 bits 2/3, clamped to -512..768), then returns it
 * toward 64 by 8*dt when neither flag is set. That return is gated by the
 * `andi $v0,$v0,0xC` on the flags at 0x800685E8; while the stick owns the
 * camera the mod returns the elevation toward the player's choice at the same
 * rate and sets a flag bit in $v0 only, which skips the return to 64. */
#define TS2_ANDI_AVOID_ELEV   0x3042000Cu
#define TS2_ELEV_RETURN_SITE  0x800685E8u
#define TS2_AVOID_ELEV_FLAGS  0x000Cu

/* The pause-menu Camera Mode is bit 0x40 of 0x800C160C (set = Active, the
 * default). Active pulls the camera behind Buzz every frame, which would undo
 * any stick turn. The update reads the bit at three `andi $v0,$v0,0x40` sites;
 * while the stick owns the camera the mod clears it in $v0 there, so that
 * update runs the game's Passive logic. RAM is never written, so a savestate
 * or rewind snapshot taken mid-frame cannot store a changed Camera Mode. */
#define TS2_ANDI_CAMERA_MODE  0x30420040u
#define TS2_CAMERA_MODE_ACTIVE 0x40u
static const uint32_t ts2_camera_mode_sites[] = { 0x80067688u, 0x80067898u, 0x80067DE8u };

/* Passive recentres after the idle timer (gp+0x654) reaches 240; L2/R2 set it
 * to 66. The mod does the same while the stick owns the camera, so the chosen
 * angle holds. */
#define TS2_AUTO_ALIGN_TIMER  0x800A132Cu
#define TS2_AUTO_ALIGN_HOLD   66

#define TS2_FRAME_VBLANKS     0x800A1174u   /* game dt in 60 Hz VBlanks */
#define TS2_PAD_GAME          0x800A15BCu   /* game button word */
#define TS2_PAD_CAMERA_L2R2   0x0300u
#define TS2_CAM_TRANSITION    0x800B2254u   /* fade/transition state machine */
#define TS2_CAM_FLAGS         0x800B2256u   /* bit 0: scripted (letterbox) camera */
#define TS2_CAM_BLEND         0x800A1498u   /* gp+0x7C0: blend toward a new target */
#define TS2_DEMO_PLAYBACK     0x800A120Cu   /* 1 while the attract demo replays input */

/* R3 recenters the camera behind Buzz. R3 is read from the guest's own pad
 * receive buffer: byte 0 is 0x00 for a connected pad, byte 1 the pad id, and
 * the active-low button word at +2 carries R3 (bit 2) in digital (0x41) and
 * DualShock (0x73) mode alike. The game never reads R3, and only reading the
 * buffer leaves its input untouched. "Behind Buzz" is orbit yaw == Buzz yaw
 * (0x800B2196), with no offset; 64 is the game's own normal elevation. */
#define TS2_PAD_RX_BUFFER     0x800CF8A0u
#define TS2_PAD_BTN_SELECT    0x0001u
#define TS2_PAD_BTN_R3        0x0004u
#define TS2_BUZZ_YAW          0x800B2196u
#define MC_ELEV_NORMAL        64
#define MC_RECENTER_VBLANKS   24             /* 0.4 s */

#define MC_PACKAGE "toystory2.enhancement.modern-camera"
#define MC_FEATURE "modern-camera"

static int32_t mc_speed_deg = 120;     /* full-deflection turn rate, degrees/s */
static int32_t mc_vspeed_deg = 60;     /* full-deflection tilt rate, degrees/s */
static int32_t mc_deadzone_pct = 15;
static int mc_invert_x;
static int mc_invert_y;
static int mc_recenter_enabled = 1;

static int mc_free;           /* stick has taken the camera; held until L2/R2 */
static int mc_mask_mode;      /* clear the Camera Mode bit in this update */
static int32_t mc_elev_goal;  /* elevation the player chose */
static int32_t mc_dt = 1;     /* game dt of this update, in VBlanks */
static int64_t mc_yaw_carry;  /* sub-unit yaw, 8 fractional bits */
static int64_t mc_elev_carry; /* sub-unit elevation, 8 fractional bits */
static int mc_recentering;    /* R3 recenter animation running */
static int32_t mc_recenter_left; /* VBlanks of the animation still to run */
static int mc_r3_held = 1;    /* R3 state of the last update; starts held so a
                               * button held into a session never triggers */

static long mc_option_number(const char* id, long fallback, long lo, long hi)
{
    char text[32] = "";
    if (!psx_mod_option_value(MC_PACKAGE, MC_FEATURE, id, text, sizeof text) || !text[0])
        return fallback;
    char* end = NULL;
    const long value = strtol(text, &end, 10);
    if (!end || *end != '\0' || value < lo || value > hi)
        return fallback;
    return value;
}

static int mc_option_flag(const char* id, int fallback)
{
    char text[16] = "";
    if (!psx_mod_option_value(MC_PACKAGE, MC_FEATURE, id, text, sizeof text) || !text[0])
        return fallback;
    return text[0] == 't';
}

static void mc_release(void)
{
    mc_free = 0;
    mc_mask_mode = 0;
    mc_yaw_carry = 0;
    mc_elev_carry = 0;
    mc_recentering = 0;
}

static int32_t mc_clamp(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Scripted cameras, transitions and the attract demo keep the original logic. */
static int mc_camera_is_scripted(void)
{
    return (psx_mod_read_half(TS2_CAM_FLAGS) & 1u) != 0 ||
           psx_mod_read_half(TS2_CAM_TRANSITION) != 0 ||
           (int32_t)psx_mod_read_word(TS2_CAM_BLEND) > 0 ||
           psx_mod_read_word(TS2_DEMO_PLAYBACK) == 1u;
}

/* One stick axis byte as -1024..1024 after the deadzone, 0 when centred. */
static int32_t mc_axis(uint8_t byte, int invert)
{
    int32_t v = (int32_t)byte - 128;
    const int32_t dead = mc_deadzone_pct * 128 / 100;
    const int32_t mag = v < 0 ? -v : v;
    if (mag <= dead || dead >= 127)
        return 0;
    int32_t scaled = (mag - dead) * 1024 / (127 - dead);
    if (scaled > 1024)
        scaled = 1024;
    if (invert)
        v = -v;
    return v < 0 ? -scaled : scaled;
}

/* Angle units for this update: stick/1024 * deg/s * 4096/360 * dt/60, with
 * the remainder carried in 1/256 units so slow turns stay smooth. */
static int32_t mc_step(int64_t* carry, int32_t stick, int32_t deg_per_s)
{
    *carry += (int64_t)stick * deg_per_s * 4096 * mc_dt * 256 / (1024LL * 360 * 60);
    const int32_t units = (int32_t)(*carry / 256);
    *carry -= (int64_t)units * 256;
    return units;
}

/* Shortest signed turn from `from` to `to`, -2048..2047 (as the game wraps). */
static int32_t mc_angle_delta(int32_t from, int32_t to)
{
    return ((to - from + 2048) & 0x0FFF) - 2048;
}

/* R3 just pressed: down now, up in the last update. Select+R3 is the host's
 * Rewind shortcut and never recenters. A pad that is not answering reads as
 * held, so reconnecting with R3 down does not trigger either. */
static int mc_r3_pressed(void)
{
    const uint32_t word = psx_mod_read_word(TS2_PAD_RX_BUFFER);
    const uint32_t id = (word >> 8) & 0xF0u;
    const uint32_t buttons = ~(word >> 16) & 0xFFFFu;   /* active-high */
    const int valid = (word & 0xFFu) == 0x00u && (id == 0x40u || id == 0x50u || id == 0x70u);
    const int held = !valid || (buttons & TS2_PAD_BTN_R3) != 0;
    const int pressed = valid && held && !mc_r3_held && !(buttons & TS2_PAD_BTN_SELECT);
    mc_r3_held = held;
    return pressed;
}

/* Quadratic ease-out toward behind Buzz and the normal elevation. The share
 * of the remaining distance covered this update is 1 - ((T - dt) / T)^2, so
 * the remaining distance falls as (1 - t)^2 and the last update lands
 * exactly; Buzz's yaw is re-read every update, so a turn during the
 * animation is followed. The elevation moves through the goal, like the
 * stick does, so an avoidance offset the game is applying is kept. */
static void mc_recenter_step(void)
{
    const int32_t left = mc_recenter_left;
    const int32_t after = left > mc_dt ? left - mc_dt : 0;
    const int64_t keep = (int64_t)after * after * 4096 / ((int64_t)left * left);
    const int32_t share = 4096 - (int32_t)keep;            /* 0..4096 */

    const int32_t yaw = psx_mod_read_half(TS2_CAM_ORBIT_YAW) & 0x0FFF;
    const int32_t buzz = psx_mod_read_half(TS2_BUZZ_YAW) & 0x0FFF;
    const int32_t dyaw = mc_angle_delta(yaw, buzz);
    const int32_t turn = after == 0 ? dyaw : (int32_t)((int64_t)dyaw * share / 4096);
    psx_mod_write_half(TS2_CAM_ORBIT_YAW, (uint16_t)((yaw + turn) & 0x0FFF));

    const int32_t delev = MC_ELEV_NORMAL - mc_elev_goal;
    const int32_t goal = after == 0 ? MC_ELEV_NORMAL
                                    : mc_elev_goal + (int32_t)((int64_t)delev * share / 4096);
    const int32_t elev = (int16_t)psx_mod_read_half(TS2_CAM_ORBIT_ELEV);
    psx_mod_write_half(TS2_CAM_ORBIT_ELEV,
                       (uint16_t)mc_clamp(elev + goal - mc_elev_goal, -512, 768));
    mc_elev_goal = goal;

    mc_recenter_left = after;
    if (after == 0)
        mc_recentering = 0;   /* done; the camera stays free behind Buzz */
}

static int modern_camera_update_filter(struct CPUState* cpu, uint32_t address)
{
    (void)cpu;
    (void)address;
    mc_mask_mode = 0;
    /* Track R3 every update, so a press during a scripted camera is not
     * remembered for later. */
    const int r3_pressed = mc_r3_pressed();
    if (mc_camera_is_scripted()) {
        mc_release();
        return 0;
    }
    /* L2/R2 keep their Camera Mode meaning and hand the camera back. */
    if (psx_mod_read_half(TS2_PAD_GAME) & TS2_PAD_CAMERA_L2R2) {
        mc_release();
        return 0;
    }

    uint8_t sticks[4];
    if (!psx_mod_read_local_pad_sticks(0, sticks))
        sticks[2] = sticks[3] = 0x80;
    const int32_t sx = mc_axis(sticks[2], mc_invert_x);
    /* Stick up (low byte) looks up, which lowers the camera. */
    const int32_t sy = mc_axis(sticks[3], mc_invert_y);
    mc_dt = mc_clamp((int32_t)psx_mod_read_word(TS2_FRAME_VBLANKS), 1, 4);

    if ((sx != 0 || sy != 0 || (r3_pressed && mc_recenter_enabled)) && !mc_free) {
        mc_free = 1;
        mc_elev_goal = mc_clamp((int16_t)psx_mod_read_half(TS2_CAM_ORBIT_ELEV),
                                MC_ELEV_MIN, MC_ELEV_MAX);
    }
    /* The stick always wins: it ends a recenter where it is and continues
     * from the goal reached so far. */
    if (sx != 0 || sy != 0) {
        mc_recentering = 0;
    } else if (r3_pressed && mc_recenter_enabled) {
        mc_recentering = 1;
        mc_recenter_left = MC_RECENTER_VBLANKS;
        mc_yaw_carry = 0;
        mc_elev_carry = 0;
    }
    if (mc_recentering)
        mc_recenter_step();
    if (sx != 0) {
        const uint16_t yaw = psx_mod_read_half(TS2_CAM_ORBIT_YAW);
        psx_mod_write_half(TS2_CAM_ORBIT_YAW,
                           (uint16_t)((yaw + mc_step(&mc_yaw_carry, sx, mc_speed_deg)) & 0x0FFFu));
    }
    if (sy != 0) {
        /* Move the goal and the live elevation together, so an avoidance
         * offset the game is applying right now is kept. */
        const int32_t goal = mc_clamp(mc_elev_goal + mc_step(&mc_elev_carry, sy, mc_vspeed_deg),
                                      MC_ELEV_MIN, MC_ELEV_MAX);
        const int32_t elev = (int16_t)psx_mod_read_half(TS2_CAM_ORBIT_ELEV);
        psx_mod_write_half(TS2_CAM_ORBIT_ELEV,
                           (uint16_t)mc_clamp(elev + goal - mc_elev_goal, -512, 768));
        mc_elev_goal = goal;
    }
    if (mc_free) {
        mc_mask_mode = 1;
        if ((int32_t)psx_mod_read_word(TS2_AUTO_ALIGN_TIMER) > TS2_AUTO_ALIGN_HOLD)
            psx_mod_write_word(TS2_AUTO_ALIGN_TIMER, TS2_AUTO_ALIGN_HOLD);
    }
    return 0;
}

static void modern_camera_mode_read(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    if (mc_mask_mode)
        cpu->gpr[2] &= ~TS2_CAMERA_MODE_ACTIVE;
}

/* $v0 holds the avoidance flags. With an avoidance flag set the game keeps
 * its own result; otherwise move toward the chosen elevation by the game's
 * 8*dt and skip the return to 64. */
static void modern_camera_elev_return(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    if (!mc_mask_mode || (cpu->gpr[2] & TS2_AVOID_ELEV_FLAGS))
        return;
    const int32_t rate = 8 * mc_dt;
    int32_t elev = (int16_t)psx_mod_read_half(TS2_CAM_ORBIT_ELEV);
    if (elev > mc_elev_goal)
        elev = elev - rate < mc_elev_goal ? mc_elev_goal : elev - rate;
    else if (elev < mc_elev_goal)
        elev = elev + rate > mc_elev_goal ? mc_elev_goal : elev + rate;
    psx_mod_write_half(TS2_CAM_ORBIT_ELEV, (uint16_t)elev);
    cpu->gpr[2] |= TS2_AVOID_ELEV_FLAGS;
}

static void modern_camera_activate(void)
{
    mc_speed_deg = (int32_t)mc_option_number("speed", 120, 30, 360);
    mc_vspeed_deg = (int32_t)mc_option_number("vspeed", 60, 10, 180);
    mc_deadzone_pct = (int32_t)mc_option_number("deadzone", 15, 0, 50);
    mc_invert_x = mc_option_flag("invert_x", 0);
    mc_invert_y = mc_option_flag("invert_y", 0);
    mc_recenter_enabled = mc_option_flag("r3_recenter", 1);
    mc_release();
    mc_r3_held = 1;
}

/* Host state is not part of a savestate: start each restored state with the
 * original camera until the stick or R3 is used again. A recenter in progress
 * ends, and R3 must be released before it counts again. */
static void modern_camera_savestate(void)
{
    mc_release();
    mc_r3_held = 1;
}

PSX_MOD_CONSTRUCTOR(register_modern_camera)
{
    (void)psx_mod_register_activation_plugin("toystory2.modern-camera", modern_camera_activate);
    (void)psx_mod_register_savestate_plugin("toystory2.modern-camera", modern_camera_savestate);
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.modern-camera", TS2_CAM_TARGET_UPDATE, modern_camera_update_filter);
    for (unsigned i = 0; i < sizeof ts2_camera_mode_sites / sizeof ts2_camera_mode_sites[0]; ++i)
        (void)psx_mod_register_instruction_plugin("toystory2.modern-camera",
            ts2_camera_mode_sites[i], TS2_ANDI_CAMERA_MODE, modern_camera_mode_read);
    (void)psx_mod_register_instruction_plugin("toystory2.modern-camera",
        TS2_ELEV_RETURN_SITE, TS2_ANDI_AVOID_ELEV, modern_camera_elev_return);
}
