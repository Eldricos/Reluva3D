# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# LEGO Island's engine test with the game's own files: one step each time
# ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/legoisland/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and docs/engines/legoisland/TESTING.md.
#
# The game's folder comes from the environment variable named by GAME_ENV
# (RELUVA_LEGOISLAND), or the launcher's entry for it: an import, holding
# ISLE.EXE, LEGO1.DLL and the lego folder. Without either the step is
# skipped. Nothing is written there: what the game saves goes to the
# build's output folder.
#
# Each step runs reluva-tool's legoisland commands on the game:
#   release     what the game is (detect)
#   scripts     every SI file's header and events (si-info, si-events)
#   world       WORLD.WDB read: its worlds, parts, models and textures
#               (wdb-info)
#   animations  every world's animation list, each animation found in its
#               SI file (dta-check)
#   island      the game run on a fixed clock from a new save folder: the
#               intro skipped, a player registered, Pepper dragged to the
#               pizzeria on the Information Center's map, and out on the
#               island to frame 7000, a frame kept every 1000 (run
#               -fixed-clock)
#   turning     the same, then the left arrow held 40 frames on the island:
#               whether the player turns by time (the patch to LEGO1.DLL
#               applied), and how far it turned, in its state and frames
#   build       a player registered, Pepper dropped on the boat, the jet-
#               ski's build screen, a piece clicked and dragged
#   elevator    the elevator ridden to the observation deck
#   players     a player registered and the game quit through its door
#               (the saves, the credits); then, on the same save folder, a
#               second player registered, the first's saves moved along
# A command that fails fails the step. What each prints (the game's folder
# taken out) is checksummed, with a summary line, into a record, with a
# checksum of each frame kept and of the save files written:
# tests/engines/legoisland/data/gamedata/<release>/<step>.txt. The step
# passes when its record is the same as the committed one. Records so far:
# english-1.0 (the English 1.0 CD imported, made on Linux); make others with
# RELUVA_GAMEDATA_UPDATE=1, and check them (TESTING.md).
#
# The run's clock follows the instructions run, and its threads take turns
# by them, so it repeats exactly on one machine. The game's code calls the
# x87's sin and cos, which Reluva works out with the system's maths
# library: on another system the last bit can differ, and the run can then
# go a little differently with nothing broken. Keep a baseline of your own
# there (RELUVA_GAMEDATA_RECORDS, docs/DEVELOPMENT.md).
#
# Environment: RELUVA_LEGOISLAND, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("an import of the game, holding ISLE.EXE, LEGO1.DLL and the lego folder")

# What the game is: the record's folder is named after its release
# ("english-1.0"), or "unknown" for one Reluva's table doesn't have.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "legoisland/legoisland: LEGO Island \\(([^)]*)\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "LEGO Island isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "${release}" slug)
string(REPLACE " " "-" slug "${slug}")

if(STEP STREQUAL "release")
    record_run(release "legoisland/legoisland" detect "${game_dir}")
endif()

if(STEP STREQUAL "scripts")
    file(GLOB_RECURSE scripts "${game_dir}/*.si" "${game_dir}/*.SI")
    list(REMOVE_DUPLICATES scripts)
    list(SORT scripts)
    if(NOT scripts)
        message(FATAL_ERROR "No SI files in ${game_dir}")
    endif()
    foreach(script IN LISTS scripts)
        file(RELATIVE_PATH name "${game_dir}" "${script}")
        string(TOLOWER "${name}" name)
        string(REPLACE "/" "." name "${name}")
        record_run("${name}" "objects" legoisland si-info "${script}")
        record_run("${name}.events" "" legoisland si-events "${script}")
    endforeach()
endif()

if(STEP STREQUAL "world")
    file(GLOB_RECURSE world "${game_dir}/*/world.wdb" "${game_dir}/*/WORLD.WDB")
    if(NOT world)
        message(FATAL_ERROR "No WORLD.WDB in ${game_dir}")
    endif()
    list(GET world 0 world)
    record_run(world "parts and [0-9]+ models read" legoisland wdb-info "${world}")
endif()

if(STEP STREQUAL "animations")
    record_run(animations "" legoisland dta-check "${game_dir}")
endif()

