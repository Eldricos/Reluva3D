# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Angel Game Engine's test with the game's own files: one step each
# time ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/age/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and docs/engines/age/TESTING.md.
#
# The game's folder (Midtown Madness 2 installed: the one holding
# Midtown2.exe and mm2core.ar) comes from the environment variable named by
# GAME_ENV (RELUVA_MM2). Without it the step is skipped.
#
# Each step runs reluva-tool's age commands on the game:
#   release  what the game is (detect)
#   archives every archive's files listed (ar-list)
#   files    every texture, model, city, road map, placement list, path
#            set and visibility file read; those it can't, named (check)
#   play     the game itself, headless: the opening skipped, the race
#            screen (which says it needs 3D) left; a checksum of every
#            screen shown, and its titles (game-run)
#   viewer   the data viewer, headless, as reluva-viewer runs it: its
#            pages in turn, a few keys on the textures and the files, then
#            Esc; a checksum of every screen shown, and its titles
#            (viewer-run)
# A command that fails fails the step. What each prints (paths and timings
# taken out) is checksummed, with a summary line, into a record:
# tests/engines/age/data/gamedata/<release>/<step>.txt. The step passes when
# its record is the same as the committed one. Records so far, made on
# Linux: en (the English release); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them (docs/engines/age/TESTING.md).
#
# Environment: RELUVA_MM2, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the one holding Midtown2.exe and mm2core.ar")

# What the game is: the record's folder is named after it.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "age/mm2: ([^\n]*)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Midtown Madness 2 isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
if(release MATCHES "\\(en, windows\\)")
    set(slug "en")
else()
    set(slug "unknown-release")
endif()

if(STEP STREQUAL "release")
    set(record "release: ${release}\n")
elseif(STEP STREQUAL "archives")
    # The archives (*.ar) beside the game, in name order, any letter case.
    file(GLOB entries "${game_dir}/*")
    set(archives "")
    foreach(path IN LISTS entries)
        get_filename_component(leaf "${path}" NAME)
        string(TOLOWER "${leaf}" lower)
        if(lower MATCHES "\\.ar$")
            list(APPEND archives "${lower}|${path}")
        endif()
    endforeach()
    list(SORT archives)
    foreach(entry IN LISTS archives)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 name)
        list(GET parts 1 path)
        record_run("${name}" "files" age ar-list "${path}")
    endforeach()
elseif(STEP STREQUAL "files")
    record_run(files "files read:" age check "${game_dir}")
elseif(STEP STREQUAL "play")
    record_run(play "screens:" age game-run "${game_dir}")
elseif(STEP STREQUAL "viewer")
    record_run(viewer "left by:" age viewer-run "${game_dir}")
else()
    message(FATAL_ERROR "No step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
