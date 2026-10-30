/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

/* BIOS free() stub (A0:34h) in the game's text segment.
 *
 * Toy Story 2's overlay frees pointers that lie inside its own "COLA" pool,
 * not in a BIOS malloc block. The retail SCPH-1001 free() never walks the
 * heap: it only sets bit 0 of the word at a0-4 and returns. OpenBIOS walks a
 * free list through that (game-owned) memory and never returns.
 *
 * This filter reproduces the retail behaviour for the same call. Anything that
 * is not an aligned main-RAM pointer is passed through to the original body. */
#define TS2_BIOS_FREE_STUB 0x80082E8Cu
#define PSX_RAM_SIZE       0x00200000u
#define PSX_RAM_MIRROR_MASK 0x1FFFFFFFu

static int bios_free_compat_filter(struct CPUState* cpu, uint32_t address)
{
    (void)address;
    const uint32_t a0 = cpu->gpr[4];
    const uint32_t phys = a0 & PSX_RAM_MIRROR_MASK;
    const uint32_t segment = a0 >> 29;
    const int kernel_or_user_ram = (segment == 0u || segment == 4u || segment == 5u);
    if (!kernel_or_user_ram || (a0 & 3u) != 0u || phys < 8u || phys >= PSX_RAM_SIZE)
        return 0;
    const uint32_t word = a0 - 4u;
    psx_mod_write_word(word, psx_mod_read_word(word) | 1u);
    return 1;
}

PSX_MOD_CONSTRUCTOR(register_bios_free_compat)
{
    (void)psx_mod_register_function_filter_plugin(
        "toystory2.bios-free-compat", TS2_BIOS_FREE_STUB, bios_free_compat_filter);
}
