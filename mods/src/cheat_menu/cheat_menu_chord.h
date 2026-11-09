/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 * SPDX-FileCopyrightText: 2026 PeriBluGaming */
#ifndef TS2_CHEAT_MENU_CHORD_H
#define TS2_CHEAT_MENU_CHORD_H

/* Rising-edge detection for the Select + L3 menu chord. Pure logic, no SDL,
 * so it can be tested on its own.
 *
 * Two sources describe the buttons, and neither is enough alone:
 *
 *  - Processed button events. They arrive in order, but the F7 save-state
 *    menu and the Rewind screen poll SDL themselves and never forward events
 *    to the menu provider, so a release made there is never seen: an
 *    event-kept bit can go stale and later complete the chord on L3 alone.
 *  - SDL's live button state. It is never stale, but SDL updates it when it
 *    pumps input, before the queued events are processed. When Select and L3
 *    go down within one poll interval, each of the two down events already
 *    sees the other button held, and a check against live state alone fires
 *    on both: the menu opens and closes again at once.
 *
 * So the chord fires on a button-down event when the other button is held by
 * BOTH sources: its own down event was already processed (the events say so)
 * and it is still down (the live state says so). A down event means its
 * button was up just before, so the chord was inactive then; firing on it is
 * exactly the transition from inactive to active, once per press. Of two
 * down events in one poll interval only the second sees the first in the
 * event mask, so only it fires.
 *
 * Stale bits are dropped whenever the live state shows their button up: on
 * every chord event and on cm_chord_resync(), which the provider calls once
 * per frame after the runtime has drained the event queue. Bits are only
 * ever cleared from live state, never set, so a press whose event is still
 * queued cannot be counted twice.
 *
 * One gamepad is tracked: the one that sent the last Select or L3 event. A
 * chord event from another gamepad starts over, so buttons on two different
 * pads never combine. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CM_CHORD_SELECT = 1u,
    CM_CHORD_L3 = 2u,
    CM_CHORD_BOTH = CM_CHORD_SELECT | CM_CHORD_L3
};

typedef struct CmChord {
    uint32_t pad;      /* tracked gamepad (SDL_JoystickID) */
    int has_pad;
    unsigned mask;     /* CM_CHORD_* whose down event was processed, not yet
                          released, and not contradicted by live state */
} CmChord;

/* One processed Select or L3 event. `bit` is CM_CHORD_SELECT or
 * CM_CHORD_L3, `live` the pad's live CM_CHORD_* state (0 when SDL no longer
 * knows the pad). Returns 1 when this press completes the chord. */
static inline int cm_chord_button(CmChord *c, uint32_t pad, unsigned bit,
                                  int down, unsigned live)
{
    const unsigned other = CM_CHORD_BOTH & ~bit;
    int fire;
    if (!c->has_pad || c->pad != pad) {
        c->pad = pad;
        c->has_pad = 1;
        c->mask = 0;
    }
    if (!down) {
        c->mask &= ~bit;
        return 0;
    }
    fire = (c->mask & other) != 0 && (live & other) != 0;
    /* This button's own event is authoritative even if a quick tap already
     * released it live; its release event clears it. */
    c->mask = (c->mask | bit) & (live | bit);
    return fire;
}

/* Drop bits the live state contradicts (`live` as above; 0 for a pad SDL no
 * longer knows, which clears everything). */
static inline void cm_chord_resync(CmChord *c, unsigned live)
{
    if (c->has_pad) c->mask &= live;
}

static inline void cm_chord_reset(CmChord *c)
{
    c->has_pad = 0;
    c->pad = 0;
    c->mask = 0;
}

#ifdef __cplusplus
}
#endif

#endif
