#include <stdint.h>

#include "cpu_state.h"
#include "mod_plugins.h"

#define TS2_HEALTH_STORE_SITE 0x8006B18Cu
#define TS2_HEALTH_STORE_WORD 0xA423221Eu
#define TS2_HEALTH_COUNTER 0x800B221Eu

static void infinite_health_instruction(struct CPUState* cpu, uint32_t address)
{
    if (address != TS2_HEALTH_STORE_SITE) {
        return;
    }

    const int32_t current = (int16_t)psx_mod_read_half(TS2_HEALTH_COUNTER);
    const int32_t proposed = (int16_t)(uint16_t)cpu->gpr[3];

    if (current >= 0 && proposed == current - 1) {
        cpu->gpr[3] = (uint32_t)(uint16_t)current;
    }
}

PSX_MOD_CONSTRUCTOR(register_infinite_health)
{
    (void)psx_mod_register_instruction_plugin(
        "toystory2.infinite-health",
        TS2_HEALTH_STORE_SITE,
        TS2_HEALTH_STORE_WORD,
        infinite_health_instruction
    );
}
