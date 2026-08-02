// full_function_emitter.h
// ----------------------------------------------------------------------------
// Phase 2: Full BIOS C emitter.
//
// Given a DiscoveryResult and the ROM image, emits:
//   - SCPH1001_full.c      (one C function per discovered BIOS function)
//   - SCPH1001_dispatch.c  (normalized-address -> function-pointer table)
//
// Design constraints:
//   - Does NOT modify strict_translator. Consumes TranslateResult metadata
//     but reconstructs terminator emission for multi-block control flow.
//   - Functions named func_{NORMALIZED:08X} (physical address after KSEG strip
//     and ROM-to-RAM alias resolution).
//   - Intra-function branches use goto + labels at basic block leaders.
//   - Inter-function calls (JAL) become direct C function calls.
//   - Indirect calls (JALR) and indirect jumps (JR non-$ra) go through
//     psx_dispatch().
//   - JR $ra becomes return.

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "config_loader.h"
#include "function_discovery.h"

namespace PSXRecompV4 {

struct EmitStats {
    uint32_t functions_emitted = 0;
    uint32_t functions_skipped = 0;  // e.g. FPU functions
    uint32_t total_instructions = 0;
    uint32_t dispatch_entries = 0;
    uint32_t continuation_entries = 0;
    std::vector<std::pair<uint32_t, std::string>> skipped;  // (addr, reason)
};

// A continuation label: the return point inside a calling function after
// a jal/jalr to a separate dispatch entry.  When the callee does jr $ra,
// the dispatch loop must route back to this label.
struct ContinuationLabel {
    uint32_t rom_addr;          // ROM address of the label (e.g., 0xBFC3F534)
    uint32_t norm_addr;         // normalized dispatch address (same as rom_addr for shell)
    uint32_t parent_func_norm;  // normalized address of the parent function
};

class FullFunctionEmitter {
public:
    // Emit SCPH1001_full.c and SCPH1001_dispatch.c into out_dir.
    // rom: flat BIOS image bytes
    // base_addr: virtual address of rom[0] (0xBFC00000)
    // rom_end: last valid address inclusive (0xBFC7FFFF)
    // dr: discovery result from Phase 1c/2 discovery pass
    // bios_sha256: hex string for provenance header
    static EmitStats emit(
        const std::vector<uint8_t>&       rom,
        uint32_t                          base_addr,
        uint32_t                          rom_end,
        const DiscoveryResult&            dr,
        const std::string&                bios_sha256,
        const std::string&                out_dir,
        const std::vector<BiosVectorTable>& bios_vectors = {},
        const std::vector<BiosAlias>&       bios_aliases = {});

private:
    // Emit a single function's C code to the output stream.
    // Populates out_continuations with labels that need dispatch entries.
    // injected_cross_targets: ROM addresses INSIDE this function that are
    //   referenced from other functions as branch/jump targets — these need
    //   to become dispatch-routable continuations of THIS function.
    // out_cross_targets: collected cross-function tail-call targets discovered
    //   while emitting THIS function. Each entry's parent_func_norm is the
    //   function that CONTAINS the target (NOT the current emitting function).
    //   The top-level emit pass aggregates these and feeds them as
    //   injected_cross_targets in PASS 2.
    static bool emit_function(
        std::string&                out,
        const DiscoveredFunction&   func,
        const FunctionDiscovery::SingleFunctionResult& sfr,
        const std::set<uint32_t>&   all_function_entries,
        const std::vector<uint8_t>& rom,
        uint32_t                    base_addr,
        uint32_t                    rom_end,
        std::vector<ContinuationLabel>& out_continuations,
        const std::set<uint32_t>&   injected_cross_targets,
        std::vector<ContinuationLabel>& out_cross_targets);

    // Emit the dispatch table.
    static void emit_dispatch(
        std::string&                        out,
        const DiscoveryResult&              dr,
        const std::set<uint32_t>&           emitted_normalized,
        const std::map<uint32_t, ContinuationLabel>& continuations,
        const std::string&                  bios_sha256,
        const std::vector<uint8_t>&         rom,
        uint32_t                            base_addr,
        const std::vector<BiosVectorTable>& bios_vectors,
        const std::vector<BiosAlias>&       bios_aliases);

    static uint32_t normalize_address(uint32_t addr);
    static uint32_t read_u32_le(const std::vector<uint8_t>& rom, uint32_t offset);
};

} // namespace PSXRecompV4