# The intro skipped and a player registered, at frames shown: the intro
# clicked away, the register book opened, A and B, its tick.
set(register -click 60@320,240 -click 1500@330,350 -click 2000@323,306 -click 2400@168,130 -click 2500@196,130
             -click 2700@524,136)
# Pepper's portrait dragged to the pizzeria on the map.
set(to_pizzeria -drag 4500@322,100:315,210,60)

# The game run on a fixed clock to frame `until` (or its end), with `ARGN`
# for its input and frames kept, from a new save folder if `fresh`, else
# the step's last one: its output, then each frame kept (in its own
# folder) and each save file, their record lines begun with `prefix`.
function(record_game name until fresh prefix)
    set(dir "${OUTPUT}/${STEP}")
    if(fresh)
        file(REMOVE_RECURSE "${dir}/save")
    endif()
    file(REMOVE_RECURSE "${dir}/shots/${name}")
    file(MAKE_DIRECTORY "${dir}/save" "${dir}/shots/${name}")
    record_run(${name} " after [0-9]+ instructions" legoisland run "${game_dir}" -limit 0 -fixed-clock 20000
               -until ${until} -save "${dir}/save" -shots "${dir}/shots/${name}" ${ARGN})
    file(GLOB shots "${dir}/shots/${name}/*.tga")
    file(GLOB saves "${dir}/save/*")
    list(SORT saves)
    list(SORT shots)
    foreach(file IN LISTS saves shots)
        get_filename_component(leaf "${file}" NAME)
        record_file("${prefix}${leaf}" "${file}")
    endforeach()
    set(record "${record}" PARENT_SCOPE)
endfunction()

# Registered and out at the pizzeria, from a new save folder, with `ARGN`.
function(record_island name until)
    record_game(${name} ${until} TRUE "" ${register} ${to_pizzeria} ${ARGN})
    set(record "${record}" PARENT_SCOPE)
endfunction()

if(STEP STREQUAL "island")
    record_island(island 7000 -every 1000)
endif()

if(STEP STREQUAL "turning")
    # Pepper out on the island, her arrival over, by frame 9000; the left
    # arrow held from it, her view at 9001 and, turned, at 9501.
    record_island(turning 9501 -every 500 -key 9000@left,40)
    file(READ "${OUTPUT}/${STEP}/turning.txt" out)
    string(REGEX MATCH "turning by time: [^\n]*" line "${out}")
    if(NOT line MATCHES "yes")
        message(FATAL_ERROR "LEGO1.DLL wasn't patched to turn by time: ${line}")
    endif()
    set(record "${record}${line}\n")
endif()

if(STEP STREQUAL "build")
    # Registered, Pepper dropped on the boat: the lifeguard hut, its
    # baseplate clicked, the jet-ski's build screen; a piece clicked, its
    # colours and decals offered, and one dragged.
    record_game(build 14500 TRUE "" -every 500 ${register} -drag 4500@322,100:322,236,60 -click 10500@350,290
                -click 13000@330,170 -click 13300@380,110 -drag 13600@330,170:320,380,40)
endif()

if(STEP STREQUAL "elevator")
    # The left arrow to the lobby, into the elevator, its third floor: the
    # observation deck.
    record_game(elevator 6000 TRUE "" -every 500 -click 60@320,240 -click 1500@330,350 -click 2200@30,315
                -click 3000@300,260 -click 3800@402,414)
endif()

if(STEP STREQUAL "players")
    # A player registered, and the game quit through its door (the green
    # brick): its saves written, the credits, its end. Then again on the
    # same save folder, a second player registered (C and D), the first's
    # saves moved along (MoveFileA: G0.GS to G1.GS).
    record_game(quit 9000 TRUE "quit." -every 500 ${register} -click 4200@590,240 -click 5200@235,385)
    record_game(second 4500 FALSE "second." -every 500 -click 60@320,240 -click 1500@330,350 -click 2000@323,306
                -click 2400@224,130 -click 2500@252,130 -click 2700@524,136)
    if(NOT EXISTS "${OUTPUT}/${STEP}/save/G1.GS")
        message(FATAL_ERROR "The first player's saves weren't moved along for the second (no G1.GS)")
    endif()
endif()

gamedata_end("${release}" "${slug}")
