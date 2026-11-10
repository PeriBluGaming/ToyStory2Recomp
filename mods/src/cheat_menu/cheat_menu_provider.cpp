// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
// SPDX-FileCopyrightText: 2026 PeriBluGaming
//
// Cheat Menu: the title's in-game menu provider (psxrecomp
// runtime/include/psx_menu_host.h), drawn with recomp-ui's runtime menu model
// and Dear ImGui over the presented frame. Rows: Resume Game, Max Coins
// (cheat_menu_view.h).
//
// Policy stays with the runtime: PsxMenuHost.set_paused holds the guest and
// audio through the host pause, and while is_open() reports 1 the runtime
// feeds the guest released pads (UI capture). Opening inside event() lets the
// same frame's host_pause_service settle that capture before the pad sample,
// and the input guard armed when the host pause ends swallows the closing
// press. This file never reads or writes guest memory.
//
// Threads: attach, event, is_open, set_open and detach run on the main
// thread; draw runs on the GL thread (the render thread when enabled).
// g_mu guards every piece of shared state.
//
// Cheats: the Max Coins row writes only to the cheat_menu_state mailbox (its
// set_value may run inside handle_input on the main thread, or in theory
// from a click inside render_imgui on the GL thread). sync_cheats_locked()
// hands the request to the cheat engine; it runs only from p_attach, p_event
// and p_is_open, i.e. on the main thread, which is also the thread that runs
// the engine's VBlank callback. Draw reads only the snapshot copy, under
// g_mu. Lock order: g_mu is the only lock here; the engine takes none and
// its VBlank callback never takes g_mu.
//
// Toggles: F10, and Select + L3 on any gamepad, in either order. The chord
// fires once per press, on the transition to both buttons held, using the
// processed button events checked against SDL's live state
// (cheat_menu_chord.h explains why both are needed): live state alone fires
// twice when both presses arrive within one poll interval, and events alone
// go stale when the F7 save-state menu or the Rewind screen swallows a
// release. is_open(), which the runtime calls once per frame after draining
// the event queue, drops such stale bits.
//
// While closed only those two inputs are consumed. In particular F7, the
// other host hotkeys and Select + R1 reach the runtime unchanged.

#include "cheat_menu.h"
#include "cheat_menu_chord.h"
#include "cheat_menu_view.h"

#include "psx_menu_host.h"
#include "recomp_runtime_ui.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#if defined(PSX_SDL3)
#include <SDL3/SDL.h>
#endif

#include <cfloat>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace {

std::mutex g_mu;
const PsxMenuHost *g_host = nullptr;   // main thread, under g_mu
RecompRuntimeUi *g_ui = nullptr;       // under g_mu
bool g_paused = false;                 // a host pause is requested
ImGuiContext *g_imgui = nullptr;       // GL thread, under g_mu
bool g_gl_ready = false;
uint64_t g_last_counter = 0;
CmChord g_chord{};                     // Select + L3 tracking, under g_mu
cheat_menu_view g_view;                // rows + cheat bridge, under g_mu

// MAIN THREAD ONLY, g_mu held. Hands a pending Max Coins request to the
// engine and refreshes the snapshot the draw callback reads. After a
// request, the footer names the resulting state (replacing the model's
// generic "Saved").
void sync_cheats_locked() {
    const bool had_request = cheat_menu_state_pending(&g_view.state, nullptr) != 0;
    (void)cheat_menu_view_sync(&g_view);
    if (had_request && g_ui) recomp_runtime_ui_set_status(g_ui, cheat_menu_view_status(&g_view));
}

// ---- Model callbacks (called with g_mu held) ------------------------------

void close_menu() {
    if (g_ui) recomp_runtime_ui_close(g_ui);
}

void toggle_menu() {
    if (!g_ui) return;
    if (recomp_runtime_ui_is_open(g_ui)) recomp_runtime_ui_close(g_ui);
    else recomp_runtime_ui_open(g_ui);
}

int cb_action(void *, const RecompRuntimeUiItem *item) {
    if (item && item->key && std::strcmp(item->key, CHEAT_MENU_KEY_RESUME) == 0) close_menu();
    return 0;
}

void cb_visibility(void *, int open) {
    if (open) {
        // Show the rows directly: the menu has a single section, so Accept
        // on the (only) section entry enters it. Public API only; the
        // section helpers are internal to recomp-ui.
        recomp_runtime_ui_handle_input(g_ui, RECOMP_RUNTIME_UI_INPUT_ACCEPT, 1, 0);
        g_paused = g_host && g_host->set_paused && g_host->set_paused(1) != 0;
    } else {
        if (g_paused && g_host && g_host->set_paused) g_host->set_paused(0);
        g_paused = false;
    }
}

