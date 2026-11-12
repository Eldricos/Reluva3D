cmake_minimum_required(VERSION 3.24)
project(reluva VERSION 0.1.0 LANGUAGES C CXX)

# Optimized unless asked otherwise: the x86 interpreter and the software
# rasterizer are many times slower in a debug build.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Visual Studio: the interpreter's stand-ins and handlers are thousands of
# lambdas and template instances, more sections than an object file holds
# by default in a debug build (error C1128); and the sources are UTF-8,
# whatever the system's code page.
if(MSVC)
    add_compile_options(/bigobj /utf-8)
    # Visual Studio's "x64-Release" configuration is CMake's RelWithDebInfo,
    # whose defaults are made for debugging: /Ob1 inlines only what's
    # marked inline, and /INCREMENTAL links every call through a jump stub.
    # The interpreter calls a handler for every instruction it runs, so a
    # profile of LEGO Alpha Team found these costing much of the game's
    # thread. Inline fully and link plainly, keeping the debug information.
    foreach(lang C CXX)
        string(REPLACE "/Ob1" "/Ob2" CMAKE_${lang}_FLAGS_RELWITHDEBINFO "${CMAKE_${lang}_FLAGS_RELWITHDEBINFO}")
    endforeach()
    foreach(kind EXE SHARED MODULE)
        string(REGEX REPLACE "/INCREMENTAL(:[A-Z]+)?" "/INCREMENTAL:NO" CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO
               "${CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO}")
        string(APPEND CMAKE_${kind}_LINKER_FLAGS_RELWITHDEBINFO " /OPT:REF /OPT:ICF")
    endforeach()
endif()

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

# clang-format leaves the build folder alone: the code downloaded into it
# (SDL3, Dear ImGui in _deps/) isn't ours, and an IDE often makes its build
# folder inside the checkout (out/, cmake-build-*/). A .clang-format there
# turns formatting off, but SDL brings a .clang-format of its own, which
# is nearer to its files; so a .clang-format-ignore too, which clang-format
# checks first. Its patterns match one folder level each (clang-format 18),
# hence one per depth. Not in an in-source build, where this would switch
# it off for Reluva's own code.
set(ignore_all "# Written by Reluva's CMakeLists.txt: no formatting in the build folder.\n")
set(level "*")
foreach(depth RANGE 1 16)
    string(APPEND ignore_all "${level}\n")
    string(APPEND level "/*")
endforeach()
foreach(dir IN ITEMS "${CMAKE_BINARY_DIR}" "${FETCHCONTENT_BASE_DIR}")
    if(dir AND NOT dir STREQUAL CMAKE_SOURCE_DIR)
        file(WRITE "${dir}/.clang-format"
             "# Written by Reluva's CMakeLists.txt: no formatting in the build folder.\n"
             "DisableFormat: true\nSortIncludes: Never\n")
        file(WRITE "${dir}/.clang-format-ignore" "${ignore_all}")
    endif()
endforeach()

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
    src/common/cue.cpp
    src/common/fs.cpp
    src/common/md5.cpp
    src/common/md4.cpp
    src/common/archive.cpp
    src/common/zip.cpp
    src/common/video/bink.cpp
    src/common/video/bink_tables.cpp
    src/common/video/smacker.cpp
    src/common/video/cinepak.cpp
    src/common/video/avi.cpp
    src/common/video/indeo5.cpp
    src/common/video/indeo5_tables.cpp
    src/common/video/movie.cpp
    src/common/image.cpp
    src/common/gfx3d.cpp
    src/common/net.cpp
    src/common/pe.cpp
    src/common/x86/cpu.cpp
    src/common/x86/cpu_cache.cpp
    src/common/x86/jit.cpp
    src/common/x86/fpu.cpp
    src/common/x86/win32.cpp
    src/common/x86/win32_files.cpp
    src/common/x86/win32_system.cpp
    src/common/x86/win32_user.cpp
    src/common/x86/win32_registry.cpp
    src/common/x86/msvcrt.cpp
    src/common/x86/msvcp60.cpp
    src/common/x86/com.cpp
    src/common/x86/directx/ddraw.cpp
    src/common/x86/directx/surface.cpp
    src/common/x86/directx/interfaces.cpp
    src/common/x86/directx/d3d7.cpp
    src/common/x86/directx/raster.cpp
    src/common/x86/directx/dinput.cpp
    src/common/x86/a3d.cpp
    src/common/x86/format.cpp
    src/common/x86/disasm.cpp
    src/common/detection.cpp
    src/common/inflate.cpp
    src/common/wise.cpp
    src/common/audio/decode.cpp
    src/common/audio/mixer.cpp
    src/common/audio/stream.cpp
    src/common/audio/mp3.cpp
    src/common/audio/wav.cpp
    src/common/font.cpp
    src/common/text.cpp
    src/common/stream.cpp
    src/common/log.cpp
    src/common/palette.cpp
    src/common/draw.cpp
    src/common/settings.cpp
    src/common/dcl.cpp
    src/common/cursor.cpp
    src/common/bmp.cpp
    src/common/truetype.cpp)
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

# reluva-tool: the shared commands (src/tools/) and each enabled engine's
# (reluva_add_engine_tools in its CMakeLists.txt).
reluva_finish_tool(
    src/tools/main.cpp
    src/tools/tool.cpp
    src/tools/commands.cpp
    src/tools/media.cpp
    src/tools/games.cpp
    src/tools/x86.cpp)

# --- tests ----------------------------------------------------------------------
# reluva-tests: the shared code's tests (tests/common/) and each enabled
# engine's (reluva_add_engine_tests in its CMakeLists.txt). See
# docs/DEVELOPMENT.md (Tests).
reluva_add_tests(common
    SOURCES ${PROJECT_SOURCE_DIR}/tests/common/unit.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/jit.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/windows.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/indeo.cpp
    TOPICS unit.hashes unit.files unit.archives unit.text unit.audio unit.image unit.video unit.net unit.x86
        unit.jit unit.windows unit.indeo)
reluva_finish_tests()
