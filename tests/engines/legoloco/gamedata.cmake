# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 The Reluva contributors

# LEGO Loco's engine test with the game's own files: one step each time
# ctest runs this script (cmake -P), as registered by
# reluva_add_gamedata_test in src/engines/legoloco/CMakeLists.txt. See
# docs/DEVELOPMENT.md (Engine tests) and docs/engines/legoloco/TESTING.md.
#
# The game's install comes from the environment variable named by GAME_ENV
# (RELUVA_LEGOLOCO), or the launcher's entry for it. Without either the
# step is skipped.
#
# Each step runs reluva-tool on the game:
#   release    what the game is (detect)
#   files      the files the game came with, by name, size and MD5, as one
#              checksum: not those a player's own games change (LEGO.INI,
#              the layout being played, the post bag's own folders)
#   resources  every entry of the resource file unpacked (rf-list)
#   formats    every object definition, board, area arrangement,
#              date-based surprise and postcard read (dat, sav, lay, ee,
#              crd)
#   run        loco.exe run on a clock of instructions (run -deterministic),
#              at 1024 by 768 with the movies skipped: one player's game
#              started from the front screen, the welcome bubble closed and
#              the Toybox opened; the screen at four moments, Windows'
#              pointer drawn where it shows, as checksums
#   network    two copies on one network in memory (pair -deterministic), at
#              800 by 600: one hosts a game, the other joins it by TCP/IP,
#              each picks an area and they go on to their boards; what each
#              did, and the longest messages they sent each other (the
#              boards), which come out the same whatever the copies' threads
#              do; not their screens, which can differ by a frame
# A command that fails fails the step. The records, in
# tests/engines/legoloco/data/gamedata/<release>/<step>.txt, hold only
# counts and checksums. The step passes when its record is the same as the
# committed one. Records so far: 1.0.18.2 (English, made on Linux).
#
# Environment: RELUVA_LEGOLOCO, the install; and those of the shared
# helpers (cmake/ReluvaGamedata.cmake): RELUVA_GAMEDATA_UPDATE=1,
# RELUVA_GAMEDATA_RECORDS.

include("${HELPERS}")
gamedata_begin("the game's install, the one holding Exe and art-res")

# What the game is: the record's folder is named after its version.
run_tool(detected detect "${game_dir}")
string(REGEX MATCH "legoloco/legoloco: LEGO Loco \\(([^)]*)\\) \\(([^)]*)\\)" found "${detected}")
if(NOT found)
    message(FATAL_ERROR "LEGO Loco isn't found in ${game_dir}:\n${detected}")
endif()
set(version "${CMAKE_MATCH_1}")
set(release "${CMAKE_MATCH_1} (${CMAKE_MATCH_2})")
string(REGEX MATCH "^[^,]*" language "${CMAKE_MATCH_2}")
set(release_folder "${version}-${language}")

if(STEP STREQUAL "release")
    set(record "${record}release: ${release}\n")
elseif(STEP STREQUAL "files")
    # Every file but those that change as it's played, by its path (in
    # lower case: copies differ), size and MD5, as one checksum.
    file(GLOB_RECURSE files LIST_DIRECTORIES false "${game_dir}/*")
    set(listing "")
    set(count 0)
    foreach(f IN LISTS files)
        file(RELATIVE_PATH rel "${game_dir}" "${f}")
        string(TOLOWER "${rel}" rel)
        if(rel MATCHES "^exe/lego\\.ini$|^art-res/~curr$|^art-res/postbag/(sort|album|att_)|/netsettings\\.dat$")
            continue()
        endif()
        file(SIZE "${f}" size)
        file(MD5 "${f}" md5)
        list(APPEND listing "${rel} ${size} ${md5}")
        math(EXPR count "${count} + 1")
    endforeach()
    list(SORT listing)
    list(JOIN listing "\n" listing)
    string(SHA256 sum "${listing}")
    string(SUBSTRING "${sum}" 0 16 sum)
    set(record "${record}files: ${sum} | ${count} files\n")
    file(MAKE_DIRECTORY "${OUTPUT}/${STEP}")
    file(WRITE "${OUTPUT}/${STEP}/files.txt" "${listing}\n")
elseif(STEP STREQUAL "resources")
    record_run(rf-list "entries, " legoloco rf-list "${game_dir}")
