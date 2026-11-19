# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The developer tools.

# reluva-render-check: the 3D renderer on whatever GPU backend SDL picks
# (Metal on macOS, Vulkan elsewhere) against reference frames: see
# render_check.cpp and .github/workflows/ci.yml.
add_executable(reluva-render-check render_check.cpp)
target_link_libraries(reluva-render-check PRIVATE reluva_shared reluva_backend_sdl3 reluva_backend_sockets
                                                  reluva_warnings)

# reluva-tool: the shared commands (here) and each enabled engine's
# (reluva_add_engine_tools in its CMakeLists.txt).
reluva_finish_tool(
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/commands.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/media.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/games.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/x86.cpp)
