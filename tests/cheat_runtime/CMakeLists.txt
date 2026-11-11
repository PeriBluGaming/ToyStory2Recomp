# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# SPDX-FileCopyrightText: 2026 PeriBluGaming
#
# psxcheat <-> psxrecomp adapter tests, Cheat Menu engine lifecycle tests and
# the Cheat Menu rows driven through recomp-ui's runtime menu model.
# The psxrecomp services are stubs (psx_runtime_stub.c, plugin_stub.c)
# compiled against the real mod_plugins.h / psx_menu_host.h; the test
# executables are never part of the game and no guest memory is touched.
#
# From the game build (off by default):
#   cmake -S . -B <dir> -DTS2_BUILD_CHEAT_CORE_TESTS=ON
# Standalone:
#   cmake -S tests/cheat_runtime -B <dir> && cmake --build <dir> && ctest --test-dir <dir>
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(psxcheat_runtime_tests LANGUAGES C)
    enable_testing()
endif()

set(TS2_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(PSXCHEAT_RUNTIME_DIR "${TS2_REPO_ROOT}/mods/src/cheat_runtime")
set(TS2_CHEAT_MENU_DIR "${TS2_REPO_ROOT}/mods/src/cheat_menu")
set(PSXRECOMP_MOD_INCLUDE "${TS2_REPO_ROOT}/psxrecomp/runtime/include")
set(RECOMP_UI_SRC "${TS2_REPO_ROOT}/recomp-ui/src")

if(NOT TARGET psxcheat_core)
    add_subdirectory("${TS2_REPO_ROOT}/tests/cheat_core"
                     "${CMAKE_CURRENT_BINARY_DIR}/cheat_core")
endif()

# Adapter + TS2 policy + TS2 cheat table, test-only library.
add_library(psxcheat_psxrecomp_testlib STATIC
    "${PSXCHEAT_RUNTIME_DIR}/psxcheat_psxrecomp.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_memory.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_table.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_external.c"
)
target_include_directories(psxcheat_psxrecomp_testlib PUBLIC
    "${PSXCHEAT_RUNTIME_DIR}" "${TS2_CHEAT_MENU_DIR}")
# mod_plugins.h only for its declarations; SYSTEM keeps framework header
# warnings out of the strict build.
target_include_directories(psxcheat_psxrecomp_testlib SYSTEM PUBLIC "${PSXRECOMP_MOD_INCLUDE}")
target_link_libraries(psxcheat_psxrecomp_testlib PUBLIC psxcheat_core)

add_executable(psxcheat_adapter_tests
    "${CMAKE_CURRENT_LIST_DIR}/adapter_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
)
target_link_libraries(psxcheat_adapter_tests PRIVATE psxcheat_psxrecomp_testlib)

# Cheat Menu plugin + engine lifecycle. cheat_menu_plugin.c is compiled into
# the executable itself (not a static library) so its registration
# constructor is always linked and runs before main.
add_executable(ts2_cheat_engine_tests
    "${CMAKE_CURRENT_LIST_DIR}/engine_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_plugin.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_engine_tests PRIVATE psxcheat_psxrecomp_testlib)

# Engine state model + cheat_menu_state bridge (mailbox/snapshot).
add_executable(ts2_cheat_menu_state_tests
    "${CMAKE_CURRENT_LIST_DIR}/cheat_menu_state_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_state.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_menu_state_tests PRIVATE psxcheat_psxrecomp_testlib)

# Phase 2C: Cheat Menu rows driven through recomp-ui's real runtime menu
# model. recomp_runtime_ui.c is compiled from the submodule unchanged, as
# third-party code: SYSTEM includes and none of the strict warning flags.
add_library(ts2_cheat_test_runtime_ui STATIC "${RECOMP_UI_SRC}/common/recomp_runtime_ui.c")
target_include_directories(ts2_cheat_test_runtime_ui SYSTEM PUBLIC
    "${RECOMP_UI_SRC}" "${RECOMP_UI_SRC}/common")

add_executable(ts2_cheat_menu_view_tests
    "${CMAKE_CURRENT_LIST_DIR}/cheat_menu_view_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_view.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_state.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_menu_view_tests PRIVATE
    psxcheat_psxrecomp_testlib ts2_cheat_test_runtime_ui)

# Phase 2D: "remember-changes" persistence through a fake PsxMenuHost.mod_set
# (records calls only; no file is written). Links the real plugin so its
# activation reads the launcher options.
add_executable(ts2_cheat_menu_persist_tests
    "${CMAKE_CURRENT_LIST_DIR}/cheat_menu_persist_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_plugin.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_view.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_state.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_menu_persist_tests PRIVATE
    psxcheat_psxrecomp_testlib ts2_cheat_test_runtime_ui)

# Phase 2E-B: cheat table validation, independent engine slots, multi-line
# programs and isolation, with synthetic cheat definitions and policies.
add_executable(ts2_cheat_table_tests
    "${CMAKE_CURRENT_LIST_DIR}/cheat_table_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_table_tests PRIVATE psxcheat_psxrecomp_testlib)

# Phase 2E-C: several independent cheat rows (synthetic N = 3 table) through
# the real engine, bridge, view and recomp-ui model; fake menu host.
add_executable(ts2_cheat_menu_multi_tests
    "${CMAKE_CURRENT_LIST_DIR}/cheat_menu_multi_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_view.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_state.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_menu_multi_tests PRIVATE
    psxcheat_psxrecomp_testlib ts2_cheat_test_runtime_ui)

# Phase 2E-D2: the production table with both verified cheats (Max Coins,
# Infinite Lives) through the real plugin activation, engine, bridge, view
# and recomp-ui model; fake menu host.
add_executable(ts2_cheat_production_tests
    "${CMAKE_CURRENT_LIST_DIR}/production_cheats_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_plugin.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_view.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_state.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_production_tests PRIVATE
    psxcheat_psxrecomp_testlib ts2_cheat_test_runtime_ui)

# Phase 3B: external cheat validation and the combined cheat table, with
# synthetic in-memory entries (no file I/O), through the real plugin
# activation, engine, bridge, view and recomp-ui model; fake menu host.
add_executable(ts2_cheat_external_tests
    "${CMAKE_CURRENT_LIST_DIR}/external_cheats_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
    "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_plugin.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_view.c"
    "${TS2_CHEAT_MENU_DIR}/cheat_menu_state.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
)
target_link_libraries(ts2_cheat_external_tests PRIVATE
    psxcheat_psxrecomp_testlib ts2_cheat_test_runtime_ui)

# Phase 3B.3: verified health span and external health cheats; built twice:
# with the production plugin, and with the plugin compiled for the opt-in
# runtime test (TS2_CHEAT_RUNTIME_TEST_HEALTH), to check both paths.
foreach(_variant ts2_cheat_health_tests ts2_cheat_health_runtime_tests)
    add_executable(${_variant}
        "${CMAKE_CURRENT_LIST_DIR}/health_cheat_tests.c"
        "${CMAKE_CURRENT_LIST_DIR}/psx_runtime_stub.c"
        "${CMAKE_CURRENT_LIST_DIR}/plugin_stub.c"
        "${TS2_CHEAT_MENU_DIR}/cheat_menu_plugin.c"
        "${TS2_CHEAT_MENU_DIR}/cheat_menu_view.c"
        "${TS2_CHEAT_MENU_DIR}/cheat_menu_state.c"
        "${TS2_CHEAT_MENU_DIR}/ts2_cheat_engine.c"
    )
    target_link_libraries(${_variant} PRIVATE
        psxcheat_psxrecomp_testlib ts2_cheat_test_runtime_ui)
endforeach()
target_compile_definitions(ts2_cheat_health_runtime_tests PRIVATE TS2_CHEAT_RUNTIME_TEST_HEALTH=1)

add_executable(ts2_ranges_check "${CMAKE_CURRENT_LIST_DIR}/ts2_ranges_check.c")
target_link_libraries(ts2_ranges_check PRIVATE psxcheat_psxrecomp_testlib)
target_compile_definitions(ts2_ranges_check PRIVATE
    TS2_RANGES_FILE="${TS2_REPO_ROOT}/generated/SLUS_008.93_full.ranges")

foreach(_t psxcheat_psxrecomp_testlib psxcheat_adapter_tests ts2_cheat_engine_tests
           ts2_cheat_menu_state_tests ts2_cheat_menu_view_tests
           ts2_cheat_menu_persist_tests ts2_cheat_table_tests ts2_cheat_menu_multi_tests
           ts2_cheat_production_tests ts2_cheat_external_tests ts2_cheat_health_tests
           ts2_cheat_health_runtime_tests ts2_ranges_check)
    set_target_properties(${_t} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${_t} PRIVATE -Wall -Wextra -Werror)
    elseif(MSVC)
        target_compile_options(${_t} PRIVATE /W4 /WX)
    endif()
endforeach()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(psxcheat_psxrecomp_testlib PRIVATE -Wpedantic -Wconversion)
endif()

add_test(NAME psxcheat_adapter_tests COMMAND psxcheat_adapter_tests)
add_test(NAME ts2_cheat_engine_tests COMMAND ts2_cheat_engine_tests)
add_test(NAME ts2_cheat_menu_state_tests COMMAND ts2_cheat_menu_state_tests)
add_test(NAME ts2_cheat_menu_view_tests COMMAND ts2_cheat_menu_view_tests)
add_test(NAME ts2_cheat_menu_persist_tests COMMAND ts2_cheat_menu_persist_tests)
add_test(NAME ts2_cheat_table_tests COMMAND ts2_cheat_table_tests)
add_test(NAME ts2_cheat_menu_multi_tests COMMAND ts2_cheat_menu_multi_tests)
add_test(NAME ts2_cheat_production_tests COMMAND ts2_cheat_production_tests)
add_test(NAME ts2_cheat_external_tests COMMAND ts2_cheat_external_tests)
add_test(NAME ts2_cheat_health_tests COMMAND ts2_cheat_health_tests)
add_test(NAME ts2_cheat_health_runtime_tests COMMAND ts2_cheat_health_runtime_tests)
add_test(NAME ts2_ranges_check COMMAND ts2_ranges_check)
