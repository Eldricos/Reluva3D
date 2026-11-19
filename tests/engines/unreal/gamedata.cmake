# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# The Unreal engine's test with The Fallen's own files: one step each time
# ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/unreal/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/unreal/games/ds9fallen/TESTING.md.
#
# The game's folder (Star Trek: Deep Space Nine - The Fallen installed: the
# one holding System, Maps and Textures) comes from the environment variable
# named by GAME_ENV (RELUVA_DS9FALLEN). Without it the step is skipped.
#
# Each step runs reluva-tool's unreal commands on the game:
#   release  what the game is (detect)
#   packages every package read: textures, sounds, music, meshes, levels,
#            scripts (check)
#   points   every mesh's attachment points against its bones (points)
#   scripts  every class's compiled UnrealScript written out as text
#            (script -all), and a checksum of all of it
#   levels   every map's scripts run for 20 seconds with the player logged
#            in (run -all)
#   play     a level played as the Maps page plays it: the player walks,
#            draws the rifle, saves, loads the save back and carries on;
#            and the settings a console command sets, written and read on
#            the next level (play)
# A command that fails fails the step. What each prints (paths and timings
# taken out) is checksummed, with a summary line, into a record:
# tests/engines/unreal/data/gamedata/<release>/<step>.txt. The step passes
# when its record is the same as the committed one. Records so far, made on
# Linux: en (the English Windows release); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them
# (docs/engines/unreal/games/ds9fallen/TESTING.md).
#
# Environment: RELUVA_DS9FALLEN, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the one holding System, Maps and Textures")

# What the game is: the record's folder is named after it.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "unreal/ds9fallen: ([^\n]*)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "The Fallen isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
if(release MATCHES "\\(en, windows\\)")
    set(slug "en")
else()
    set(slug "unknown-release")
endif()

if(STEP STREQUAL "release")
    set(record "release: ${release}\n")
elseif(STEP STREQUAL "packages")
    record_run(packages "^textures +[0-9]+ decoded" unreal check "${game_dir}")
elseif(STEP STREQUAL "points")
    record_run(points "attachment points against" unreal points "${game_dir}")
elseif(STEP STREQUAL "scripts")
    set(dir "${OUTPUT}/scripts/classes")
    file(REMOVE_RECURSE "${dir}")
    record_run(scripts "classes written" unreal script "${game_dir}" -all "${dir}")
    # Every class's text, in name order, as one checksum.
    file(GLOB_RECURSE files RELATIVE "${dir}" "${dir}/*")
    list(SORT files)
    set(all "")
    foreach(f IN LISTS files)
        file(SHA256 "${dir}/${f}" sum)
        string(APPEND all "${f} ${sum}\n")
    endforeach()
    string(SHA256 sum "${all}")
    string(SUBSTRING "${sum}" 0 16 sum)
    list(LENGTH files count)
    string(APPEND record "classes' text: ${sum} | ${count} files\n")
elseif(STEP STREQUAL "levels")
    record_run(levels "natives missing" unreal run "${game_dir}" -all 20)
elseif(STEP STREQUAL "play")
    # Its saves and settings in the build, not the game's folder.
    set(dir "${OUTPUT}/play/files")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}")
    record_run(saved "^where:" unreal play "${game_dir}" M04_KiraL1 saves "${dir}" config "${dir}"
        play wait 6 press 2 wait 2 where savegame QuickSave travel "?load=QuickSave.dsa" wait 2 where)
    record_run(settings "^prop " unreal play "${game_dir}" M04_KiraL1 config "${dir}"
        play wait 1 console "set Engine.PlayerPawn MouseSensitivity 5" travel M01_SiskoL1A wait 1
        prop DS9_SiskoEVA0 MouseSensitivity)
else()
    message(FATAL_ERROR "No step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
