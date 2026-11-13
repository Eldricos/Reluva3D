# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# LEGO Alpha Team's engine test with the game's own files: one step each
# time ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/netimmerse/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and docs/engines/netimmerse/games/legoalphateam/TESTING.md.
#
# The game's folder comes from the environment variable named by GAME_ENV
# (RELUVA_ALPHATEAM): the installed game (the folder holding LEGOATeam.exe
# and the .pac archives). Without it the step is skipped. Nothing is written
# there: what the game saves goes to the build's output folder.
#
# Each step runs reluva-tool's netimmerse commands on the game:
#   release       what the game is (detect)
#   archives      every .pac archive listed (pac-list)
#   models        every .FIN model read, its images found (fin-check)
#   start         the game's own code run headless from a new install's
#                 save to frame 300 (the logos and the attract sequence),
#                 a frame kept every 100 (at-run -fixed-clock)
#   mission-one   Mission One (M1010) played through with its solved layout
#                 and scripted clicks, at 640x480x16, to frame 3300 (its
#                 solution running), a frame kept every 300; the route
#                 every change to the emulator, the JIT and the drawing is
#                 checked against (TESTING.md)
#   mission-one-1024  the same at 1024x768x32
# A command that fails fails the step. What each prints (the game's folder
# taken out) is checksummed, with a summary line (the instructions run, for
# the runs), into a record, with a checksum of each frame kept:
# tests/engines/netimmerse/data/gamedata/<release>/<step>.txt. The step
# passes when its record is the same as the committed one. Records so far:
# english (the English retail CD's install, made on Linux); make others
# with RELUVA_GAMEDATA_UPDATE=1, and check them (TESTING.md).
#
# The runs use the fixed clock (time follows the instructions run), so each
# repeats exactly on one machine. The game's code calls the x87's sin, cos
# and the like, which Reluva works out with the system's maths library: on
# another system the last bit can differ, and a run can then go a little
# differently with nothing broken. Keep a baseline of your own there
# (RELUVA_GAMEDATA_RECORDS, docs/DEVELOPMENT.md).
#
# Environment: RELUVA_ALPHATEAM, the game's folder; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the installed game, holding LEGOATeam.exe and the .pac archives")

# What the game is: the record's folder is named after its language
# ("english"), or "unknown" for a release Reluva's table doesn't have.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "netimmerse/legoalphateam: ([^\n]*)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "LEGO Alpha Team isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
set(slug "unknown")
if(release MATCHES "\\(en, windows\\)")
    set(slug "english")
endif()

# at-run from a save made for the step (Game.txt: `game_txt`'s lines), with
# `args`, keeping its frames: the record has its output and each frame.
function(record_game name game_txt)
    set(dir "${OUTPUT}/${STEP}/${name}")
    file(REMOVE_RECURSE "${dir}")
    file(MAKE_DIRECTORY "${dir}/save" "${dir}/shots")
    string(REPLACE ";" "\r\n" lines "${game_txt}")
    file(WRITE "${dir}/save/Game.txt" "${lines}")
    record_run(${name} "after [0-9]+ instructions" netimmerse at-run "${game_dir}" -limit 0 -fixed-clock
               -save "${dir}/save" -shots "${dir}/shots" ${ARGN})
    file(GLOB shots "${dir}/shots/*.tga")
    list(SORT shots)
    foreach(shot IN LISTS shots)
        get_filename_component(leaf "${shot}" NAME_WE)
        record_file("${name}.${leaf}" "${shot}")
    endforeach()
    set(record "${record}" PARENT_SCOPE)
endfunction()

# Mission One's route: from an Adventure saved at M1010, through the menus
# (Adventure, the mission's card), its fly-over, and Go, with the parts where
# its solved layout has them; the clicks scaled to the screen's size.
function(mission_one name width height bpp)
    math(EXPR ax "${width} / 2")
    math(EXPR ay "${height} * 128 / 480")
    math(EXPR bx "${width} * 180 / 640")
    math(EXPR by "${height} * 124 / 480")
    math(EXPR gx "${width} * 572 / 640")
    math(EXPR gy "${height} * 434 / 480")
    if(width EQUAL 1024)
        # Placed as recorded at that size (the menus' layout rounds so).
        set(ay 205)
        set(bx 288)
        set(by 198)
        set(gx 915)
        set(gy 694)
    endif()
    set(game "##  Game.txt;;P0Space  M1010;P0Time  1793119257;;##  End;")
    if(NOT width EQUAL 640)
        set(game "##  Game.txt;;BPP  ${bpp};Height  ${height};Width  ${width};P0Space  M1010;P0Time  1793119257;;##  End;")
    endif()
    record_game(${name} "${game}" -present-late -click 25 -point 1510@${ax},${ay} -click 1512
                -point 1600@${bx},${by} -click 1602 -click 2400 -click 2500 -click 3000
                -point 3196@${gx},${gy} -click 3200 -quit 3300 -solved M1010 -every 300)
    set(record "${record}" PARENT_SCOPE)
endfunction()

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
elseif(STEP STREQUAL "archives")
    file(GLOB archives "${game_dir}/*.pac" "${game_dir}/*.PAC")
    list(SORT archives)
    foreach(archive IN LISTS archives)
        get_filename_component(leaf "${archive}" NAME)
        record_run("${leaf}" "files, " netimmerse pac-list "${archive}")
    endforeach()
elseif(STEP STREQUAL "models")
    set(others "")
    foreach(name Map.pac LMap.pac Etc.pac)
        if(EXISTS "${game_dir}/${name}")
            list(APPEND others "${game_dir}/${name}")
        endif()
    endforeach()
    record_run(fin-check "models with animation tracks" netimmerse fin-check "${game_dir}/Fin.pac" ${others})
elseif(STEP STREQUAL "start")
    # A new install's save: empty.
    record_game(start "" -quit 300 -every 100)
elseif(STEP STREQUAL "mission-one")
    mission_one(m1010-640 640 480 16)
elseif(STEP STREQUAL "mission-one-1024")
    mission_one(m1010-1024 1024 768 32)
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