// ---- SDL3 input -----------------------------------------------------------

#if defined(PSX_SDL3)
int key_input(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP: return RECOMP_RUNTIME_UI_INPUT_UP;
    case SDLK_DOWN: return RECOMP_RUNTIME_UI_INPUT_DOWN;
    case SDLK_LEFT: return RECOMP_RUNTIME_UI_INPUT_LEFT;
    case SDLK_RIGHT: return RECOMP_RUNTIME_UI_INPUT_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE: return RECOMP_RUNTIME_UI_INPUT_ACCEPT;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: return RECOMP_RUNTIME_UI_INPUT_BACK;
    default: return -1;
    }
}

int pad_input(Uint8 button) {
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return RECOMP_RUNTIME_UI_INPUT_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return RECOMP_RUNTIME_UI_INPUT_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return RECOMP_RUNTIME_UI_INPUT_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return RECOMP_RUNTIME_UI_INPUT_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH: return RECOMP_RUNTIME_UI_INPUT_ACCEPT;  // Cross
    case SDL_GAMEPAD_BUTTON_EAST: return RECOMP_RUNTIME_UI_INPUT_BACK;     // Circle
    default: return -1;
    }
}

// Back closes the menu from anywhere: it has nothing to go back to.
void menu_input(int in, bool repeat) {
    if (in == RECOMP_RUNTIME_UI_INPUT_BACK) {
        if (!repeat) close_menu();
        return;
    }
    recomp_runtime_ui_handle_input(g_ui, (RecompRuntimeUiInput)in, 1, repeat ? 1 : 0);
}

// CM_CHORD_* bit of a gamepad button, 0 for any other button.
unsigned chord_bit(Uint8 button) {
    if (button == SDL_GAMEPAD_BUTTON_BACK) return CM_CHORD_SELECT;
    if (button == SDL_GAMEPAD_BUTTON_LEFT_STICK) return CM_CHORD_L3;
    return 0;
}

// Live CM_CHORD_* state of a gamepad; 0 when SDL no longer knows it
// (disconnected), which never completes the chord and clears its bits.
unsigned chord_live(SDL_JoystickID which) {
    SDL_Gamepad *pad = SDL_GetGamepadFromID(which);
    if (!pad) return 0;
    unsigned live = 0;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_BACK)) live |= CM_CHORD_SELECT;
    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) live |= CM_CHORD_L3;
    return live;
}

// Feeds one Select or L3 event; 1 when this press completes the chord.
bool chord_event(const SDL_GamepadButtonEvent &b, bool down) {
    const unsigned bit = chord_bit(b.button);
    return bit && cm_chord_button(&g_chord, (uint32_t)b.which, bit, down ? 1 : 0,
                                  chord_live(b.which)) != 0;
}

int on_event(const SDL_Event &e) {
    const bool open = recomp_runtime_ui_is_open(g_ui) != 0;
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN: {
        const bool plain = (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) == 0;
        if (e.key.key == SDLK_F10 && plain) {
            if (!e.key.repeat) toggle_menu();
            return 1;
        }
        if (!open) return 0;
        const int in = key_input(e.key.key);
        if (in >= 0) menu_input(in, e.key.repeat);
        return 1;  // the game sees no key while the menu is open
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
        if (chord_event(e.gbutton, true)) {
            toggle_menu();
            return 1;
        }
        if (!open) return 0;
        const int in = pad_input(e.gbutton.button);
        if (in >= 0) menu_input(in, false);
        return 1;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        (void)chord_event(e.gbutton, false);
        return open ? 1 : 0;
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_TEXT_EDITING:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return open ? 1 : 0;
    default:
        // Quit, window and device added/removed events always reach the
        // runtime, so a disconnect is handled there while the menu stays
        // open and F10 still closes it.
        return 0;
    }
}
#endif  // PSX_SDL3

// ---- GL thread ------------------------------------------------------------

void release_gl_locked() {
    if (!g_imgui) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(g_imgui);
    if (g_gl_ready) ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(g_imgui);
    ImGui::SetCurrentContext(prev == g_imgui ? nullptr : prev);
    g_imgui = nullptr;
    g_gl_ready = false;
    g_last_counter = 0;
}

// ---- PsxMenuProvider ------------------------------------------------------

int p_attach(void *, void *, void *, const PsxMenuHost *host) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!ts2_cheat_menu_activated()) return 0;  // feature disabled: stay out
    if (!host || host->abi_version != PSX_MENU_ABI_VERSION) return 0;
