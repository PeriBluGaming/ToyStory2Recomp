# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# SPDX-FileCopyrightText: 2026 PeriBluGaming
#
# psxcheat <-> psxrecomp adapter tests and Cheat Menu engine lifecycle tests.
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

if(NOT TARGET psxcheat_core)
    add_subdirectory("${TS2_REPO_ROOT}/tests/cheat_core"
                     "${CMAKE_CURRENT_BINARY_DIR}/cheat_core")
endif()

# Adapter + TS2 policy, test-only library.
add_library(psxcheat_psxrecomp_testlib STATIC
    "${PSXCHEAT_RUNTIME_DIR}/psxcheat_psxrecomp.c"
    "${TS2_CHEAT_MENU_DIR}/ts2_cheat_memory.c"
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

add_executable(ts2_ranges_check "${CMAKE_CURRENT_LIST_DIR}/ts2_ranges_check.c")
target_link_libraries(ts2_ranges_check PRIVATE psxcheat_psxrecomp_testlib)
target_compile_definitions(ts2_ranges_check PRIVATE
    TS2_RANGES_FILE="${TS2_REPO_ROOT}/generated/SLUS_008.93_full.ranges")

foreach(_t psxcheat_psxrecomp_testlib psxcheat_adapter_tests ts2_cheat_engine_tests
           ts2_cheat_menu_state_tests ts2_ranges_check)
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
add_test(NAME ts2_ranges_check COMMAND ts2_ranges_check)
