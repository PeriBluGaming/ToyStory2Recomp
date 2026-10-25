#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define TS2_LIVES_DECREMENT_SITE 0x8007B060u
#define TS2_LIVES_RESET_STORE_SITE 0x8007BE30u
#define TS2_LIVES_DECREMENT_WORD 0xA4222222u
#define TS2_LIVES_COUNTER 0x800B2222u

static void infinite_lives_instruction(struct CPUState* cpu, uint32_t address)
{
    if (address == TS2_LIVES_DECREMENT_SITE) {
        cpu->gpr[2] = cpu->gpr[3];
        return;
    }

    if (address == TS2_LIVES_RESET_STORE_SITE) {
        const int32_t current = (int16_t)psx_mod_read_half(TS2_LIVES_COUNTER);
        const int32_t proposed = (int16_t)(uint16_t)cpu->gpr[2];

        if (current >= 1 && proposed >= 0 && proposed == current - 1) {
            cpu->gpr[2] = (uint32_t)(uint16_t)current;
        }
    }
}

PSX_MOD_CONSTRUCTOR(register_infinite_lives)
{
    (void)psx_mod_register_instruction_plugin(
        "toystory2.infinite-lives",
        TS2_LIVES_DECREMENT_SITE,
        TS2_LIVES_DECREMENT_WORD,
        infinite_lives_instruction
    );
    (void)psx_mod_register_instruction_plugin(
        "toystory2.infinite-lives",
        TS2_LIVES_RESET_STORE_SITE,
        TS2_LIVES_DECREMENT_WORD,
        infinite_lives_instruction
    );
}
