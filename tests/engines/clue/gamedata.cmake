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
# step is skipped. An install holds one language's game, US or UK English;
# the CD holds both, and the files, play and viewer steps run for each.
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
#   viewer   the data viewer, headless, as reluva-viewer runs it: every
#            page, a few keys on each, then Esc; a checksum of every screen
#            shown, and its titles (viewer-run)
# A command that fails fails the step. What each prints (paths and timings
# taken out) is checksummed, with a summary line, into a record:
# tests/engines/clue/data/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one. Records so far, made on
# Linux: english-2.2 (the US install patched to 2.2), english-2.0 (the US
# install) and 2.0-cd (the CD, both languages); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them (docs/engines/clue/TESTING.md).
#
# Environment: RELUVA_CLUE, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the one holding Clue.exe")

# What the game is: the record's folder is named after it. An install is
# one language's; the CD holds both, and each step runs for each.
run_tool(detected detect "${game_dir}")
string(REGEX MATCHALL "clue/clue(-uk)?: [^\n]*" games "${detected}")
if(NOT games)
    message(FATAL_ERROR "Clue isn't found in ${game_dir}:\n${detected}")
endif()
set(languages "")
set(releases "")
foreach(game IN LISTS games)
    string(REGEX MATCH "clue/(clue|clue-uk): (.*)" found "${game}")
    list(APPEND languages "${CMAKE_MATCH_1}")
    list(APPEND releases "${CMAKE_MATCH_2}")
endforeach()
list(GET releases 0 release)
if(release MATCHES "2\\.0, CD\\)")
    set(slug "2.0-cd")
else()
    if(languages STREQUAL "clue-uk")
        set(slug "uk-english")
    else()
        set(slug "english")
    endif()
    if(release MATCHES "2\\.2\\)")
        string(APPEND slug "-2.2")
    elseif(release MATCHES "2\\.0\\)")
        string(APPEND slug "-2.0")
    else()
        set(slug "unknown-release")
    endif()
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
    foreach(r IN LISTS releases)
        set(record "${record}release: ${r}\n")
    endforeach()
elseif(STEP STREQUAL "files")
    foreach(language IN LISTS languages)
        if(language STREQUAL "clue-uk")
            record_run(files-uk "pictures they name" clue check "${game_dir}" -uk)
        else()
            record_run(files "pictures they name" clue check "${game_dir}")
        endif()
    endforeach()
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
    foreach(language IN LISTS languages)
        if(language STREQUAL "clue-uk")
            record_run(play-uk "screens:" clue game-run "${game_dir}" -uk)
        else()
            record_run(play "screens:" clue game-run "${game_dir}")
        endif()
    endforeach()
elseif(STEP STREQUAL "viewer")
    foreach(language IN LISTS languages)
        if(language STREQUAL "clue-uk")
            record_run(viewer-uk "left by:" clue viewer-run "${game_dir}" -uk)
        else()
            record_run(viewer "left by:" clue viewer-run "${game_dir}")
        endif()
    endforeach()
else()
    message(FATAL_ERROR "No step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
