#ifndef PSXRECOMP_PSX_CYCLES_H
#define PSXRECOMP_PSX_CYCLES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PSX guest CPU cycle clock — the single source of truth for guest-
 * visible time. Peripherals derive all schedules from this counter.
 *
 * Phase 1.0e-a (scaffold): the counter exists and psx_advance_cycles is
 * defined, but no caller invokes psx_advance_cycles yet. Validation:
 * counter remains 0 throughout a normal session. */
extern uint64_t psx_cycle_count;

/* Advance guest time. Negative or zero is a no-op. Charges accumulate into
 * psx_cycle_count cheaply; devices are serviced at event deadlines (see
 * psx_cycles.c "Event-deadline device servicing"). */
void psx_advance_cycles(uint32_t cycles);

/* Diagnostic replay clock: advance the CPU-visible guest clock without
 * servicing devices, then restore the authoritative live clock afterward.
 * Used only while g_ls_replay_active is set. */
int      psx_cycle_replay_begin(uint64_t start_cycle);
uint64_t psx_cycle_replay_end(void);

/* Transactional I-cache view for the overlay differential harness. The
 * interpreter keeps the authoritative post-call cache; native replay starts
 * from the saved entry tags, mutates a temporary view, then restores live. */
int      psx_icache_shadow_record_begin(void);
int      psx_icache_shadow_replay_begin(void);
void     psx_icache_shadow_replay_end(void);
void     psx_icache_shadow_abort(void);

/* Event-deadline device model (psx_cycles.c): catch every device up to the
 * charged guest-cycle position and force a deadline recompute. memory.c calls
 * this at the top of every device-MMIO read/write so handlers always see
 * current device state and re-arming writes shorten the next service. */
void psx_devices_mmio_sync(void);

/* Read accessor for telemetry. */
uint64_t psx_get_cycle_count(void);

/* Idle-loop cycle skip (see psx_cycles.c "Idle-loop cycle skip"). Called by
 * psx_check_interrupts with the resume PC of the current check; detects
 * side-effect-free poll loops and fast-forwards guest time to the next
 * internal device event in whole loop quanta (bit-exact vs executing them).
 * Counters/toggle are the TCP `idle_skip` command's surface. */
struct CPUState;
void psx_idle_note_check(struct CPUState *cpu, uint32_t check_pc);
int  psx_idle_skip_is_enabled(void);
extern int      g_idle_skip_enabled;
extern uint64_t g_idle_skip_count;
extern uint64_t g_idle_skip_cycles;
extern uint32_t g_idle_skip_last_pc;
extern uint32_t g_idle_skip_last_quantum;

/* Save-state restore: re-anchor the deadline device model after psx_cycle_count
 * is overwritten from a snapshot (call once, right after boot_state_load). */
void psx_cycles_resync_after_restore(void);

#ifdef __cplusplus
}
#endif

#endif /* PSXRECOMP_PSX_CYCLES_H */
