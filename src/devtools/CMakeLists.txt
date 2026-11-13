# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The developer tools.

# reluva-render-check: the 3D renderer on whatever GPU backend SDL picks
# (Metal on macOS, Vulkan elsewhere) against reference frames: see
# render_check.cpp and .github/workflows/ci.yml.
add_executable(reluva-render-check render_check.cpp)
target_link_libraries(reluva-render-check PRIVATE reluva_shared reluva_backend_sdl3 reluva_backend_sockets
                                                  reluva_warnings)

# What reluva-viewer's Tests window runs, apart from the window (and so
# tested in reluva-tests, tests/devtools/).
add_library(reluva_test_runs STATIC test_runs.cpp)
target_include_directories(reluva_test_runs PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(reluva_test_runs PRIVATE reluva_warnings)

# reluva-viewer: the engines' data viewers (reluva_add_engine_viewers),
# from the launcher's list of games or the command line: viewer.cpp. reluva
# itself, which players get, is built without them.
# Its Tests window (tests_window.cpp) runs this build's tests through ctest.
add_executable(reluva-viewer viewer.cpp tests_window.cpp ctest_runner.cpp)
target_link_libraries(reluva-viewer PRIVATE reluva_gui reluva_viewers reluva_test_runs reluva_backend_sdl3
                                            reluva_backend_sockets imgui SDL3::SDL3 reluva_warnings)
target_compile_definitions(reluva-viewer PRIVATE RELUVA_BUILD_DIR="${PROJECT_BINARY_DIR}"
                                                 RELUVA_CTEST="${CMAKE_CTEST_COMMAND}"
                                                 RELUVA_BUILD_CONFIG="$<$<BOOL:${CMAKE_CONFIGURATION_TYPES}>:$<CONFIG>>")

# reluva-tool: the shared commands (here) and each enabled engine's
# (reluva_add_engine_tools in its CMakeLists.txt).
reluva_finish_tool(
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/media.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/games.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/x86.cpp)
