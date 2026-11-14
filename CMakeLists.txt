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
# The games' debug keys and the launcher's "Developer:" settings, for
# working on the engines: release builds for players turn them off
# (src/common/developer.h).
option(RELUVA_DEVELOPER_KEYS "Build in the games' debug keys and developer settings" ON)
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

# --- shared code: a library per folder (each folder's CMakeLists.txt) ------
# common/ is the core the others build on; none of them may use SDL.
add_subdirectory(src/common)
add_subdirectory(src/audio)
add_subdirectory(src/graphics)
add_subdirectory(src/image)
add_subdirectory(src/video)
add_subdirectory(src/emulation)

# Everything an engine may use. Engines link this and nothing else from
# src/: not the backends, the launcher or the tools.
add_library(reluva_shared INTERFACE)
target_link_libraries(reluva_shared INTERFACE reluva_common reluva_audio reluva_graphics reluva_image reluva_video
                                              reluva_emulation)
target_compile_definitions(reluva_shared INTERFACE RELUVA_DEVELOPER_KEYS=$<BOOL:${RELUVA_DEVELOPER_KEYS}>)

# --- engines: talk only to the shared code (reluva_shared) ---------------------
# Each engine's own CMakeLists.txt (src/engines/<engine>/) declares it; the
# ones engines.cfg enables are built, with the registry over them
# (reluva_engines).
reluva_configure_engines()

# --- backends, launcher, programs ---------------------------------------------
# Every program at the top of the build folder (build/reluva,
# build/reluva-tool; build/Release/... with Visual Studio), wherever its
# CMakeLists.txt is: the docs, CI and people's habits run them from there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
add_subdirectory(src/backends)
add_subdirectory(src/gui)
# reluva: the program (src/base/)
add_subdirectory(src/base)
# reluva-viewer, reluva-render-check and reluva-tool (src/devtools/)
add_subdirectory(src/devtools)

# --- tests ----------------------------------------------------------------------
# reluva-tests: the shared code's tests (tests/common/) and each enabled
# engine's (reluva_add_engine_tests in its CMakeLists.txt). See
# docs/DEVELOPMENT.md (Tests).
reluva_add_tests(common
    SOURCES ${PROJECT_SOURCE_DIR}/tests/common/unit.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/jit.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/windows.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/indeo.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/x86_more.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/mmx.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/drives.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/mixer.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/ea_audio.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/prologic.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/glide.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/glide_more.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/glide_boards.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/glide_gpu.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/sockets.cpp
        ${PROJECT_SOURCE_DIR}/tests/common/sdl_controller.cpp
    TOPICS unit.hashes unit.files unit.archives unit.text unit.audio unit.image unit.video unit.net unit.x86
        unit.jit unit.windows unit.indeo unit.x86_more unit.mmx unit.drives unit.mixer unit.ea_audio unit.prologic
        unit.glide unit.glide_more unit.glide_boards unit.glide_gpu unit.sockets unit.sdl_controller)
reluva_finish_tests()
