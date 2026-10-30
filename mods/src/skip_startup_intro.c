/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

/* main() (0x8007A9E8) runs the pre-title intro as:
 *   0x8007AAF8: jal 0x8007BC74   ; $a0 = 10, legal + ESRB screens
 *   0x8007AB00: if (state == 9) goto 0x8007AB58  ; skip the intro movies
 * The state word is 0x800A1374 (gp + 0x69C). The game itself stores 9 there
 * when the player skips the legal screens, so returning early with state 9
 * takes its own "intro already seen" path to the title loop. */
#define TS2_MAIN_ENTER_INTRO_RA 0x8007AB00u
#define TS2_INTRO_MODE          10u
#define TS2_STATE_WORD          0x800A1374u
#define TS2_STATE_INTRO_SKIPPED 9u

static int skip_startup_intro_filter(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    if (cpu->gpr[4] != TS2_INTRO_MODE || cpu->gpr[31] != TS2_MAIN_ENTER_INTRO_RA) {
        return 0;
    }
    psx_mod_write_word(TS2_STATE_WORD, TS2_STATE_INTRO_SKIPPED);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_skip_startup_intro)
{
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.skip-startup-intro",
        0x8007BC74u,
        skip_startup_intro_filter
    );
}