#if defined(PSX_SDL3)
    if (g_ui) recomp_runtime_ui_destroy(g_ui);
    g_host = host;
    g_paused = false;
    cm_chord_reset(&g_chord);
    // Activation (and so the engine's launcher option) ran before attach:
    // the first sync shows the real initial state.
    cheat_menu_view_init(&g_view);
    (void)cheat_menu_view_sync(&g_view);
    RecompRuntimeUiConfig cfg{};
    cfg.title = "Cheat Menu";
    cfg.subtitle = "Developer build";
    cfg.items = cheat_menu_view_items(&g_view, &cfg.item_count);
    cheat_menu_view_callbacks(&g_view, &cfg.callbacks);
    cfg.callbacks.run_action = cb_action;
    cfg.callbacks.visibility_changed = cb_visibility;
    cfg.theme = "psx";
    cfg.accept_label = "Enter / Cross";
    cfg.back_label = "Esc / Circle";
    g_ui = recomp_runtime_ui_create(&cfg);
    if (!g_ui) {
        g_host = nullptr;
        return 0;
    }
    return 1;
#else
    return 0;  // input is wired for SDL3 only; release builds use SDL3
#endif
}

int p_event(void *, const void *ev) {
#if defined(PSX_SDL3)
    if (!ev) return 0;
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_ui) return 0;
    const int consumed = on_event(*static_cast<const SDL_Event *>(ev));
    sync_cheats_locked();
    return consumed;
#else
    (void)ev;
    return 0;
#endif
}

// Called by the runtime once per frame, after it drained the event queue
// (host_pause_service), which makes it the place to drop chord bits whose
// release an F7 or Rewind screen swallowed, and to refresh the cheat
// snapshot (VBlank state changes, a request made outside p_event).
int p_is_open(void *) {
    std::lock_guard<std::mutex> lk(g_mu);
#if defined(PSX_SDL3)
    if (g_chord.has_pad) cm_chord_resync(&g_chord, chord_live((SDL_JoystickID)g_chord.pad));
#endif
    if (g_ui) sync_cheats_locked();
    return g_ui && recomp_runtime_ui_is_open(g_ui);
}

void p_set_open(void *, int open) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_ui) return;
    if (open) recomp_runtime_ui_open(g_ui);
    else recomp_runtime_ui_close(g_ui);
}

void p_draw(void *, int w, int h) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (!g_ui || !recomp_runtime_ui_is_open(g_ui) || w <= 0 || h <= 0) return;
    ImGuiContext *prev = ImGui::GetCurrentContext();
    if (!g_imgui) {
        g_imgui = ImGui::CreateContext();
        ImGui::SetCurrentContext(g_imgui);
        ImGui::GetIO().IniFilename = nullptr;
    }
    ImGui::SetCurrentContext(g_imgui);
    if (!g_gl_ready) g_gl_ready = ImGui_ImplOpenGL3_Init(nullptr);
    if (!g_gl_ready) {
        ImGui::SetCurrentContext(prev);
        return;
    }
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
    io.DisplayFramebufferScale = ImVec2(1, 1);
    // Keyboard/controller driven: no pointer, so hover never moves the
    // selection under the player.
    io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
#if defined(PSX_SDL3)
    const uint64_t now = SDL_GetPerformanceCounter();
    const uint64_t frequency = SDL_GetPerformanceFrequency();
    io.DeltaTime = g_last_counter && now > g_last_counter && frequency
        ? (float)((double)(now - g_last_counter) / (double)frequency) : 1.0f / 60.0f;
    g_last_counter = now;
#else
    io.DeltaTime = 1.0f / 60.0f;
#endif
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    recomp_runtime_ui_render_imgui(g_ui);
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ImGui::SetCurrentContext(prev);
}

// Before the GL context goes away, with it current.
void p_detach(void *) {
    std::lock_guard<std::mutex> lk(g_mu);
    release_gl_locked();
    if (g_ui) {
        recomp_runtime_ui_close(g_ui);  // drops a held pause
        recomp_runtime_ui_destroy(g_ui);
    }
    g_ui = nullptr;
    g_host = nullptr;
    g_paused = false;
    cm_chord_reset(&g_chord);
}

const PsxMenuProvider kProvider = {
    PSX_MENU_ABI_VERSION, "toystory2-cheat-menu", nullptr,
    p_attach, p_event, p_is_open, p_set_open, p_draw, p_detach,
};

}  // namespace

extern "C" const PsxMenuProvider *ts2_cheat_menu_provider(void) { return &kProvider; }
