# Which engines are built, and how an engine declares itself.
#
# engines.cfg (at the top of the repository) lists the engines, one per
# line: <engine>=enabled or <engine>=disabled. engines.local.cfg, if there
# is one (git ignores it), overrides any of its lines for one machine;
# -DRELUVA_ENGINES_FILE=<file> reads another file instead of engines.cfg.
# An engine is a folder src/engines/<engine>/ with a CMakeLists.txt that
# calls reluva_add_engine() (and reluva_add_engine_test() for its tests).
# The testbed is always built: the launcher and `reluva testbed` use it.

set(RELUVA_ENGINES_FILE "${PROJECT_SOURCE_DIR}/engines.cfg" CACHE FILEPATH
    "The list of engines to build (<engine>=enabled or disabled, one per line)")
set(RELUVA_ENGINES_LOCAL_FILE "${PROJECT_SOURCE_DIR}/engines.local.cfg")

# reluva_add_engine(<engine> ACCESSOR <function> SOURCES <files>...)
#   A library of the engine's code, reluva_engine_<engine>, using common/.
#   <function> returns its MetaEngine (`const MetaEngine& <function>()`).
#   RELUVA_ENGINE_<ENGINE> is defined for code that uses the engine.
function(reluva_add_engine name)
    cmake_parse_arguments(E "" "ACCESSOR" "SOURCES" ${ARGN})
    if(NOT E_ACCESSOR OR NOT E_SOURCES)
        message(FATAL_ERROR "reluva_add_engine(${name}) needs ACCESSOR and SOURCES")
    endif()
    set(target reluva_engine_${name})
    add_library(${target} STATIC ${E_SOURCES})
    target_link_libraries(${target} PUBLIC reluva_common PRIVATE reluva_warnings)
    string(TOUPPER "${name}" upper)
    target_compile_definitions(${target} PUBLIC RELUVA_ENGINE_${upper})
    set_property(GLOBAL APPEND PROPERTY RELUVA_ENGINE_TARGETS ${target})
    set_property(GLOBAL APPEND PROPERTY RELUVA_ENGINE_ACCESSORS ${E_ACCESSOR})
endfunction()

# reluva_add_engine_test(<engine> NAME <test> SOURCES <files>... [NETWORK])
#   A test program, reluva_<engine>_tests, run by ctest as <test>. NETWORK:
#   it uses sockets (Windows' socket libraries linked).
function(reluva_add_engine_test name)
    if(NOT RELUVA_BUILD_TESTS)
        return()
    endif()
    cmake_parse_arguments(T "NETWORK" "NAME" "SOURCES" ${ARGN})
    set(target reluva_${name}_tests)
    add_executable(${target} ${T_SOURCES})
    target_link_libraries(${target} PRIVATE reluva_engines reluva_warnings)
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    if(T_NETWORK AND WIN32)
        target_link_libraries(${target} PRIVATE ws2_32 iphlpapi)
    endif()
    add_test(NAME ${T_NAME} COMMAND ${target})
endfunction()

# Reads <file>'s lines into RELUVA_ENGINE_STATE_<engine> (enabled or
# disabled), stopping on anything else.
function(reluva_read_engine_file file)
    file(STRINGS "${file}" lines)
    set(seen "")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "#.*$" "" line "${line}")
        string(STRIP "${line}" line)
        if(line STREQUAL "")
            continue()
        endif()
        if(NOT line MATCHES "^([A-Za-z0-9_]+)[ \t]*=[ \t]*([A-Za-z]+)$")
            message(FATAL_ERROR "${file}: \"${line}\": expected <engine>=enabled or <engine>=disabled")
        endif()
        set(engine "${CMAKE_MATCH_1}")
        string(TOLOWER "${CMAKE_MATCH_2}" state)
        if(NOT state STREQUAL "enabled" AND NOT state STREQUAL "disabled")
            message(FATAL_ERROR "${file}: \"${line}\": the state is enabled or disabled")
        endif()
        if(NOT IS_DIRECTORY "${PROJECT_SOURCE_DIR}/src/engines/${engine}"
           OR NOT EXISTS "${PROJECT_SOURCE_DIR}/src/engines/${engine}/CMakeLists.txt")
            message(FATAL_ERROR "${file}: there's no engine \"${engine}\" (no src/engines/${engine}/CMakeLists.txt)")
        endif()
        set(RELUVA_ENGINE_STATE_${engine} "${state}" PARENT_SCOPE)
        list(APPEND seen ${engine})
    endforeach()
    set(RELUVA_ENGINES_LISTED ${seen} PARENT_SCOPE)