elseif(STEP STREQUAL "formats")
    record_run(definitions "definitions: " legoloco dat "${game_dir}")
    record_run(boards "boards read" legoloco sav "${game_dir}")
    record_run(layouts "" legoloco lay "${game_dir}")
    record_run(surprises "" legoloco ee "${game_dir}")
    record_run(postcards "postcards, " legoloco crd "${game_dir}")
elseif(STEP STREQUAL "run")
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}/save")
    file(MAKE_DIRECTORY "${out}/save")
    # Single player (the one head), the tick; then the welcome bubble's
    # close button, the pointer onto the board, and the Toybox (top left).
    set(shots loading 12 welcome 29 board 32.5 toybox 35.5)
    set(args)
    set(names)
    while(shots)
        list(POP_FRONT shots name at)
        list(APPEND args -shot ${at} "${out}/${name}.ppm")
        list(APPEND names ${name})
        file(REMOVE "${out}/${name}.ppm")
    endwhile()
    run_tool(log legoloco run "${game_dir}" -save "${out}/save" -seconds 40 -limit 20000 -nomovies
             -deterministic 50000 -desktop 1024x768
             -click 5 477 429 -click 7 830 604 -click 30 698 462 -move 31 300 200 -move 31.5 310 210
             -move 33 83 42 -click 33.5 83 42 ${args})
    file(WRITE "${out}/run.txt" "${log}")
    # What repeats from run to run: the imports, and when each screen was
    # taken; not how the program ended, after the clock ran out.
    string(REGEX MATCH "[0-9]+ imports stood in for by nothing[^\n]*" imports "${log}")
    set(record "${record}imports: ${imports}\n")
    foreach(name IN LISTS names)
        if(NOT EXISTS "${out}/${name}.ppm")
            message(FATAL_ERROR "No screen was saved as ${name}:\n${log}")
        endif()
        string(REGEX MATCH "frame [0-9]+ saved to <output>/${STEP}/${name}\\.ppm" when "${log}")
        string(REGEX MATCH "[0-9]+" frame "${when}")
        file(SHA256 "${out}/${name}.ppm" sum)
        string(SUBSTRING "${sum}" 0 16 sum)
        set(record "${record}${name}: ${sum} | frame ${frame}\n")
    endforeach()
    file(REMOVE_RECURSE "${out}/save")
elseif(STEP STREQUAL "network")
    set(out "${OUTPUT}/${STEP}")
    file(REMOVE_RECURSE "${out}/a" "${out}/b")
    file(MAKE_DIRECTORY "${out}/a" "${out}/b")
    # Both: the two heads, the tick, TCP, the tick. A hosts (the pen),
    # picks the top left area and goes on; B joins and picks the next.
    set(both -move 6.5 545 345 -click 7 550 345)
    set(protocol -click 10 718 520 -move 12.5 385 170 -click 13 390 170 -move 15 505 540 -click 15.5 510 540)
    run_tool(log legoloco pair "${game_dir}" -seconds 100 -limit 20000 -nomovies -deterministic 50000
             -save "${out}/a" ${both} -move 8 718 280 -click 8.5 718 280 ${protocol}
             -move 24 108 100 -click 25 108 100 -move 26 515 545 -click 27 515 545
             -- -save "${out}/b" ${both} ${protocol}
             -move 28 270 100 -click 29 270 100 -move 30 515 545 -click 31 515 545)
    file(WRITE "${out}/pair.txt" "${log}")
    foreach(copy IN ITEMS A B)
        string(REGEX MATCH "\\[${copy}\\] DirectPlay: (hosted|joined), ([0-9]+) players at most[^\n]*the longest ([0-9]+) bytes[^\n]*the longest ([0-9]+) bytes"
               found "${log}")
        if(NOT found)
            message(FATAL_ERROR "Copy ${copy} played no network game:\n${log}")
        endif()
        set(record "${record}${copy}: ${CMAKE_MATCH_1}, ${CMAKE_MATCH_2} players, the longest messages ${CMAKE_MATCH_3} bytes sent and ${CMAKE_MATCH_4} received\n")
    endforeach()
    string(REGEX MATCH "\\[A\\] \\[program\\] DirectPlay: hosting \"[^\"]*\"" hosting "${log}")
    string(REGEX MATCH "\\[B\\] \\[program\\] DirectPlay: joined \"[^\"]*\"" joined "${log}")
    set(record "${record}${hosting}\n${joined}\n")
    file(REMOVE_RECURSE "${out}/a" "${out}/b")
else()
    message(FATAL_ERROR "No step ${STEP}")
endif()

gamedata_end("LEGO Loco ${release}" "${release_folder}")
