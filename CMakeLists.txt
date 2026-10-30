cmake_minimum_required(VERSION 3.24)
project(reluva VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Floating point as written, with no multiply and add fused into one step:
# Clang does that by default on ARM (and GCC wherever FMA is available),
# which rounds differently. The game code expects the original's x86
# results (collision, movement, what a networked game agrees on), the same
# on every machine. Visual Studio doesn't fuse unless asked.
if(NOT MSVC)
    add_compile_options(-ffp-contract=off)
endif()

option(RELUVA_USE_SYSTEM_SDL "Use an installed SDL3 instead of building it" OFF)
option(RELUVA_BUILD_TESTS "Build unit tests" ON)
if(RELUVA_BUILD_TESTS)
    enable_testing()
endif()
# Which engines are built: engines.cfg (see cmake/ReluvaEngines.cmake).
include(cmake/ReluvaEngines.cmake)

include(FetchContent)

# --- SDL3 -------------------------------------------------------------------
if(RELUVA_USE_SYSTEM_SDL)
    find_package(SDL3 REQUIRED CONFIG)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.4.14
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- Dear ImGui (no CMake support upstream, so build it here) -----------------
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

# --- Warnings -----------------------------------------------------------------
add_library(reluva_warnings INTERFACE)
if(MSVC)
    target_compile_options(reluva_warnings INTERFACE /W4 /permissive-)
else()
    target_compile_options(reluva_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

# --- common: platform-independent core, no SDL allowed here -------------------
add_library(reluva_common STATIC
    src/common/config.cpp
    src/common/fs.cpp
    src/common/md5.cpp
    src/common/md4.cpp
    src/common/archive.cpp
    src/common/zip.cpp
    src/common/video/bink.cpp
    src/common/video/bink_tables.cpp
    src/common/image.cpp
    src/common/gfx3d.cpp
    src/common/net.cpp
    src/common/pe.cpp
    src/common/x86/cpu.cpp
    src/common/x86/fpu.cpp
    src/common/x86/win32.cpp
    src/common/x86/format.cpp
    src/common/detection.cpp
    src/common/inflate.cpp
    src/common/wise.cpp
    src/common/audio/decode.cpp
    src/common/audio/mixer.cpp
    src/common/audio/mp3.cpp
    src/common/audio/wav.cpp
    src/common/font.cpp
    src/common/text.cpp)
target_include_directories(reluva_common PUBLIC src)
# Third-party headers (third_party/, see each README) as system includes, so
# our warnings don't apply to them.
target_include_directories(reluva_common SYSTEM PRIVATE third_party/minimp3 third_party/stb)
target_link_libraries(reluva_common PRIVATE reluva_warnings)

# --- engines: talk only to common/ --------------------------------------------
# Each engine's own CMakeLists.txt (src/engines/<engine>/) declares it; the
# ones engines.cfg enables are built, with the registry over them
# (reluva_engines).
reluva_configure_engines()

# --- executable: SDL3 backend + ImGui launcher --------------------------------
add_executable(reluva
    src/main.cpp
    src/backends/sdl3/sdl_system.cpp
    src/backends/sdl3/sdl_gpu.cpp
    src/backends/sockets/socket_net.cpp
    src/launcher/launcher.cpp)
target_link_libraries(reluva PRIVATE reluva_engines imgui SDL3::SDL3 reluva_warnings)
if(WIN32)
    target_link_libraries(reluva PRIVATE ws2_32 iphlpapi)
endif()

# --- developer tools -----------------------------------------------------------
# A check of the 3D renderer on whatever GPU backend SDL picks (Metal on
# macOS, Vulkan elsewhere) against reference frames: see
# src/tools/render_check.cpp and .github/workflows/ci.yml.
add_executable(reluva-render-check
    src/tools/render_check.cpp
    src/backends/sdl3/sdl_system.cpp
    src/backends/sdl3/sdl_gpu.cpp
    src/backends/sockets/socket_net.cpp)
target_link_libraries(reluva-render-check PRIVATE reluva_common SDL3::SDL3 reluva_warnings)
if(WIN32)
    target_link_libraries(reluva-render-check PRIVATE ws2_32 iphlpapi)
endif()

add_executable(reluva-tool src/tools/reluva_tool.cpp src/backends/sockets/socket_net.cpp)
target_link_libraries(reluva-tool PRIVATE reluva_engines reluva_warnings)
if(WIN32)
    target_link_libraries(reluva-tool PRIVATE ws2_32 iphlpapi)
endif()

# --- tests ----------------------------------------------------------------------
# reluva-tests: the shared code's tests (tests/common/) and each enabled
# engine's (reluva_add_engine_tests in its CMakeLists.txt). See
# docs/DEVELOPMENT.md (Tests).
reluva_add_tests(common
    SOURCES ${PROJECT_SOURCE_DIR}/tests/common/unit.cpp
    TOPICS unit.hashes unit.files unit.archives unit.text unit.audio unit.image unit.net unit.x86)
reluva_finish_tests()