endfunction()

# Adds the engines the files enable (and the testbed), then makes
# reluva_engines: the registry, over every engine built.
function(reluva_configure_engines)
    # The switches this replaces, if a build folder still has them.
    foreach(old RELUVA_ENGINE_IDTECH3 RELUVA_ENGINE_NEWWORLDS)
        if(DEFINED CACHE{${old}})
            unset(${old} CACHE)
            message(STATUS "Ignoring ${old}: engines are chosen in engines.cfg now")
        endif()
    endforeach()

    set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${RELUVA_ENGINES_FILE}")
    if(NOT EXISTS "${RELUVA_ENGINES_FILE}")
        message(FATAL_ERROR "No engine list: ${RELUVA_ENGINES_FILE}")
    endif()
    reluva_read_engine_file("${RELUVA_ENGINES_FILE}")
    set(listed ${RELUVA_ENGINES_LISTED})
    if(EXISTS "${RELUVA_ENGINES_LOCAL_FILE}")
        set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${RELUVA_ENGINES_LOCAL_FILE}")
        reluva_read_engine_file("${RELUVA_ENGINES_LOCAL_FILE}")
        list(APPEND listed ${RELUVA_ENGINES_LISTED})
    endif()

    # The testbed first, then every engine folder, listed or not.
    add_subdirectory("${PROJECT_SOURCE_DIR}/src/engines/testbed" "${PROJECT_BINARY_DIR}/engines/testbed")
    file(GLOB entries LIST_DIRECTORIES true RELATIVE "${PROJECT_SOURCE_DIR}/src/engines"
        "${PROJECT_SOURCE_DIR}/src/engines/*")
    list(SORT entries)
    set(summary "")
    foreach(engine IN LISTS entries)
        if(NOT EXISTS "${PROJECT_SOURCE_DIR}/src/engines/${engine}/CMakeLists.txt")
            continue()
        endif()
        if(engine STREQUAL "testbed")
            if(DEFINED RELUVA_ENGINE_STATE_testbed AND RELUVA_ENGINE_STATE_testbed STREQUAL "disabled")
                message(WARNING "The testbed is always built (the launcher and `reluva testbed` use it)")
            endif()
            continue()
        endif()
        if(NOT engine IN_LIST listed)
            message(WARNING "The engine \"${engine}\" isn't in ${RELUVA_ENGINES_FILE}, so it isn't built. "
                            "Add a line \"${engine}=enabled\" (or \"${engine}=disabled\").")
            list(APPEND summary "${engine} (not listed)")
            continue()
        endif()
        if(RELUVA_ENGINE_STATE_${engine} STREQUAL "enabled")
            add_subdirectory("${PROJECT_SOURCE_DIR}/src/engines/${engine}" "${PROJECT_BINARY_DIR}/engines/${engine}")
            list(APPEND summary "${engine}")
        else()
            list(APPEND summary "${engine} (disabled)")
        endif()
    endforeach()
    list(JOIN summary ", " text)
    message(STATUS "Reluva engines: testbed, ${text}")

    # The registry's list of accessors, rewritten only when it changes.
    get_property(accessors GLOBAL PROPERTY RELUVA_ENGINE_ACCESSORS)
    set(content "// Generated by cmake/ReluvaEngines.cmake from engines.cfg. Do not edit.\n")
    foreach(a IN LISTS accessors)
        string(APPEND content "RELUVA_ENGINE(${a})\n")
    endforeach()
    set(generated "${PROJECT_BINARY_DIR}/generated/engines/engine_list.inc")
    file(CONFIGURE OUTPUT "${generated}" CONTENT "${content}" @ONLY)

    get_property(targets GLOBAL PROPERTY RELUVA_ENGINE_TARGETS)
    add_library(reluva_engines STATIC "${PROJECT_SOURCE_DIR}/src/engines/registry.cpp")
    target_include_directories(reluva_engines PRIVATE "${PROJECT_BINARY_DIR}/generated")
    target_link_libraries(reluva_engines PUBLIC reluva_common ${targets} PRIVATE reluva_warnings)
endfunction()
