// strict_translator.h
// ----------------------------------------------------------------------------
// Phase 1a strict, fail-loud MIPS-to-C translator for the BIOS boot slice.
//
// Design contract:
//   - Translates ONLY the instructions Phase 1a's bounded slice actually
//     contains. Currently: LUI, ORI, ADDIU, SW, SLL (incl. NOP), J, JAL,
//     JR, JALR, RFE.
//   - Returns {supported=false, fail_reason=...} for ANY other opcode/funct.
//     Never emits a `/* TODO */` comment, never falls through silently.
//   - Does NOT call into the salvaged CodeGenerator. Phase 1a is a clean,
//     audited path. The salvaged generator will be revisited later.
//
// If you are tempted to add a fallthrough case here: don't. Add a real
// translation, or leave it unsupported and let the build fail loud. That
// is the entire purpose of this file existing as a separate translator.

#pragma once

#include <cstdint>
#include <string>

#include "mips_decoder.h"

namespace PSXRecompV4 {

struct TranslateResult {
    bool        supported = false;  // true = c_code is valid; false = fail_reason is valid
    std::string c_code;              // C statement(s) to emit, no trailing newline
    std::string comment;             // human-readable annotation (e.g. "lui $t0, 0x0013")
    std::string fail_reason;         // populated when supported == false
    bool        is_terminator = false;  // J/JAL/JR/JALR/RFE/branches — slice walker uses this
    const char* terminator_kind = nullptr;  // "j", "jal", "jr", "jalr", "rfe", "branch_*"
    uint32_t    terminator_target = 0;  // for J/JAL/branches: computed target; else unused

    // Optional C statement(s) to be emitted BEFORE the delay slot.
    // Used by conditional branches to capture pre-delay-slot snapshots of
    // their rs/rt operands into uniquely-named function-scope locals, so
    // that the branch condition (in c_code, which is emitted AFTER the
    // delay slot) reads the architecturally-correct values. Empty for
    // every other instruction. The slice walker's emit logic emits this
    // as an EmittedInstr right before the delay slot when non-empty.
    std::string pre_delay_code;
};

class StrictTranslator {
public:
    // Translate a single decoded instruction. Pure function — no state.
    static TranslateResult translate(const PSXRecomp::DecodedInstruction& d);
};

} // namespace PSXRecompV4
