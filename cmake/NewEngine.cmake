# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Starts a new engine from the template (cmake/engine-template/):
#
#   cmake -DENGINE=<name> [-DTITLE="<Name shown>"] -P cmake/NewEngine.cmake
#
# run from the top of the repository. It makes:
#   src/engines/<name>/              CMakeLists.txt, <name>.cpp (MetaEngine,
#                                    detection table, Engine), tools/files.cpp
#   tests/engines/<name>/            unit/detection.cpp, engine.cpp, and the
#                                    engine test's record in data/
#   docs/engines/<name>/README.md
# and adds `<name>=enabled` to engines.cfg. It builds and its tests pass
# as it is; docs/ADDING_AN_ENGINE.md says what to fill in.

cmake_minimum_required(VERSION 3.24)

get_filename_component(root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(template "${CMAKE_CURRENT_LIST_DIR}/engine-template")

if(NOT DEFINED ENGINE OR ENGINE STREQUAL "")
    message(FATAL_ERROR "usage: cmake -DENGINE=<name> [-DTITLE=\"<Name shown>\"] -P cmake/NewEngine.cmake")
endif()
if(NOT ENGINE MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR "\"${ENGINE}\": an engine's name is lower case letters, digits and _, starting with a "
                        "letter (it's its folder, its id, and its name on reluva-tool's command line)")
endif()
foreach(dir src/engines/${ENGINE} tests/engines/${ENGINE} docs/engines/${ENGINE})
    if(EXISTS "${root}/${dir}")
        message(FATAL_ERROR "${dir} exists already")
    endif()
endforeach()
file(STRINGS "${root}/engines.cfg" lines)
foreach(line IN LISTS lines)
    if(line MATCHES "^${ENGINE}=")
        message(FATAL_ERROR "engines.cfg has a line for ${ENGINE} already")
    endif()
endforeach()

# The names in the code: my_game -> MyGame, myGameMetaEngine.
set(CLASS "")
string(REPLACE "_" ";" parts "${ENGINE}")
foreach(part IN LISTS parts)
    if(NOT part STREQUAL "")
        string(SUBSTRING "${part}" 0 1 first)
        string(SUBSTRING "${part}" 1 -1 rest)
        string(TOUPPER "${first}" first)
        string(APPEND CLASS "${first}${rest}")
    endif()
endforeach()
string(SUBSTRING "${CLASS}" 0 1 first)
string(SUBSTRING "${CLASS}" 1 -1 rest)
string(TOLOWER "${first}" first)
set(ACCESSOR "${first}${rest}MetaEngine")
if(NOT DEFINED TITLE OR TITLE STREQUAL "")
    set(TITLE "${CLASS}")
endif()
string(TIMESTAMP YEAR "%Y")

set(made "")
function(make from to)
    configure_file("${template}/${from}" "${root}/${to}" @ONLY NEWLINE_STYLE UNIX)
    set(made "${made}  ${to}\n" PARENT_SCOPE)
endfunction()
make(CMakeLists.txt.in src/engines/${ENGINE}/CMakeLists.txt)
make(engine.cpp.in src/engines/${ENGINE}/${ENGINE}.cpp)
make(tools.cpp.in src/engines/${ENGINE}/tools/files.cpp)
make(unit_detection.cpp.in tests/engines/${ENGINE}/unit/detection.cpp)
make(engine_test.cpp.in tests/engines/${ENGINE}/engine.cpp)
make(engine-record.txt.in tests/engines/${ENGINE}/data/engine-record.txt)
make(README.md.in docs/engines/${ENGINE}/README.md)
file(APPEND "${root}/engines.cfg" "\n${ENGINE}=enabled\n")

message("Made the ${ENGINE} engine (${TITLE}):\n${made}  and ${ENGINE}=enabled in engines.cfg.\n"
        "Build, and `reluva-tests ${ENGINE}` passes. Then see docs/ADDING_AN_ENGINE.md.")
