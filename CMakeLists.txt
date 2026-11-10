cmake_minimum_required(VERSION 3.20)
set(PSXRECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/psxrecomp" CACHE PATH
    "Path to psxrecomp")
if(NOT EXISTS "${PSXRECOMP_ROOT}/runtime/runtime.cmake")
    message(FATAL_ERROR
        "PSXRecomp runtime is missing: ${PSXRECOMP_ROOT}/runtime/runtime.cmake\n"
        "This project expects the psxrecomp framework checkout at ${PSXRECOMP_ROOT}.\n"
        "Run: git submodule update --init --recursive\n"
        "If psxrecomp/ is not a submodule, set -DPSXRECOMP_ROOT=<path-to-psxrecomp>.")
endif()

project(ToyStory2Recomp LANGUAGES C CXX)

# --- Opt-in product surfaces (OFF by default in the framework) -------------
# Scaffold prompts / flags fill these. Without recomp-ui, PSX_RECOMP_UI is OFF.
# The wizard (WIZARD_CMAKE_BLOCK / ENABLE_SETUP_WIZARD) is the first-run disc
# picker; releases are compiled from the committed generated/ C, so it never
# needs to generate or rebuild anything on a player's machine.
# recomp-ui submodule present (PSX_RECOMP_UI defaults ON).
# Full netplay UI — enable after adding recomp-ui + testing:
# if(EXISTS "${PSXRECOMP_ROOT}/lib/recomp-net/CMakeLists.txt")
#     set(PSX_NETPLAY ON CACHE BOOL "…" FORCE)
# endif()
set(PSX_SETUP_WIZARD ON CACHE BOOL
    "Advertise first-run setup wizard + Generate & rebuild in recomp-ui" FORCE)
include("${PSXRECOMP_ROOT}/runtime/runtime.cmake")

# This project's own mod catalog. The FRAMEWORK stages it -- together with its
# builtin psx.* packages -- into <exe-dir>/mods/bundled, and refuses to
# configure if a catalog exists here but is not declared. Never copy it by
# hand: five titles did, and framework 4cc04be3's rename of the staged
# directory broke all five silently (bead beads-eio.3.101).
set(PSX_PRELOADED_MODS "${CMAKE_CURRENT_SOURCE_DIR}/mods/preloaded")
if(NOT IS_DIRECTORY "${PSX_PRELOADED_MODS}")
    set(PSX_PRELOADED_MODS "NONE")
endif()

psxrecomp_add_game_runtime(psx-runtime
    PRELOADED_MODS_DIR "${PSX_PRELOADED_MODS}"
    # ENABLE_NETPLAY_IF_PRESENT
    # NETPLAY_LOBBY_URL "ws://netplay.retcomm.net:8765"
    ENABLE_SETUP_WIZARD
    WINDOW_TITLE "ToyStory2 Recompiled"
    DEFAULT_GAME_CONFIG_PATH "game.toml"
    MAX_PLAYERS 1
    # LAUNCHER_BOXART "${CMAKE_CURRENT_SOURCE_DIR}/launcher_assets/img/boxart.tga"
    APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/psxrecomp.ico"
    # Committed game C (psxrecomp_cli.py generate; git add generated). Release
    # CI configures with -DPSXRECOMP_REQUIRE_GAME_C=ON so a checkout without
    # it fails instead of building a setup host.
    GEN_MARKER "generated/SLUS_008.93_dispatch.c"
    GEN_FULL_GLOB "generated/SLUS_008.93_full_*.c"
    CODEGEN_SETUP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/codegen_setup.c"
)

# Toy Story 2 mods: native widescreen (16:9), Skip Startup Intro, BIOS
# free() compatibility for OpenBIOS, the right-stick Modern Camera, the
# pause-menu Progress Tracker, and the Cheat Menu (developer
# channel; its in-game menu provider builds on recomp-ui's runtime menu and
# Dear ImGui, already compiled into this target). Its Max Coins row reaches
# the engine through the cheat_menu_state mailbox/snapshot bridge. The engine
# runs one off-by-default test cheat through the psxcheat Action Replay core and
# its psxrecomp memory adapter; all of it only runs while the developer
# feature is in the mod plan (release builds filter that feature out).
target_sources(psx-runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/widescreen.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/skip_startup_intro.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/bios_free_compat.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/modern_camera.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/progress_tracker.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_menu/cheat_menu_plugin.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_menu/cheat_menu_provider.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_menu/cheat_menu_state.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_menu/cheat_menu_view.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_menu/ts2_cheat_engine.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_menu/ts2_cheat_memory.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_runtime/psxcheat_psxrecomp.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_core/psxcheat_parse.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_core/psxcheat_exec.c"
)
target_include_directories(psx-runtime PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_core"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_runtime"
    "${CMAKE_CURRENT_SOURCE_DIR}/mods/src/cheat_menu"
)

# psxcheat unit tests: the Action Replay core (fake memory), the psxrecomp
# memory adapter and the Cheat Menu engine/plugin lifecycle (stubbed runtime
# services). Off by default: normal and release builds neither build nor run
# them.
option(TS2_BUILD_CHEAT_CORE_TESTS "Build the psxcheat core and adapter unit tests (CTest)" OFF)
if(TS2_BUILD_CHEAT_CORE_TESTS)
    enable_testing()
    add_subdirectory(tests/cheat_core)
    add_subdirectory(tests/cheat_runtime)
endif()

target_include_directories(psx-runtime PRIVATE
    "${PSXRECOMP_ROOT}/runtime/include"
)

# Stage runtime configs next to the exe for direct build/ launches. Only copy
# files that exist — never fail POST_BUILD when game_options.toml is absent.
foreach(_PSX_RUNTIME_CONFIG game.toml game_options.toml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}")
        add_custom_command(TARGET psx-runtime POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${_PSX_RUNTIME_CONFIG}"
                "$<TARGET_FILE_DIR:psx-runtime>/${_PSX_RUNTIME_CONFIG}"
            COMMENT "Staging ${_PSX_RUNTIME_CONFIG}"
            VERBATIM)
    endif()
endforeach()
