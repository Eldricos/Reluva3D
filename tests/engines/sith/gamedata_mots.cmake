# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# Mysteries of the Sith's test with the game's own files: one step each time
# ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/sith/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and
# docs/engines/sith/games/mots/TESTING.md.
#
# The game's install comes from RELUVA_MOTS, as Import Game... makes it
# (its CD's cut scenes and music in cd1, Jedi Knight's CD 2's marker in
# cd2). Without the variable or a launcher entry the step is skipped.
#
# Each step runs reluva-tool on the game:
#   release  what the game is (detect), and the CDs and their music found
#            (sith folders)
#   formats  every file of the kinds Reluva reads in the archives decoded,
#            each kind as a checksum (sith formats -hash)
#   movies   every cut scene's frames, palettes, sound and subtitle numbers
#            decoded, each as a checksum (sith movies -all)
#   start    the retail game run in the emulator from a new state until it
#            waits at New Player: its checks of Jedi Knight's CD 2 and its
#            own, the files it opens, its registry writes and its debug
#            output (sith host -files)
#   level    the same with a player made and New Game clicked, cut scenes
#            skipped, into the rebels' base and through it by the cheat that
#            ends a level: the cut scenes and levels reached, and every file
#            opened, up to the second level (sith host -skim)
#   wide     the same into the rebels' base in widescreen, 16:9 (854 by
#            480): the view widened, and the frames' sizes (sith host -wide)
#   wide1610 the same at 16:10 (768 by 480)
#   wide219  the same at 21:9 (1120 by 480)
#   widesw   the same at 16:9 drawn by the game's own renderer, in 8-bit
#            colour; the others through Direct3D (-d3d)
# As for Jedi Knight, the host steps record only what a run decides
# itself, the clicks at set seconds (gamedata_host.cmake).
#
# The records, in tests/engines/sith/data/gamedata/mots-<release>/<step>.txt,
# hold only counts, names of the game's files and checksums. Records so
# far: mots-us-1.0 (the US retail CD's image, made on Linux).
#
# Environment: RELUVA_MOTS, the install; and those of the shared helpers
# (cmake/ReluvaGamedata.cmake).

include("${HELPERS}")
include("${CMAKE_CURRENT_LIST_DIR}/gamedata_host.cmake")
gamedata_begin("the game's install, as Import Game... makes it")

run_tool(detected detect "${game_dir}")
string(REGEX MATCH "sith/mots: Jedi Knight: Mysteries of the Sith \\(([^)]*)\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "Mysteries of the Sith isn't found in ${game_dir}:\n${detected}")
endif()
set(release "${CMAKE_MATCH_1}")
string(TOLOWER "mots-${release}" slug)
string(REGEX REPLACE "[ ,]+" "-" slug "${slug}")

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
    run_tool(folders sith folders "${game_dir}")
    string(REGEX MATCHALL "(CD [12]: [^\n]*\\(([^)]*)\\)|music: [^\n]*)" lines "${folders}")
    foreach(line IN LISTS lines)
        string(REGEX REPLACE "^(CD [12]): .*$" "\\1: found" line "${line}")
        set(record "${record}${line}\n")
    endforeach()
elseif(STEP STREQUAL "formats")
    # Two materials fail as the game's loader fails on them (an empty file,
    # and one of version 10): the tool says so and ends with 1, kept in the
    # record with the rest of its output.
    execute_process(COMMAND "${TOOL}" sith formats "${game_dir}" -q -hash
        OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE rc TIMEOUT 1800)
    string(REPLACE "${game_dir}" "<game>" out "${out}")
    string(REGEX MATCHALL "FAILED" failed "${out}")
    list(LENGTH failed failures)
    if(NOT rc EQUAL 1 OR NOT failures EQUAL 2)
        message(FATAL_ERROR "reluva-tool sith formats ended ${rc}, ${failures} files failing:\n${out}")
    endif()
    file(MAKE_DIRECTORY "${OUTPUT}/${STEP}")
    file(WRITE "${OUTPUT}/${STEP}/formats.txt" "${out}")
    string(SHA256 sum "${out}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}formats: ${sum} | ${failures} failing, as in the game\n")
elseif(STEP STREQUAL "movies")
    record_run(movies "S5L3ECS\\.SAN" sith movies "${game_dir}" -all)
elseif(STEP STREQUAL "start")
    record_host(start 30 "" -keys "4:esc")
elseif(STEP STREQUAL "level")
    # A player "mara" made at New Player, then Single Player and New Game.
    record_host(level 95 "s1l2_astcrust\\.jkl" -skim
        -keys "4:esc,10:m,10.3:a,10.6:r,10.9:a,12:enter,16:click318x217,20:click295x108")
elseif(STEP MATCHES "^wide")
    set(width 854)
    set(drawing -d3d)
    if(STEP STREQUAL "wide1610")
        set(width 768)
    elseif(STEP STREQUAL "wide219")
        set(width 1120)
    elseif(STEP STREQUAL "widesw")
        set(drawing "")
    endif()
    record_wide(${STEP} ${width} 50 "s1l1_rebelbase\\.jkl" ${drawing} -skim
        -keys "4:esc,10:m,10.3:a,10.6:r,10.9:a,12:enter,16:click318x217,20:click295x108")
else()
    message(FATAL_ERROR "Unknown step ${STEP}")
endif()

gamedata_end("${release}" "${slug}")
