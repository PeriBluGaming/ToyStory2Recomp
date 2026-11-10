# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# SPDX-FileCopyrightText: 2026 PeriBluGaming
#
# psxcheat unit tests (fake memory only; never linked into the game).
#
# From the game build (off by default):
#   cmake -S . -B <dir> -DTS2_BUILD_CHEAT_CORE_TESTS=ON
#   cmake --build <dir> --target psxcheat_tests && ctest --test-dir <dir> -R psxcheat
# Standalone (no psxrecomp configure needed):
#   cmake -S tests/cheat_core -B <dir> && cmake --build <dir> && ctest --test-dir <dir>
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(psxcheat_tests LANGUAGES C)
    enable_testing()
endif()

set(PSXCHEAT_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../mods/src/cheat_core")

add_library(psxcheat_core STATIC
    "${PSXCHEAT_CORE_DIR}/psxcheat_parse.c"
    "${PSXCHEAT_CORE_DIR}/psxcheat_exec.c"
)
target_include_directories(psxcheat_core PUBLIC "${PSXCHEAT_CORE_DIR}")
set_target_properties(psxcheat_core PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

add_executable(psxcheat_tests
    "${CMAKE_CURRENT_LIST_DIR}/psxcheat_tests.c"
    "${CMAKE_CURRENT_LIST_DIR}/fake_memory.c"
)
target_link_libraries(psxcheat_tests PRIVATE psxcheat_core)
set_target_properties(psxcheat_tests PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(psxcheat_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Werror)
    target_compile_options(psxcheat_tests PRIVATE -Wall -Wextra -Werror)
elseif(MSVC)
    target_compile_options(psxcheat_core PRIVATE /W4 /WX)
    target_compile_options(psxcheat_tests PRIVATE /W4 /WX)
endif()

add_test(NAME psxcheat_tests COMMAND psxcheat_tests)
