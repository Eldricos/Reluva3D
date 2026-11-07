# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Clue engine's test with the game's own files: one step each time ctest
# runs this script (cmake -P), as registered by reluva_add_gamedata_test in
# src/engines/clue/CMakeLists.txt. See docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/clue/TESTING.md.
#
# The game's folder (Clue: Murder at Boddy Mansion installed: the one
# holding Clue.exe and Data/Classic; or its CD, or a copy of it) comes from
# the environment variable named by GAME_ENV (RELUVA_CLUE). Without it the
# step is skipped.
#
# Each step runs reluva-tool's clue commands on the game:
#   release  what the game is (detect)
#   files    every image, animation, object and dialog read, the mansion
#            built from clue.lvl, and every picture the dialogs name
#            found (check)
#   levels   clue.lvl's placements, each one (level -v)
#   boards   the board it lays out, and 200 games of computer players on
#            it, the same games every time (board)
#   play     the game itself, headless: the intro, a new game of a person
#            and two computer players set up in the menus, the deal, the
#            game saved from the Esc menu, left, and loaded again; a
#            checksum of every screen shown and of the saved game (game-run)
# A command that fails fails the step. What each prints (paths and timings
# taken out) is checksummed, with a summary line, into a record:
# tests/engines/clue/data/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one. Records so far: english-2.2
# (the US install patched to 2.2, made on Linux); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them (docs/engines/clue/TESTING.md).
#
# Environment: RELUVA_CLUE, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the one holding Clue.exe")

# What the game is: the record's folder is named after it.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "clue/(clue|clue-uk): ([^\n]*)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Clue isn't found in ${game_dir}:\n${detected}")
endif()
set(game_id "${CMAKE_MATCH_1}")
set(release "${CMAKE_MATCH_2}")
if(game_id STREQUAL "clue-uk")
    set(slug "uk-english")
else()
    set(slug "english")
endif()
if(release MATCHES "\\(2\\.2\\)" OR release MATCHES ", 2\\.2\\)")
    string(APPEND slug "-2.2")
elseif(release MATCHES "2\\.0, CD\\)")
    string(APPEND slug "-2.0-cd")
elseif(release MATCHES "2\\.0\\)")
    string(APPEND slug "-2.0")
else()
    set(slug "unknown-release")
endif()

# clue.lvl: an install's Data/Classic/Levels, the CD's Data/LangIndepend/Levels.
file(GLOB_RECURSE levels "${game_dir}/*")
set(level "")
foreach(path IN LISTS levels)
    get_filename_component(leaf "${path}" NAME)
    string(TOLOWER "${leaf}" leaf)
    if(leaf STREQUAL "clue.lvl")
        set(level "${path}")
    endif()
endforeach()

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
elseif(STEP STREQUAL "files")
    if(game_id STREQUAL "clue-uk")
        record_run(files "pictures they name" clue check "${game_dir}" -uk)
    else()
        record_run(files "pictures they name" clue check "${game_dir}")
    endif()
elseif(STEP STREQUAL "levels" OR STEP STREQUAL "boards")
    if(level STREQUAL "")
        message(FATAL_ERROR "No clue.lvl in ${game_dir}")
    endif()
    if(STEP STREQUAL "levels")
        record_run(levels "placements" clue level "${level}" -v)
    else()
        record_run(boards "games:" clue board "${level}" 200)
    endif()
elseif(STEP STREQUAL "play")
    record_run(play "screens:" clue game-run "${game_dir}")
else()
    message(FATAL_ERROR "No step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
