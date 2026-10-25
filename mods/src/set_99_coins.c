#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define TS2_COINS_STORE_SITE 0x80076DACu
#define TS2_COINS_STORE_WORD 0xA4820000u

static void set_99_coins_instruction(struct CPUState* cpu, uint32_t address)
{
    if (address == TS2_COINS_STORE_SITE) {
        cpu->gpr[2] = 99;
    }
}

PSX_MOD_CONSTRUCTOR(register_set_99_coins)
{
    (void)psx_mod_register_instruction_plugin(
        "toystory2.set-99-coins",
        TS2_COINS_STORE_SITE,
        TS2_COINS_STORE_WORD,
        set_99_coins_instruction
    );
}
