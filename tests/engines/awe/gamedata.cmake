# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The AWE engine's test with the game's own files: one step each time ctest
# runs this script (cmake -P), as registered by reluva_add_gamedata_test in
# src/engines/awe/CMakeLists.txt. See docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/awe/games/motoe/TESTING.md.
#
# The game's folder (GAME motoe: Murder on the Orient Express installed,
# the one holding motoe.exe; GAME attwn: And Then There Were None's, with
# ac.exe) comes from the environment variable named by GAME_ENV
# (RELUVA_MOTOE, RELUVA_ATTWN). Without it the step is skipped.
#
# Each step runs reluva-tool's awe commands on the game:
#   release  what the game is (detect)
#   files    every Granny file, scene, text, movie and sound read, the
#            cameras and compressed curves compared with granny2.dll's
#            (files-check)
#   scripts  every script's commands counted, and the verbs Reluva doesn't
#            know (script-check)
#   route-search  a way from a new game to the credits (game-run -solve),
#            searched for the first time and kept in the build
#            (gamedata/awe/route.txt), as it names the game's files; its
#            checksum is in the record. A search takes up to an hour or so,
#            so it has a step of its own, with a longer time.
#   route    the whole game by that route, four ways: by the mouse with
#            sound, by the hand alone, saved and loaded after every action,
#            and wandering off it (game-check -quick).
# A command that fails fails the step. What each prints (paths and timings
# taken out) is checksummed, with a summary line, into a record:
# tests/engines/awe/data/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one. Records so far: english-1.0
# (Orient Express) and attwn-english (made on Linux); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them (each game's TESTING.md).
#
# Environment: RELUVA_MOTOE or RELUVA_ATTWN, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
if(GAME STREQUAL "attwn")
    gamedata_begin("the one holding ac.exe")
else()
    gamedata_begin("the one holding motoe.exe")
endif()

# What the game is: the record's folder is named after it.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "awe/${GAME}: ([^\n]*)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "The game (${GAME}) isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
if(GAME STREQUAL "attwn" AND release MATCHES "\\(en, windows\\)$")
    set(slug "attwn-english")
elseif(release MATCHES "\\(en, windows\\)$")
    set(slug "english-1.0")
elseif(GAME STREQUAL "attwn")
    set(slug "attwn-unknown-release")
else()
    set(slug "unknown-release")
endif()

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
elseif(STEP STREQUAL "files")
    record_run(files "Everything read|failed" awe files-check "${game_dir}" -j 4)
elseif(STEP STREQUAL "scripts")
    record_run(scripts "commands of" awe script-check "${game_dir}")
elseif(STEP STREQUAL "route-search")
    set(route "${OUTPUT}/route.txt")
    if(NOT EXISTS "${route}")
        # (Not run_tool: a search can take longer than its half hour.)
        file(MAKE_DIRECTORY "${OUTPUT}")
        execute_process(COMMAND "${TOOL}" awe game-run "${game_dir}" -solve "${route}.part" -mouse -characters
            OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE rc TIMEOUT 7000)
        if(NOT rc EQUAL 0)
            message(FATAL_ERROR "The search for a route failed (${rc}):\n${out}")
        endif()
        file(RENAME "${route}.part" "${route}")
    endif()
    record_file(route.txt "${route}")
elseif(STEP STREQUAL "route")
    set(route "${OUTPUT}/route.txt")
    if(NOT EXISTS "${route}")
        message(FATAL_ERROR "No route: the route-search step makes it (ctest -R awe.gamedata.route-search)")
    endif()
    record_run(route "all passed|FAILED" awe game-check "${game_dir}" "${route}" -quick)
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
