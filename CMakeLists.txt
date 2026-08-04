cmake_minimum_required(VERSION 3.20)
project(SLUS_00893Recomp C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp")
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

include(${CMAKE_SOURCE_DIR}/recomp-ui/recomp_ui.cmake)
recomp_target_launcher_ui(psx-runtime
    CONSOLE psx
    BOXART ${CMAKE_SOURCE_DIR}/art/boxart.tga
    PAD    ${CMAKE_SOURCE_DIR}/art/pad.tga
    BRAND  ${CMAKE_SOURCE_DIR}/art/brand.tga)