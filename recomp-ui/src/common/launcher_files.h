// launcher_files.h — platform-agnostic ROM picker (shared core).
//
// Native pickers:
//   Windows / macOS — tinyfiledialogs (GetOpenFileName / osascript)
//   Linux — posix_spawn of zenity or kdialog (avoids tinyfd's popen/vfork,
//           which SIGSEGVs from the multithreaded SDL UI thread)
// Falls back to tinyfd on Linux only if neither zenity nor kdialog is installed.
//
// Deliberately SDL-version agnostic: it does not depend on SDL3's
// SDL_ShowOpenFileDialog, so it works identically on SDL2 and SDL3.

#ifndef LAUNCHER_NG_FILES_H
#define LAUNCHER_NG_FILES_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Open the OS "choose a ROM" dialog (BLOCKING — returns when the user picks or
// cancels). Returns true and fills `out_path` on success.
//
// CONSOLE-NEUTRAL by design: this is the last-resort fallback for a console
// whose SystemProfile omits `rom_filter`, so it offers "All files" rather than
// any one system's extensions. It used to hard-code the SNES set
// ("Select SNES ROM", *.sfc/*.smc/*.fig/*.swc), which meant a PSX player who
// hit this path was asked for a Super Nintendo cartridge — see the note on the
// call site in launcher_imgui.cpp. Per-console extensions belong in that
// console's profile (SystemProfile.rom_filter), never here.
bool launcher_pick_rom(char* out_path, size_t out_cap);

// Open the OS "choose a folder" dialog (for the MSU-1 music folder). Returns
// true and fills `out_path` on success.
bool launcher_pick_folder(const char* title, char* out_path, size_t out_cap);

// Open the OS "choose a file" dialog for an arbitrary single file (e.g. a PSX
// BIOS image). `patterns`/`num_patterns` may be NULL/0 for "all files"; `desc`
// is the filter's display description (may be NULL). Returns true and fills
// `out_path` on success.
bool launcher_pick_file(const char* title, const char* const* patterns, int num_patterns,
                        const char* desc, char* out_path, size_t out_cap);

// Open the OS "save file" dialog — for choosing a DESTINATION path that need
// not already exist (e.g. picking where to write a freshly formatted PS1
// memory-card image). `patterns`/`num_patterns` may be NULL/0 for "all
// files"; `desc` is the filter's display description (may be NULL). Returns
// true and fills `out_path` on success.
bool launcher_pick_save_file(const char* title, const char* const* patterns, int num_patterns,
                             const char* desc, char* out_path, size_t out_cap);

#ifdef __cplusplus
}
#endif

#endif // LAUNCHER_NG_FILES_H
