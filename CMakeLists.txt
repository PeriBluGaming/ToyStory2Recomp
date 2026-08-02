cmake_minimum_required(VERSION 3.20)
project(SLUS_00893Recomp C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp")
set(PSX_RECOMP_UI ON CACHE BOOL "" FORCE)
set(PSX_LAUNCHER OFF CACHE BOOL "" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")
file(GLOB GAME_FULL CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/generated/*_full.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/generated/*_full_*.c")
file(GLOB GAME_DISPATCH "${CMAKE_CURRENT_SOURCE_DIR}/generated/*_dispatch.c")
psxrecomp_add_runtime_target(psx-runtime
  GAME_GENERATED_FULL_C "${GAME_FULL}"
  GAME_GENERATED_DISPATCH_C "${GAME_DISPATCH}"
  BIOS_GENERATED_FULL_C "${CMAKE_CURRENT_SOURCE_DIR}/bios-generated/SCPH1001_full.c"
  BIOS_GENERATED_DISPATCH_C "${CMAKE_CURRENT_SOURCE_DIR}/bios-generated/SCPH1001_dispatch.c"
  WINDOW_TITLE "SLUS-00893 Recompiled"
  DEFAULT_GAME_CONFIG_PATH "game.toml"
)

# ---- recomp-ui (shared Dear ImGui launcher) -------------------------------
# PSX_RECOMP_UI controls whether the console-agnostic recomp-ui launcher is
# built into the psx-runtime target. The submodule ships its own vendored
# Dear ImGui + tinyfiledialogs and stages its assets next to the exe; it only
# needs the host's SDL2 (already linked by runtime.cmake) + OpenGL.
# RECOMP_UI_ENABLE_MODS defaults ON for a fresh game build cache (see
# recomp-ui README "PSXRecomp integration"); the host supplies the provider at
# runtime via RecompLauncherCGameInfo.mods.
if(PSX_RECOMP_UI)
    if(NOT DEFINED RECOMP_UI_ENABLE_MODS)
        set(RECOMP_UI_ENABLE_MODS ON CACHE BOOL
            "Enable this game's Mods launcher view" FORCE)
    endif()
    set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH
        "Root directory of the recomp-ui launcher repo" FORCE)
    if(NOT EXISTS "${RECOMP_UI_ROOT}/recomp_ui.cmake")
        message(FATAL_ERROR
            "PSX_RECOMP_UI=ON requires the recomp-ui submodule at ${RECOMP_UI_ROOT}. "
            "Initialize it with `git submodule update --init --recursive` and re-run CMake.")
    endif()
    include("${RECOMP_UI_ROOT}/recomp_ui.cmake")
    recomp_target_launcher_ui(psx-runtime CONSOLE psx)
endif()
